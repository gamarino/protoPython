"""
plistlib: XML plist load, loads, dump, dumps.
Apple Property List format; supports dict, list, str, int, float, bool, bytes, date.
"""

import base64
from datetime import datetime

# XML entity map for encoding
_ENTITY_MAP = {
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&apos;',
}


def _escape(s):
    """Escape string for XML text content."""
    out = []
    for c in s:
        out.append(_ENTITY_MAP.get(c, c))
    return ''.join(out)


def _unescape(s):
    """Unescape XML entities."""
    s = s.replace('&lt;', '<').replace('&gt;', '>').replace('&quot;', '"')
    s = s.replace('&apos;', "'").replace('&amp;', '&')
    return s


def _find_tag_end(data, start, tag):
    """Find end of tag; returns index past </tag>. Handles nested tags."""
    open_tag = '<' + tag
    close_tag = '</' + tag + '>'
    if tag in ('true', 'false', 'date', 'data', 'integer', 'real'):
        close_tag = '</' + tag + '>'
    depth = 0
    i = start
    while i < len(data):
        if data[i:i + len(close_tag)] == close_tag and depth == 0:
            return i + len(close_tag)
        if data[i] == '<':
            # check for same-tag open
            if data[i:i + len(open_tag)] == open_tag:
                rest = data[i + len(open_tag):i + len(open_tag) + 20]
                if rest.startswith(' ') or rest.startswith('>'):
                    depth += 1
            elif data[i:i + len(close_tag)] == close_tag:
                depth -= 1
        i += 1
    return -1


def _parse_tag_content(data, start, end, tag):
    """Extract content between <tag> and </tag>. Handles CDATA and entities."""
    open_tag = '<' + tag + '>'
    open_len = len(open_tag)
    if data[start:start + open_len] != open_tag:
        return None, end
    content_start = start + open_len
    close_tag = '</' + tag + '>'
    close_idx = data.find(close_tag, content_start)
    if close_idx < 0:
        return None, end
    content = data[content_start:close_idx]
    return content, close_idx + len(close_tag)


def _parse_plist_value(data, start, end, dict_type):
    """Parse a single plist value (dict, array, string, etc.) starting at start."""
    data_slice = data[start:end]
    i = start
    # skip whitespace
    while i < end and data[i] in ' \t\n\r':
        i += 1
    if i >= end:
        return None, i
    if data[i] != '<':
        return None, i
    # determine tag
    tag_end = data.find('>', i)
    if tag_end < 0:
        return None, i
    tag = data[i + 1:tag_end].strip().split()[0] if ' ' in data[i + 1:tag_end] else data[i + 1:tag_end]
    content_start = tag_end + 1
    if tag in ('true', 'false'):
        close = data.find('</' + tag + '>', content_start)
        if close < 0:
            return (tag == 'true'), content_start
        return (tag == 'true'), close + len('</' + tag + '>')
    if tag == 'integer':
        content, next_i = _parse_tag_content(data, i, end, 'integer')
        if content is None:
            return 0, content_start
        return int(content.strip()), next_i
    if tag == 'real':
        content, next_i = _parse_tag_content(data, i, end, 'real')
        if content is None:
            return 0.0, content_start
        return float(content.strip()), next_i
    if tag == 'string':
        content, next_i = _parse_tag_content(data, i, end, 'string')
        if content is None:
            return '', content_start
        return _unescape(content), next_i
    if tag == 'data':
        content, next_i = _parse_tag_content(data, i, end, 'data')
        if content is None:
            return b'', content_start
        return base64.b64decode(content.strip().replace('\n', '').replace(' ', '')), next_i
    if tag == 'date':
        content, next_i = _parse_tag_content(data, i, end, 'date')
        if content is None:
            return None, content_start
        s = content.strip()
        try:
            if 'T' in s:
                date_part, t = s.split('T', 1)
                time_part = t.split('+')[0].split('-')[0].split('Z')[0]
                parts = date_part.split('-') + time_part.split(':')
                if len(parts) >= 6:
                    return datetime(
                        int(parts[0]), int(parts[1]), int(parts[2]),
                        int(parts[3]), int(parts[4]), int(float(parts[5]) if '.' in parts[5] else parts[5])
                    )
            parts = s.replace('T', '-').replace(':', '-').split('-')
            if len(parts) >= 3:
                return datetime(int(parts[0]), int(parts[1]), int(parts[2]))
        except Exception:
            pass
        return None, next_i
    if tag == 'dict':
        result = dict_type()
        j = content_start
        while j < end:
            while j < end and data[j] in ' \t\n\r':
                j += 1
            if j >= end:
                break
            if data[j:j + 6] == '</dict>':
                j = data.find('</dict>', j) + 7
                break
            if data[j:j + 5] != '<key>':
                break
            key_content, j = _parse_tag_content(data, j, end, 'key')
            if key_content is None:
                break
            key = _unescape(key_content)
            val, j = _parse_plist_value(data, j, end, dict_type)
            if val is None:
                break
            result[key] = val
        return result, j
    if tag == 'array':
        result = []
        j = content_start
        while j < end:
            while j < end and data[j] in ' \t\n\r':
                j += 1
            if j >= end or data[j] != '<':
                break
            if data[j:j + 7] == '</array>':
                j = data.find('</array>', j) + 8
                break
            val, j = _parse_plist_value(data, j, end, dict_type)
            if val is None:
                break
            result.append(val)
        return result, j
    return None, content_start


