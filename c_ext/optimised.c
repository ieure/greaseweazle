
#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include <stdio.h>
#include <stdint.h>

#define FLUXOP_INDEX   1
#define FLUXOP_SPACE   2
#define FLUXOP_ASTABLE 3

/* bitarray.append(value) */
static PyObject *append_s;
static int bitarray_append(PyObject *bitarray, PyObject *value)
{
    PyObject *res = PyObject_CallMethodObjArgs(
        bitarray, append_s, value, NULL);
    if (res == NULL)
        return 0;
    Py_DECREF(res);
    return 1;
}

/* Like PyList_Append() but steals a reference to @item. */
static int PyList_Append_SR(PyObject *list, PyObject *item)
{
    int rc = PyList_Append(list, item);
    Py_DECREF(item);
    return rc;
}

static PyObject *
flux_to_bitcells(PyObject *self, PyObject *args)
{
    /* Parameters */
    PyObject *bit_array, *time_array, *revolutions;
    PyObject *index_iter, *flux_iter;
    double freq, clock_centre, clock_min, clock_max;
    double pll_period_adj, pll_phase_adj;

    /* Local variables */
    PyObject *item;
    double clock, new_ticks, ticks, to_index;
    int zeros, nbits;

    if (!PyArg_ParseTuple(args, "OOOOOdddddd",
                          &bit_array, &time_array, &revolutions,
                          &index_iter, &flux_iter,
                          &freq, &clock_centre, &clock_min, &clock_max,
                          &pll_period_adj, &pll_phase_adj))
        return NULL;

    nbits = 0;
    ticks = 0.0;
    clock = clock_centre;

    /* to_index = next(index_iter) */
    item = PyIter_Next(index_iter);
    to_index = PyFloat_AsDouble(item);
    Py_DECREF(item);
    if (PyErr_Occurred())
        return NULL;

    /* for x in flux_iter: */
    assert(PyIter_Check(flux_iter));
    while ((item = PyIter_Next(flux_iter)) != NULL) {

        double x = PyFloat_AsDouble(item);
        Py_DECREF(item);
        if (PyErr_Occurred())
            return NULL;

        /* Gather enough ticks to generate at least one bitcell. */
        ticks += x / freq;
        if (ticks < clock/2)
            continue;

        /* Clock out zero or more 0s, followed by a 1. */
        for (zeros = 0; ; zeros++) {

            /* Check if we cross the index mark. */
            to_index -= clock;
            if (to_index < 0) {
                if (PyList_Append_SR(revolutions, PyLong_FromLong(nbits)) < 0)
                    return NULL;
                nbits = 0;
                item = PyIter_Next(index_iter);
                to_index += PyFloat_AsDouble(item);
                Py_DECREF(item);
                if (PyErr_Occurred())
                    return NULL;
            }

            nbits += 1;
            ticks -= clock;
            if (PyList_Append_SR(time_array, PyFloat_FromDouble(clock)) < 0)
                return NULL;
            if (ticks < clock/2) {
                if (!bitarray_append(bit_array, Py_True))
                    return NULL;
                break;
            }

            if (!bitarray_append(bit_array, Py_False))
                return NULL;

        }

        /* PLL: Adjust clock frequency according to phase mismatch. */
        if (zeros <= 3) {
            /* In sync: adjust clock by a fraction of the phase mismatch. */
            clock += ticks * pll_period_adj;
        } else {
            /* Out of sync: adjust clock towards centre. */
            clock += (clock_centre - clock) * pll_period_adj;
        }
        /* Clamp the clock's adjustment range. */
        if (clock < clock_min)
            clock = clock_min;
        else if (clock > clock_max)
            clock = clock_max;
        /* PLL: Adjust clock phase according to mismatch. */
        new_ticks = ticks * (1.0 - pll_phase_adj);
        if (PyList_SetItem(time_array, PyList_Size(time_array)-1,
                           PyFloat_FromDouble(ticks - new_ticks)) < 0)
            return NULL;
        ticks = new_ticks;

    }

    Py_RETURN_NONE;
}


static int _read_28bit(uint8_t *p)
{
    int x;
    x  = (p[0]       ) >>  1;
    x |= (p[1] & 0xfe) <<  6;
    x |= (p[2] & 0xfe) << 13;
    x |= (p[3] & 0xfe) << 20;
    return x;
}

static PyObject *
decode_flux(PyObject *self, PyObject *args)
{
    /* Parameters */
    Py_buffer bytearray;
    PyObject *res = NULL;

    /* bytearray buffer */
    uint8_t *p;
    Py_ssize_t l;

    /* Local variables */
    PyObject *flux, *index;
    long val, ticks, ticks_since_index;
    int i, opcode;

    if (!PyArg_ParseTuple(args, "y*", &bytearray))
        return NULL;
    p = bytearray.buf;
    l = bytearray.len;

    /* assert dat[-1] == 0 */
    if ((l == 0) || (p[l-1] != 0)) {
        PyErr_SetString(PyExc_ValueError, "Flux is not NUL-terminated");
        PyBuffer_Release(&bytearray);
        return NULL;
    }
    /* len(dat) -= 1 */
    l -= 1;

    /* flux, index = [], [] */
    flux = PyList_New(0);
    index = PyList_New(0);
    /* ticks, ticks_since_index = 0, 0 */
    ticks = 0;
    ticks_since_index = 0;

    while (l != 0) {
        i = *p++;
        if (i == 255) {
            if ((l -= 2) < 0)
                goto oos;
            opcode = *p++;
            switch (opcode) {
            case FLUXOP_INDEX:
                if ((l -= 4) < 0)
                    goto oos;
                val = _read_28bit(p);
                p += 4;
                if (PyList_Append_SR(
                        index, PyLong_FromLong(
                            ticks_since_index + ticks + val)) < 0)
                    goto out;
                ticks_since_index = -(ticks + val);
                break;
            case FLUXOP_SPACE:
                if ((l -= 4) < 0)
                    goto oos;
                ticks += _read_28bit(p);
                p += 4;
                break;
            default:
                PyErr_Format(PyExc_ValueError,
                             "Bad opcode in flux stream (%d)", opcode);
                goto out;
            }
        } else {
            if (i < 250) {
                l -= 1;
                val = i;
            } else {
                if ((l -= 2) < 0)
                    goto oos;
                val = 250 + (i - 250) * 255;
                val += *p++ - 1;
            }
            ticks += val;
            if (PyList_Append_SR(flux, PyLong_FromLong(ticks)) < 0)
                goto out;
            ticks_since_index += ticks;
            ticks = 0;
        }
    }

    res = Py_BuildValue("OO", flux, index);

out:
    PyBuffer_Release(&bytearray);
    Py_DECREF(flux);
    Py_DECREF(index);
    return res;

oos:
    PyErr_SetString(PyExc_ValueError, "Unexpected end of flux");
    goto out;
}


static PyMethodDef modulefuncs[] = {
    { "flux_to_bitcells", flux_to_bitcells, METH_VARARGS, NULL },
    { "decode_flux", decode_flux, METH_VARARGS, NULL },
    { NULL }
};

static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "gwoptimised", 0, -1, modulefuncs,
};

PyMODINIT_FUNC PyInit_gwoptimised(void)
{
    append_s = Py_BuildValue("s", "append");
    return PyModule_Create(&moduledef);
}
