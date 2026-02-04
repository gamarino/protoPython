"""
ConfigParser: INI-style config file parsing.
"""

class ConfigParser:
    """Parse INI-style files: [section], key = value, # and ; comments."""

    def __init__(self):
        self._sections = {}

    def read(self, filenames, encoding=None):
        """Read and parse filenames (str or list). Returns list of successfully read files."""
        if isinstance(filenames, str):
            filenames = [filenames]
        read_ok = []
        for fn in filenames:
            try:
                with open(fn, 'r', encoding=encoding or 'utf-8') as f:
                    self._parse(f.read())
                read_ok.append(fn)
            except (OSError, IOError):
                pass
        return read_ok

    def _parse(self, content):
        """Parse INI content into _sections."""
        current = None
        for line in content.splitlines():
            line = line.strip()
            if not line or line.startswith('#') or line.startswith(';'):
                continue
            if line.startswith('[') and line.endswith(']'):
                current = line[1:-1].strip()
                if current not in self._sections:
                    self._sections[current] = {}
                continue
            if '=' in line and current is not None:
                idx = line.index('=')
                key = line[:idx].strip()
                val = line[idx+1:].strip()
                if val.startswith('"') and val.endswith('"'):
                    val = val[1:-1].replace('\\"', '"')
                elif val.startswith("'") and val.endswith("'"):
                    val = val[1:-1].replace("\\'", "'")
                self._sections[current][key] = val

    def sections(self):
        """Return list of section names."""
        return list(self._sections.keys())

    def get(self, section, option, *, raw=False, vars=None, fallback=None):
        """Return option value; fallback if missing."""
        if section not in self._sections:
            if fallback is not None:
                return fallback
            raise KeyError(section)
        opts = self._sections[section]
        if vars and option in vars:
            return str(vars[option])
        if option in opts:
            return opts[option]
        if fallback is not None:
            return fallback
        raise KeyError(option)

    def getint(self, section, option, *, raw=False, vars=None, fallback=None):
        """Return option as int."""
        val = self.get(section, option, raw=raw, vars=vars, fallback=fallback)
        if val is None:
            return fallback
        return int(val)

    def getfloat(self, section, option, *, raw=False, vars=None, fallback=None):
        """Return option as float."""
        val = self.get(section, option, raw=raw, vars=vars, fallback=fallback)
        if val is None:
            return fallback
        return float(val)

    def getboolean(self, section, option, *, raw=False, vars=None, fallback=None):
        """Return option as bool. Accepts 1/0, yes/no, true/false, on/off."""
        val = self.get(section, option, raw=raw, vars=vars, fallback=fallback)
        if val is None:
            return fallback
        v = str(val).lower()
        if v in ('1', 'yes', 'true', 'on'):
            return True
        if v in ('0', 'no', 'false', 'off'):
            return False
        raise ValueError("Not a boolean: %s" % val)
