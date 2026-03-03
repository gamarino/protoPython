# Dummy _colorize module for ProtoPython
def colorize(source, *args, **kwargs):
    return source

class _DummyTheme:
    def __getattr__(self, name):
        return lambda text, **kw: text

def get_theme(dir_fd=None, tty_file=None):
    theme = _DummyTheme()
    theme.unittest = theme
    return theme

def can_colorize(file=None):
    return False