def _locate_plist_root(data):
    """Find the root element inside <plist>."""
    start = data.find('<plist')
    if start < 0:
        return -1, -1
    start = data.find('>', start) + 1
    if data.find('<dict>', start) >= 0 and (data.find('<array>', start) < 0 or data.find('<dict>', start) < data.find('<array>', start)):
        root_start = data.find('<dict>', start)
        if root_start >= 0:
            end = _find_tag_end(data, root_start + 6, 'dict')
            if end >= 0:
                return root_start, end
    if data.find('<array>', start) >= 0:
        root_start = data.find('<array>', start)
        if root_start >= 0:
            end = _find_tag_end(data, root_start + 7, 'array')
            if end >= 0:
                return root_start, end
    return -1, -1


def loads(data, *, fmt=None, dict_type=dict):
    """Parse plist from bytes or str. Returns dict or list."""
    if isinstance(data, bytes):
        data = data.decode('utf-8', errors='replace')
    root_start, root_end = _locate_plist_root(data)
    if root_start < 0:
        return dict_type()
    val, _ = _parse_plist_value(data, root_start, root_end, dict_type)
    return val if val is not None else dict_type()


def load(fp, *, fmt=None, dict_type=dict):
    """Parse plist from file-like object."""
    content = fp.read()
    if isinstance(content, bytes):
        content = content.decode('utf-8', errors='replace')
    return loads(content, fmt=fmt, dict_type=dict_type)


def _plist_dump_value(value, out, indent, sort_keys, skipkeys):
    """Serialize a value to XML plist format."""
    if value is None:
        out.append('<null/>')
    elif value is True:
        out.append('<true/>')
    elif value is False:
        out.append('<false/>')
    elif isinstance(value, bool):
        out.append('<true/>' if value else '<false/>')
    elif isinstance(value, int):
        out.append('<integer>%d</integer>' % value)
    elif isinstance(value, float):
        s = repr(value)
        if 'e' in s.lower():
            out.append('<real>%s</real>' % s)
        else:
            out.append('<real>%s</real>' % s)
    elif isinstance(value, str):
        out.append('<string>%s</string>' % _escape(value))
    elif isinstance(value, bytes):
        out.append('<data>%s</data>' % base64.b64encode(value).decode('ascii'))
    elif isinstance(value, datetime):
        out.append('<date>%04d-%02d-%02dT%02d:%02d:%02d</date>' % (
            value.year, value.month, value.day,
            value.hour, value.minute, value.second))
    elif isinstance(value, dict):
        out.append('<dict>')
        keys = sorted(value.keys()) if sort_keys else list(value.keys())
        for k in keys:
            if not isinstance(k, str):
                if skipkeys:
                    continue
                raise TypeError("keys must be strings")
            v = value[k]
            out.append('<key>%s</key>' % _escape(k))
            _plist_dump_value(v, out, indent, sort_keys, skipkeys)
        out.append('</dict>')
    elif isinstance(value, (list, tuple)):
        out.append('<array>')
        for item in value:
            _plist_dump_value(item, out, indent, sort_keys, skipkeys)
        out.append('</array>')
    else:
        if skipkeys:
            return
        raise TypeError("unsupported type: %s" % type(value).__name__)


def dumps(value, *, fmt=None, sort_keys=True, skipkeys=False):
    """Serialize value to XML plist bytes."""
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>')
    out.append('<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">')
    out.append('<plist version="1.0">')
    _plist_dump_value(value, out, 0, sort_keys, skipkeys)
    out.append('</plist>')
    return '\n'.join(out).encode('utf-8')


def dump(value, fp, *, fmt=None, sort_keys=True, skipkeys=False):
    """Serialize value to XML plist and write to fp."""
    data = dumps(value, fmt=fmt, sort_keys=sort_keys, skipkeys=skipkeys)
    if hasattr(fp, 'write'):
        fp.write(data)
    elif hasattr(fp, 'buffer'):
        fp.buffer.write(data)
