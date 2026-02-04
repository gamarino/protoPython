# csv.py - CSV reader and writer. Minimal dialect: delimiter (default ','), no quoting.

def reader(iterable, delimiter=',', **kwargs):
    """Iterate over lines from iterable; yield list of strings per row (split on delimiter)."""
    for line in iterable:
        if isinstance(line, bytes):
            line = line.decode('utf-8')
        line = line.rstrip('\r\n')
        yield line.split(delimiter)


class writer:
    """CSV writer. writerow(row) and writerows(rows) write comma-separated lines to fileobj."""

    def __init__(self, fileobj, delimiter=',', **kwargs):
        self._f = fileobj
        self._delim = delimiter

    def writerow(self, row):
        """Write one row (sequence of values) as a comma-separated line."""
        line = self._delim.join(str(f) for f in row)
        if hasattr(self._f, 'write'):
            self._f.write(line + '\n')
        return None

    def writerows(self, rows):
        """Write multiple rows."""
        for row in rows:
            self.writerow(row)
        return None
