# argparse.py - Minimal argument parsing for protoPython.

import sys


class Namespace:
    """Minimal namespace object for parse_args result."""
    def __init__(self, **kwargs):
        for k, v in kwargs.items():
            setattr(self, k, v)


class ArgumentParser:
    """Minimal ArgumentParser: add_argument for positional and --flag; parse_args returns namespace."""

    def __init__(self, **kwargs):
        self._positional = []
        self._optional = {}
        self._defaults = {}

    def add_argument(self, *args, **kwargs):
        dest = kwargs.get('dest')
        if dest is None and args:
            name = args[0]
            if name.startswith('--'):
                dest = name[2:].replace('-', '_')
            elif name.startswith('-') and len(name) > 1:
                dest = name[1:].replace('-', '_')
            else:
                dest = name.replace('-', '_')
        if dest:
            default = kwargs.get('default')
            self._defaults[dest] = default
            if args and (args[0].startswith('-') or len(args[0]) == 0):
                for a in args:
                    if a.startswith('-'):
                        self._optional[a] = dest
            else:
                self._positional.append({'dest': dest, 'nargs': kwargs.get('nargs', 1)})
        return None

    def parse_args(self, args=None):
        if args is None:
            args = getattr(sys, 'argv', [])[1:]
        result = dict(self._defaults)
        i = 0
        for opt_spec in self._positional:
            dest = opt_spec['dest']
            nargs = opt_spec.get('nargs', 1)
            if nargs == '?':
                if i < len(args) and not args[i].startswith('-'):
                    result[dest] = args[i]
                    i += 1
            elif nargs == '*' or nargs == '+':
                vals = []
                while i < len(args) and not args[i].startswith('-'):
                    vals.append(args[i])
                    i += 1
                result[dest] = vals if nargs == '+' and vals else vals
            elif i < len(args):
                result[dest] = args[i]
                i += 1
        while i < len(args):
            arg = args[i]
            if arg in self._optional:
                dest = self._optional[arg]
                if i + 1 < len(args) and not args[i + 1].startswith('-'):
                    result[dest] = args[i + 1]
                    i += 2
                    continue
                else:
                    result[dest] = True
            elif '=' in arg and arg.startswith('--'):
                eq = arg.index('=')
                flag = arg[:eq]
                if flag in self._optional:
                    dest = self._optional[flag]
                    result[dest] = arg[eq + 1:]
            i += 1
        return Namespace(**result)
