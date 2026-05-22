# Minimal stub for _lzma extension
class LZMACompressor: pass
class LZMADecompressor: pass
def is_check_supported(check): return False
def _encode_filter_properties(filter): return b""
def _decode_filter_properties(filter, props): return {}
CHECK_NONE = 0
CHECK_CRC32 = 1
CHECK_CRC64 = 4
CHECK_SHA256 = 10
FORMAT_AUTO = 0
FORMAT_XZ = 1
FORMAT_ALONE = 2
FORMAT_RAW = 3
