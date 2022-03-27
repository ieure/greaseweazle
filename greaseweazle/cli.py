# cli.py
#
# Greaseweazle control script.
#
# Written & released by Keir Fraser <keir.xen@gmail.com>
#
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import sys, time, struct, textwrap
import importlib
import bitarray
import crcmod
import serial.tools.list_ports
import requests
import pkg_resources

actions = [ 'info',
            'read',
            'write',
            'convert',
            'erase',
            'clean',
            'seek',
            'delays',
            'update',
            'pin',
            'reset',
            'bandwidth',
            'rpm' ]

def usage(argv):
    print("Usage: %s [--time] [action] [-h] ..." % (argv[0]))
    print("  --time      Print elapsed time after action is executed")
    print("  -h, --help  Show help message for specified action")
    print("Actions:")
    for a in actions:
        mod = importlib.import_module('greaseweazle.tools.' + a)
        print('  %-12s%s' % (a, mod.__dict__['description']))
    return 1

def main():
    backtrace = False
    start_time = None

    # Put all logging/printing on stderr. This keeps stdout clean for future use.
    # Configure line buffering, even if the logging output is not to a console.
    sys.stderr.reconfigure(line_buffering=True)
    sys.stdout = sys.stderr

    argv = sys.argv

    while len(argv) > 1 and argv[1].startswith('--'):
        if argv[1] == '--bt':
            backtrace = True
        elif argv[1] == '--time':
            start_time = time.time()
        else:
            return usage(argv)
        argv = [argv[0]] + argv[2:]

    if len(argv) < 2 or argv[1] not in actions:
        return usage(argv)

    mod = importlib.import_module('greaseweazle.tools.' + argv[1])
    main = mod.__dict__['main']
    try:
        res = main(argv)
        if res is None:
            res = 0
    except (IndexError, AssertionError, TypeError, KeyError, struct.error):
        raise
    except KeyboardInterrupt:
        if backtrace: raise
        res = 1
    except Exception as err:
        if backtrace: raise
        print("** FATAL ERROR:")
        print(textwrap.dedent(str(err)))
        res = 1

    if start_time is not None:
        elapsed = time.time() - start_time
        print("Time elapsed: %.2f seconds" % elapsed)

    return res

# Local variables:
# python-indent: 4
# End:
