"""Conformance test for the dataclasses module."""
import dataclasses

# Basic dataclass creation
@dataclasses.dataclass
class Point:
    x: float
    y: float

p = Point(1.0, 2.0)
assert p.x == 1.0
assert p.y == 2.0

# Default field values
@dataclasses.dataclass
class Config:
    host: str = 'localhost'
    port: int = 8080

c = Config()
assert c.host == 'localhost'
assert c.port == 8080

c2 = Config('example.com', 443)
assert c2.host == 'example.com'
assert c2.port == 443

# field() with default_factory — each instance gets its own list
@dataclasses.dataclass
class Container:
    items: list = dataclasses.field(default_factory=list)

a = Container()
b = Container()
a.items.append(42)
assert a.items == [42]
assert b.items == []

# fields() returns the field descriptors
flds = dataclasses.fields(Point)
assert len(flds) == 2
assert flds[0].name == 'x'
assert flds[1].name == 'y'
assert flds[0].type is not None

# is_dataclass() — class and instance both return True
assert dataclasses.is_dataclass(Point)
assert dataclasses.is_dataclass(p)
assert not dataclasses.is_dataclass(int)
assert not dataclasses.is_dataclass(42)

# asdict()
d = dataclasses.asdict(p)
assert d['x'] == 1.0
assert d['y'] == 2.0

# astuple()
t = dataclasses.astuple(p)
assert t[0] == 1.0
assert t[1] == 2.0

# Mutation works (dataclasses are mutable by default)
p.x = 99.0
assert p.x == 99.0

# Three-field dataclass
@dataclasses.dataclass
class Color:
    r: int
    g: int
    b: int
    alpha: float = 1.0

col = Color(255, 128, 0)
assert col.r == 255
assert col.g == 128
assert col.b == 0
assert col.alpha == 1.0

col2 = Color(0, 0, 0, 0.5)
assert col2.alpha == 0.5

# MISSING sentinel exists
assert dataclasses.MISSING is not None

# Module has expected public names
for name in ['dataclass', 'field', 'Field', 'fields', 'asdict', 'astuple',
             'is_dataclass', 'MISSING', 'KW_ONLY', 'InitVar',
             'make_dataclass', 'replace']:
    assert hasattr(dataclasses, name), f"missing: {name}"

print("test_dataclasses passed")
