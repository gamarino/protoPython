# email.utils - Minimal stub for protoPython stdlib.

def formataddr(pair):
    """Return formatted address. Minimal: returns pair[0] or empty string."""
    if not pair or len(pair) < 1:
        return ""
    return str(pair[0])


_MONTHS = {
    'jan': 1, 'feb': 2, 'mar': 3, 'apr': 4, 'may': 5, 'jun': 6,
    'jul': 7, 'aug': 8, 'sep': 9, 'oct': 10, 'nov': 11, 'dec': 12,
}


def parsedate_to_datetime(date_str):
    """Parse RFC 2822-style date string to datetime. Returns None on failure."""
    if not date_str or not isinstance(date_str, str):
        return None
    try:
        from datetime import datetime
    except ImportError:
        return None
    s = date_str.strip()
    parts = s.split()
    if len(parts) < 5:
        return None
    day = int(parts[1])
    month_str = parts[2].lower()[:3]
    month = _MONTHS.get(month_str)
    if month is None:
        return None
    year = int(parts[3])
    time_part = parts[4]
    hour, minute, second = 0, 0, 0
    if ':' in time_part:
        tparts = time_part.split(':')
        if len(tparts) >= 1:
            hour = int(tparts[0])
        if len(tparts) >= 2:
            minute = int(tparts[1])
        if len(tparts) >= 3:
            sec_str = tparts[2]
            if '.' in sec_str:
                second = int(float(sec_str))
            else:
                second = int(sec_str)
    return datetime(year, month, day, hour, minute, second)
