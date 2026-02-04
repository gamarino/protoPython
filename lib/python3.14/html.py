# html.py - Minimal html.escape / html.unescape for protoPython stdlib.

def escape(s, quote=True):
    """Replace &, <, > and optionally " and ' with HTML entities."""
    s = str(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    if quote:
        s = s.replace('"', '&quot;').replace("'", '&#x27;')
    return s


def unescape(s):
    """Replace HTML entities &amp; &lt; &gt; &quot; &#x27; &#39; with & < > " '."""
    if not s:
        return s
    s = str(s)
    s = s.replace('&amp;', '&').replace('&lt;', '<').replace('&gt;', '>')
    s = s.replace('&quot;', '"').replace('&#x27;', "'").replace('&#39;', "'")
    return s
