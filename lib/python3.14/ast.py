"""
Minimal ast stub for protoPython.
parse, dump, literal_eval; full implementation requires parser and AST construction.
"""

def parse(source, filename='<unknown>', mode='exec', *, type_comments=False, feature_version=None):
    """Stub: return None. Full impl requires parser and AST construction."""
    return None

def dump(node, annotate_fields=True, include_attributes=False, *, indent=None):
    """Stub: return empty string. Full impl requires AST serialization."""
    return ''

def literal_eval(node_or_string):
    """Stub: return None. Full impl requires safe evaluation of literals."""
    return None

def walk(node):
    """Stub: yield nothing. Full impl requires AST traversal."""
    return
    yield  # make this a generator

class NodeVisitor:
    """Stub class for AST visitor. Full impl requires visit/generic_visit dispatch."""
    def visit(self, node):
        """Stub: no-op."""
        pass
    def generic_visit(self, node):
        """Stub: no-op."""
        pass
