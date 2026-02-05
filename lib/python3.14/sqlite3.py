"""
sqlite3 stub for protoPython.
Full implementation requires the SQLite C library.
connect returns a Connection stub; execute/commit/close are no-ops for import compatibility.
"""

class Connection:
    """Stub: execute returns None; commit/close no-op. Full impl requires SQLite."""

    def execute(self, sql, parameters=()):
        """Stub: return None. Full impl requires SQLite."""
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
    """Return Connection stub. Full impl requires native SQLite."""
    return Connection()
