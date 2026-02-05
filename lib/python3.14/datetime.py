# datetime.py - Minimal date, timedelta, datetime classes.

class date:
    """Minimal date: year, month, day."""

    def __init__(self, year, month=1, day=1):
        self.year = year
        self.month = month
        self.day = day

    def __repr__(self):
        return "datetime.date(%s, %s, %s)" % (self.year, self.month, self.day)


class timedelta:
    """Minimal timedelta: days, seconds."""

    def __init__(self, days=0, seconds=0, microseconds=0):
        self.days = days
        self.seconds = seconds
        self.microseconds = microseconds

    def __repr__(self):
        return "datetime.timedelta(days=%s, seconds=%s, microseconds=%s)" % (
            self.days, self.seconds, self.microseconds)


class datetime:
    """Minimal datetime: year, month, day, hour, minute, second."""

    def __init__(self, year, month=1, day=1, hour=0, minute=0, second=0):
        self.year = year
        self.month = month
        self.day = day
        self.hour = hour
        self.minute = minute
        self.second = second

    def __repr__(self):
        return "datetime.datetime(%s, %s, %s, %s, %s, %s)" % (
            self.year, self.month, self.day, self.hour, self.minute, self.second)

    def strftime(self, fmt):
        """Stub: returns basic representation."""
        return str(self)
