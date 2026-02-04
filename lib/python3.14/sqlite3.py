"""
Minimal sqlite3 stub for protoPython.
connect, Connection; full implementation requires SQLite library.
"""

class Connection:
    """Stub: no-op. execute/close do nothing. Full impl requires SQLite."""

    def execute(self, sql, parameters=()):
        """Stub: return None or empty cursor."""
        return None

    def commit(self):
        pass

    def close(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
        return False

def connect(database, timeout=5.0, **kwargs):
    """Stub: return minimal Connection stub. Full impl requires SQLite."""
    return Connection()
