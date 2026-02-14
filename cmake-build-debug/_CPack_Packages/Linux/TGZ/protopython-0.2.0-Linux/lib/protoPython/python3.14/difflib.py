# difflib.py - SequenceMatcher with ratio and get_matching_blocks.

class SequenceMatcher:
    """Matches sequences; ratio() and get_matching_blocks() implemented."""

    def __init__(self, isjunk=None, a='', b=''):
        self.isjunk = isjunk
        self.a = a if hasattr(a, '__getitem__') else list(a)
        self.b = b if hasattr(b, '__getitem__') else list(b)

    def get_matching_blocks(self):
        """Return list of (i, j, n) where a[i:i+n] == b[j:j+n], non-overlapping."""
        a, b = self.a, self.b
        len_a, len_b = len(a), len(b)
        used_a = [False] * len_a
        used_b = [False] * len_b
        blocks = []
        for i in range(len_a):
            if used_a[i]:
                continue
            for j in range(len_b):
                if used_b[j]:
                    continue
                if a[i] == b[j]:
                    n = 0
                    while i + n < len_a and j + n < len_b and a[i + n] == b[j + n]:
                        used_a[i + n] = True
                        used_b[j + n] = True
                        n += 1
                    blocks.append((i, j, n))
                    break
        blocks.append((len_a, len_b, 0))
        return blocks

    def ratio(self):
        """Return 2.0 * matches / (len(a) + len(b))."""
        blocks = self.get_matching_blocks()
        matches = sum(n for (_, _, n) in blocks)
        total = len(self.a) + len(self.b)
        if total == 0:
            return 1.0
        return 2.0 * matches / total


def unified_diff(a, b, fromfile='', tofile='', fromfiledate='', tofiledate='', n=3, lineterm='\n'):
    """Return unified diff lines between sequences a and b (line-by-line)."""
    if hasattr(a, 'splitlines'):
        a = a.splitlines()
    elif not isinstance(a, list):
        a = list(a)
    if hasattr(b, 'splitlines'):
        b = b.splitlines()
    elif not isinstance(b, list):
        b = list(b)
    sm = SequenceMatcher(None, a, b)
    blocks = sm.get_matching_blocks()
    lines = []
    if fromfile or tofile:
        lines.append("--- %s%s" % (fromfile, " " + fromfiledate if fromfiledate else "") + lineterm)
        lines.append("+++ %s%s" % (tofile, " " + tofiledate if tofiledate else "") + lineterm)
    for k in range(len(blocks) - 1):
        i1, j1, n1 = blocks[k]
        i2, j2, n2 = blocks[k + 1]
        for ii in range(i1 + n1, i2):
            lines.append("- " + str(a[ii]) + lineterm)
        for jj in range(j1 + n1, j2):
            lines.append("+ " + str(b[jj]) + lineterm)
    return lines
