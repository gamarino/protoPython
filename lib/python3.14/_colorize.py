# Dummy _colorize module for ProtoPython
def colorize(source, *args, **kwargs):
    return source

class _DummyTheme:
    def __init__(self):
        self.argparse = self
        self.heading = ""
        self.reset = ""
        self.prog = ""
        self.prog_extra = ""
        self.usage = ""
        self.action = ""
        self.summary_action = ""
        self.summary_long_option = ""
        self.summary_short_option = ""
        self.summary_label = ""
        self.long_option = ""
        self.short_option = ""
        self.label = ""

    def __getattr__(self, name):
        return ""

def get_theme(**kwargs):
    return _DummyTheme()

def can_colorize(file=None):
    return False

def decolor(source, *args, **kwargs):
    return source
