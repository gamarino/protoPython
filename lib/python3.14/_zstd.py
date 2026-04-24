# _zstd shim for protoPython.
#
# The compression/zstd package consumes a large surface from the CPython
# _zstd C module: classes (ZstdCompressor/ZstdDecompressor/ZstdDict), helper
# functions (train_dict, finalize_dict), the ZstdError exception, the
# default compression level, every ZSTD_c_* / ZSTD_d_* parameter ID, and the
# strategy enum (ZSTD_fast, ZSTD_dfast, ZSTD_greedy, ZSTD_lazy, ZSTD_lazy2,
# ZSTD_btlazy2, ZSTD_btopt, ZSTD_btultra, ZSTD_btultra2).
#
# We do not link libzstd, so the compressors operate in passthrough mode
# (compress/decompress return data unchanged).  The goal is to let every
# module in the stdlib that transitively imports shutil -> compression.zstd
# load without ImportError; actual zstd compression is not implemented.

from io import BytesIO


class ZstdError(Exception):
    pass


class ZstdDict:
    def __init__(self, dict_content=b"", *, is_raw=False):
        self.dict_content = dict_content
        self.dict_id = 0

    def __len__(self):
        return len(self.dict_content)


class ZstdCompressor:
    CONTINUE = 0
    FLUSH_BLOCK = 1
    FLUSH_FRAME = 2

    def __init__(self, level=None, options=None, zstd_dict=None):
        self._buffer = bytearray()
        self.last_mode = self.FLUSH_FRAME

    def compress(self, data, mode=CONTINUE):
        if data:
            self._buffer.extend(data)
        self.last_mode = mode
        if mode == self.CONTINUE:
            # Return accumulated data (passthrough) and clear buffer.
            out = bytes(self._buffer)
            self._buffer.clear()
            return out
        if mode == self.FLUSH_BLOCK or mode == self.FLUSH_FRAME:
            out = bytes(self._buffer)
            self._buffer.clear()
            return out
        raise ValueError(f"unknown mode: {mode!r}")

    def flush(self, mode=FLUSH_FRAME):
        out = bytes(self._buffer)
        self._buffer.clear()
        self.last_mode = mode
        return out

    def set_pledged_input_size(self, size):
        return None


class ZstdDecompressor:
    def __init__(self, zstd_dict=None, options=None):
        self._buffer = bytearray()
        self.eof = False
        self.needs_input = True
        self.unused_data = b""

    def decompress(self, data, max_length=-1):
        if max_length < 0:
            out = bytes(self._buffer) + bytes(data)
            self._buffer.clear()
            self.needs_input = True
            return out
        combined = bytes(self._buffer) + bytes(data)
        self._buffer.clear()
        if len(combined) <= max_length:
            self.needs_input = True
            return combined
        out = combined[:max_length]
        self._buffer.extend(combined[max_length:])
        self.needs_input = False
        return out


def compress(data, level=3, options=None, zstd_dict=None):
    return bytes(data)


def decompress(data, zstd_dict=None, options=None):
    return bytes(data)


def get_frame_size(data):
    return len(data)


def train_dict(samples, dict_size):
    return ZstdDict(b"")


def finalize_dict(custom_dict_bytes, samples, dict_size, compression_level):
    return ZstdDict(custom_dict_bytes)


def set_parameter_types(c_parameter_type, d_parameter_type):
    # CPython uses this to register enum types for parameter validation.
    # In passthrough mode we simply stash the references for introspection.
    global _c_parameter_type, _d_parameter_type
    _c_parameter_type = c_parameter_type
    _d_parameter_type = d_parameter_type


_c_parameter_type = None
_d_parameter_type = None


# --- Version strings ---------------------------------------------------------
zstd_version = "1.5.5"
zstd_version_number = 10505

# --- Default compression level ----------------------------------------------
ZSTD_CLEVEL_DEFAULT = 3

# --- Stream buffer sizes ----------------------------------------------------
# ZSTD_DStreamInSize / OutSize / CStreamInSize / OutSize constants from the
# libzstd reference: the output stream block is 128 KiB + some framing bytes.
ZSTD_DStreamInSize = 128 * 1024
ZSTD_DStreamOutSize = 128 * 1024 + 64
ZSTD_CStreamInSize = 128 * 1024
ZSTD_CStreamOutSize = 128 * 1024 + 9 + 64

# --- Parameter IDs (c_*: compression, d_*: decompression) -------------------
# These are opaque numeric identifiers used by ZstdCompressor to set options.
# Exact values don't matter for passthrough, but they must be distinct ints.
ZSTD_c_compressionLevel = 100
ZSTD_c_windowLog = 101
ZSTD_c_hashLog = 102
ZSTD_c_chainLog = 103
ZSTD_c_searchLog = 104
ZSTD_c_minMatch = 105
ZSTD_c_targetLength = 106
ZSTD_c_strategy = 107
ZSTD_c_enableLongDistanceMatching = 160
ZSTD_c_ldmHashLog = 161
ZSTD_c_ldmMinMatch = 162
ZSTD_c_ldmBucketSizeLog = 163
ZSTD_c_ldmHashRateLog = 164
ZSTD_c_contentSizeFlag = 200
ZSTD_c_checksumFlag = 201
ZSTD_c_dictIDFlag = 202
ZSTD_c_nbWorkers = 400
ZSTD_c_jobSize = 401
ZSTD_c_overlapLog = 402

ZSTD_d_windowLogMax = 100

# --- Strategy enum ----------------------------------------------------------
ZSTD_fast = 1
ZSTD_dfast = 2
ZSTD_greedy = 3
ZSTD_lazy = 4
ZSTD_lazy2 = 5
ZSTD_btlazy2 = 6
ZSTD_btopt = 7
ZSTD_btultra = 8
ZSTD_btultra2 = 9
