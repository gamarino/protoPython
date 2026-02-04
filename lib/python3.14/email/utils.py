# email.utils - Minimal stub for protoPython stdlib.

def formataddr(pair):
    """Stub: returns 'pair[0]' or empty string. Full impl formats display name and address."""
    if not pair or len(pair) < 1:
        return ""
    return str(pair[0])

def parsedate_to_datetime(date_str):
    """Stub: returns None. Full impl requires date parsing."""
    return None
