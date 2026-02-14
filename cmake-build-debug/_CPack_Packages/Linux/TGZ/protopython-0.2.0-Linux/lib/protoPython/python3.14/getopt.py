# getopt.py - Parse command-line options. getopt(args, shortopts, longopts=[]) -> (options, args).

def getopt(args, shortopts, longopts=None):
    """Parse args using shortopts (e.g. 'ab:c') and longopts (e.g. ['foo=', 'bar']). Returns ([(opt, value), ...], remaining_args)."""
    if longopts is None:
        longopts = []
    args = list(args)
    opts = []
    short_takes_arg = {}
    i = 0
    while i < len(shortopts):
        c = shortopts[i]
        if i + 1 < len(shortopts) and shortopts[i + 1] == ':':
            short_takes_arg[c] = True
            i += 2
        else:
            short_takes_arg[c] = False
            i += 1
    long_takes_arg = {}
    long_names = []
    for s in longopts:
        if s.endswith('='):
            long_takes_arg[s[:-1]] = True
            long_names.append(s[:-1])
        else:
            long_takes_arg[s] = False
            long_names.append(s)
    pos = 0
    while pos < len(args):
        a = args[pos]
        if a == '--':
            pos += 1
            break
        if a.startswith('--'):
            name = a[2:].split('=', 1)
            key = name[0]
            if key not in long_names:
                raise GetoptError('option --%s not recognized' % key, key)
            if long_takes_arg.get(key, False):
                if len(name) == 2:
                    val = name[1]
                else:
                    pos += 1
                    if pos >= len(args):
                        raise GetoptError('option --%s requires argument' % key, key)
                    val = args[pos]
                opts.append(('--' + key, val))
            else:
                opts.append(('--' + key, ''))
            pos += 1
        elif a.startswith('-') and len(a) > 1:
            for j, c in enumerate(a[1:]):
                if c not in short_takes_arg:
                    raise GetoptError('option -%s not recognized' % c, c)
                if short_takes_arg[c]:
                    rest = a[1 + j + 1:]
                    if rest:
                        opts.append(('-' + c, rest))
                        pos += 1
                        break
                    pos += 1
                    if pos >= len(args):
                        raise GetoptError('option -%s requires argument' % c, c)
                    opts.append(('-' + c, args[pos]))
                    pos += 1
                    break
                else:
                    opts.append(('-' + c, ''))
            else:
                pos += 1
        else:
            break
    return (opts, args[pos:])


class GetoptError(Exception):
    """Raised when an option is not recognized or requires an argument."""
    def __init__(self, msg, opt):
        self.msg = msg
        self.opt = opt
        Exception.__init__(self, msg)
