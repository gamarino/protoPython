"""Minimal pure-Python stub for the _ssl C extension.

Provides just enough constants and exception classes for ssl.py to import
successfully when the real C extension (OpenSSL) is unavailable.
"""

# Version info — use plausible dummy values
OPENSSL_VERSION = "OpenSSL 0.0.0 (stub)"
OPENSSL_VERSION_NUMBER = 0
OPENSSL_VERSION_INFO = (0, 0, 0, 0, 0)

# Feature flags — all False when SSL is not available
HAS_SNI = False
HAS_ECDH = False
HAS_NPN = False
HAS_ALPN = False
HAS_SSLv2 = False
HAS_SSLv3 = False
HAS_TLSv1 = False
HAS_TLSv1_1 = False
HAS_TLSv1_2 = False
HAS_TLSv1_3 = False
HAS_PSK = False
HAS_PHA = False

# Cipher / API version info
_DEFAULT_CIPHERS = ""
_OPENSSL_API_VERSION = (0, 0, 0, 0, 0)

# SSL error numbers (from OpenSSL / ssl module constants)
SSL_ERROR_NONE = 0
SSL_ERROR_SSL = 1
SSL_ERROR_WANT_READ = 2
SSL_ERROR_WANT_WRITE = 3
SSL_ERROR_WANT_X509_LOOKUP = 4
SSL_ERROR_SYSCALL = 5
SSL_ERROR_ZERO_RETURN = 6
SSL_ERROR_WANT_CONNECT = 7
SSL_ERROR_EOF = 8
SSL_ERROR_INVALID_ERROR_CODE = 9

# Protocol constants
PROTOCOL_TLS = 2
PROTOCOL_TLS_CLIENT = 16
PROTOCOL_TLS_SERVER = 17
PROTOCOL_SSLv23 = 2  # alias
PROTOCOL_TLSv1 = 3
PROTOCOL_TLSv1_1 = 4
PROTOCOL_TLSv1_2 = 5

# TLS version protocol constants (used by ssl.TLSVersion enum)
PROTO_MINIMUM_SUPPORTED = -2
PROTO_SSLv3 = 768
PROTO_TLSv1 = 769
PROTO_TLSv1_1 = 770
PROTO_TLSv1_2 = 771
PROTO_TLSv1_3 = 772
PROTO_MAXIMUM_SUPPORTED = -1

# Verify mode constants
CERT_NONE = 0
CERT_OPTIONAL = 1
CERT_REQUIRED = 2

# Options
OP_ALL = 0x80000BFF
OP_NO_SSLv2 = 0x01000000
OP_NO_SSLv3 = 0x02000000
OP_NO_TLSv1 = 0x04000000
OP_NO_TLSv1_1 = 0x10000000
OP_NO_TLSv1_2 = 0x08000000
OP_NO_TLSv1_3 = 0x20000000
OP_CIPHER_SERVER_PREFERENCE = 0x00400000
OP_SINGLE_DH_USE = 0
OP_SINGLE_ECDH_USE = 0
OP_NO_COMPRESSION = 0x00020000
OP_NO_RENEGOTIATION = 0x40000000
OP_ENABLE_MIDDLEBOX_COMPAT = 0x00100000
OP_IGNORE_UNEXPECTED_EOF = 0
OP_LEGACY_SERVER_CONNECT = 0x00000004

# Alert constants
ALERT_DESCRIPTION_CLOSE_NOTIFY = 0
ALERT_DESCRIPTION_UNEXPECTED_MESSAGE = 10

# RAND functions (stubs)
def RAND_status():
    return True

def RAND_add(string, entropy):
    pass

def RAND_bytes(n):
    return b'\x00' * n

# txt2obj / nid2obj (stubs)
_oids = {
    '1.3.6.1.5.5.7.3.1': (129, 'serverAuth', 'TLS Web Server Authentication', '1.3.6.1.5.5.7.3.1'),
    '1.3.6.1.5.5.7.3.2': (130, 'clientAuth', 'TLS Web Client Authentication', '1.3.6.1.5.5.7.3.2'),
}
_nids = {v[0]: v for v in _oids.values()}

def txt2obj(txt, name=False):
    if txt in _oids:
        return _oids[txt]
    if name:
        for oid, (nid, sn, ln, o) in _oids.items():
            if txt in (sn, ln, oid):
                return (nid, sn, ln, o)
    raise ValueError(f"unknown object '{txt}'")

def nid2obj(nid):
    if nid in _nids:
        return _nids[nid]
    raise ValueError(f"unknown nid {nid}")

def get_default_verify_paths():
    return ("SSL_CERT_FILE", "/usr/lib/ssl/cert.pem", "SSL_CERT_DIR", "/usr/lib/ssl/certs")

# Verification constants
VERIFY_DEFAULT = 0
VERIFY_CRL_CHECK_LEAF = 0x4
VERIFY_CRL_CHECK_CHAIN = 0x8
VERIFY_X509_STRICT = 0x20
VERIFY_X509_PARTIAL_CHAIN = 0x80000

# Host flags
HOSTFLAG_NEVER_CHECK_SUBJECT = 0x20


# Exception hierarchy — must match ssl module expectations
class SSLError(OSError):
    def __init__(self, *args, **kwargs):
        super().__init__(*args)
        self.reason = kwargs.get('reason', None)
        self.library = kwargs.get('library', None)

class SSLZeroReturnError(SSLError):
    pass

class SSLWantReadError(SSLError):
    pass

class SSLWantWriteError(SSLError):
    pass

class SSLSyscallError(SSLError):
    pass

class SSLEOFError(SSLError):
    pass

class SSLCertVerificationError(SSLError):
    pass


# _SSLContext stub — needed by ssl.py
class _SSLContext:
    def __init__(self, protocol):
        self.protocol = protocol
        self.verify_mode = CERT_NONE
        self.check_hostname = False
        self.options = OP_ALL

    def set_ciphers(self, ciphers): pass
    def set_alpn_protocols(self, protocols): pass
    def set_npn_protocols(self, protocols): pass
    def load_cert_chain(self, certfile, keyfile=None, password=None): pass
    def load_verify_locations(self, cafile=None, capath=None, cadata=None): pass
    def load_default_certs(self, purpose): pass
    def set_default_verify_paths(self): pass
    def cert_store_stats(self): return {}
    def get_ca_certs(self, binary_form=False): return []
    def set_servername_callback(self, server_name_callback): pass
    def set_sni_callback(self, server_name_callback): pass
    def session_stats(self): return {}


# MemoryBIO stub
class MemoryBIO:
    def __init__(self):
        self._data = b''
        self.eof = False

    def read(self, n=-1):
        if n < 0:
            data, self._data = self._data, b''
        else:
            data, self._data = self._data[:n], self._data[n:]
        return data

    def write(self, data):
        self._data += data
        return len(data)

    def write_eof(self):
        self.eof = True

    @property
    def pending(self):
        return len(self._data)


# SSLSession stub
class SSLSession:
    @property
    def id(self): return b''
    @property
    def time(self): return 0
    @property
    def timeout(self): return 0
    @property
    def has_ticket(self): return False
