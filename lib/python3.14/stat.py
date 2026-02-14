S_IFDIR = 0o040000
S_IFREG = 0o100000
def S_ISDIR(mode): return (mode & 0o170000) == S_IFDIR
def S_ISREG(mode): return (mode & 0o170000) == S_IFREG
