"""Minimal pure-Python stub for the _socket C extension.

Provides enough constants and a skeleton socket class for socket.py to
import successfully when the real C extension is unavailable.
"""

import errno as _errno
import os as _os

# Address families
AF_UNSPEC = 0
AF_UNIX = 1
AF_INET = 2
AF_INET6 = 10
AF_PACKET = 17
AF_NETLINK = 16
AF_CAN = 29
AF_BLUETOOTH = 31
AF_ALG = 38
AF_TIPC = 30
AF_RDS = 21
AF_VSOCK = 40

# Socket types
SOCK_STREAM = 1
SOCK_DGRAM = 2
SOCK_RAW = 3
SOCK_RDM = 4
SOCK_SEQPACKET = 5
SOCK_CLOEXEC = 524288
SOCK_NONBLOCK = 2048

# Socket options
SOL_SOCKET = 1
SO_REUSEADDR = 2
SO_KEEPALIVE = 9
SO_SNDBUF = 7
SO_RCVBUF = 8
SO_ERROR = 4
SO_TYPE = 3
SO_BROADCAST = 6
SO_LINGER = 13

# IP options
IPPROTO_IP = 0
IPPROTO_TCP = 6
IPPROTO_UDP = 17
IPPROTO_IPV6 = 41
IPPROTO_RAW = 255
IP_ADD_MEMBERSHIP = 35
IP_DROP_MEMBERSHIP = 36
IP_MULTICAST_TTL = 33
IP_MULTICAST_LOOP = 34
IP_MULTICAST_IF = 32
IP_TTL = 2
IP_TOS = 1
TCP_NODELAY = 1
TCP_KEEPALIVE = 9
TCP_KEEPIDLE = 4
TCP_KEEPINTVL = 5
TCP_KEEPCNT = 6

# IPv6 options
IPV6_V6ONLY = 26
IPV6_JOIN_GROUP = 20
IPV6_LEAVE_GROUP = 21

# Other constants
SHUT_RD = 0
SHUT_WR = 1
SHUT_RDWR = 2
MSG_PEEK = 2
MSG_WAITALL = 256
MSG_DONTWAIT = 64
MSG_NOSIGNAL = 16384
MSG_OOB = 1

# Error constants
EBADF = _errno.EBADF
EAGAIN = _errno.EAGAIN
EWOULDBLOCK = _errno.EWOULDBLOCK
EINPROGRESS = _errno.EINPROGRESS
EINVAL = _errno.EINVAL
ENOTCONN = _errno.ENOTCONN
ECONNRESET = _errno.ECONNRESET
ECONNREFUSED = _errno.ECONNREFUSED
ECONNABORTED = _errno.ECONNABORTED
ETIMEDOUT = _errno.ETIMEDOUT

# SCM constants (for sendmsg/recvmsg)
SCM_RIGHTS = 1

# CMSG helpers
def CMSG_LEN(length):
    # Minimal: align to 8 bytes
    return (((8 + 0 + 7) >> 3) << 3) + length

def CMSG_SPACE(length):
    return CMSG_LEN(length)

# getaddrinfo flags
AI_PASSIVE = 1
AI_CANONNAME = 2
AI_NUMERICHOST = 4
AI_NUMERICSERV = 1024
AI_ALL = 256
AI_ADDRCONFIG = 1024
AI_V4MAPPED = 2048
NI_NUMERICHOST = 1
NI_NUMERICSERV = 2
NI_NOFQDN = 4
NI_NAMEREQD = 8
NI_DGRAM = 16

# has_dualstack_ipv6
def has_dualstack_ipv6():
    return False

has_ipv6 = True

# gethostname
def gethostname():
    return _os.uname().nodename if hasattr(_os, 'uname') else 'localhost'

# getfqdn
def getfqdn(name=''):
    return name or gethostname()

# getaddrinfo stub
def getaddrinfo(host, port, family=0, type=0, proto=0, flags=0):
    raise OSError("getaddrinfo: not implemented in stub")

# getnameinfo stub
def getnameinfo(sockaddr, flags):
    raise OSError("getnameinfo: not implemented in stub")

# socketpair stub
def socketpair(family=AF_UNIX, type=SOCK_STREAM, proto=0):
    raise OSError("socketpair: not implemented in stub")

# setdefaulttimeout / getdefaulttimeout
_default_timeout = None

def setdefaulttimeout(timeout):
    global _default_timeout
    _default_timeout = timeout

def getdefaulttimeout():
    return _default_timeout

# create_connection stub
def create_connection(address, timeout=None, source_address=None):
    raise OSError("create_connection: not implemented in stub")

# inet_aton / inet_ntoa
def inet_aton(ip_string):
    parts = ip_string.split('.')
    if len(parts) != 4:
        raise OSError("illegal IP address string passed to inet_aton")
    return bytes(int(p) for p in parts)

def inet_ntoa(packed_ip):
    if len(packed_ip) != 4:
        raise OSError("packed IP wrong length for inet_ntoa")
    return '.'.join(str(b) for b in packed_ip)

def inet_pton(address_family, ip_string):
    raise OSError("inet_pton: not implemented in stub")

def inet_ntop(address_family, packed_ip):
    raise OSError("inet_ntop: not implemented in stub")

# htons / ntohs / htonl / ntohl
def htons(x): return x
def ntohs(x): return x
def htonl(x): return x
def ntohl(x): return x

# error is an alias for OSError in Python 3
error = OSError
herror = OSError
gaierror = OSError
timeout = TimeoutError


class socket:
    """Minimal stub for _socket.socket."""

    def __init__(self, family=AF_INET, type=SOCK_STREAM, proto=0, fileno=None):
        self.family = family
        self.type = type
        self.proto = proto
        self._fileno = fileno
        self._closed = False

    def fileno(self):
        return self._fileno if self._fileno is not None else -1

    def close(self):
        self._closed = True

    def __del__(self):
        self.close()

    def setsockopt(self, level, optname, value):
        pass

    def getsockopt(self, level, optname, buflen=None):
        return 0

    def setblocking(self, flag):
        pass

    def settimeout(self, timeout):
        pass

    def gettimeout(self):
        return None

    def bind(self, address):
        raise OSError("bind: not implemented in stub")

    def listen(self, backlog=5):
        raise OSError("listen: not implemented in stub")

    def accept(self):
        raise OSError("accept: not implemented in stub")

    def connect(self, address):
        raise OSError("connect: not implemented in stub")

    def connect_ex(self, address):
        return _errno.ENOTCONN

    def send(self, data, flags=0):
        raise OSError("send: not implemented in stub")

    def sendall(self, data, flags=0):
        raise OSError("sendall: not implemented in stub")

    def recv(self, bufsize, flags=0):
        raise OSError("recv: not implemented in stub")

    def recvfrom(self, bufsize, flags=0):
        raise OSError("recvfrom: not implemented in stub")

    def shutdown(self, how):
        pass

    def getsockname(self):
        return ('', 0)

    def getpeername(self):
        raise OSError("getpeername: not implemented in stub")

    def makefile(self, mode='r', buffering=None, *args, **kwargs):
        raise OSError("makefile: not implemented in stub")

    def share(self, pid):
        raise OSError("share: not implemented in stub")

    def __repr__(self):
        return f'<_socket.socket fd={self.fileno()}, family={self.family}, type={self.type}, proto={self.proto}>'
