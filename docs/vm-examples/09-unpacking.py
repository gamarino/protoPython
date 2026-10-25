def unpack(pair, rest):
    a, b = pair
    first, *others = rest
    return a, b, first, others


def options(**kwargs):
    return kwargs


def forward(f, args, kw):
    return f(*args, **kw)
