# Dummy _tokenize module for ProtoPython
print("DEBUG: _tokenize.py top")
import token
print("DEBUG: _tokenize.py imported token")
import builtins

class TokenizerIter:
    def __init__(self, source, extra_tokens=False, encoding=None):
        # source might be a callable (readline) or a string
        if isinstance(source, str):
            self.tokens = builtins._tokenize_source(source)
        elif callable(source):
            # Read all from callable
            all_content = ""
            while True:
                line = source()
                if not line:
                    break
                if isinstance(line, bytes):
                    line = line.decode(encoding or 'utf-8')
                all_content += line
            self.tokens = builtins._tokenize_source(all_content)
        else:
            self.tokens = []
        self.index = 0

    def __iter__(self):
        return self

    def __next__(self):
        if self.index >= len(self.tokens):
            raise StopIteration
        t_type, t_val = self.tokens[self.index]
        self.index += 1
        # TokenInfo(type, string, start, end, line)
        # We dummy the positions (1,0) to (1, len)
        return (t_type, t_val, (1, 0), (1, len(t_val)), "")
