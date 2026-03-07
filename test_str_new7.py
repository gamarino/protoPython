def f():
    return "".__class__

import dis
dis.dis(f)
