"""Conformance test for the re (regular expressions) module."""
import re

# Test match
m = re.match(r'(\w+)', 'hello world')
assert m is not None
assert m.group(0) == 'hello'
assert m.group(1) == 'hello'
assert m.start() == 0
assert m.end() == 5

# Test match failure
assert re.match(r'\d+', 'abc') is None

# Test search
m = re.search(r'(\d+)', 'abc 42 def')
assert m is not None
assert m.group(1) == '42'

# Test findall — no groups
result = re.findall(r'\d+', '1 and 2 and 3')
assert result == ['1', '2', '3']

# Test findall — one group
result = re.findall(r'(\w+)=(\w+)', 'a=1 b=2')
assert len(result) == 2

# Test sub
result = re.sub(r'\d+', 'X', 'a1b2c3')
assert result == 'aXbXcX'

# Test subn
result, n = re.subn(r'\d', 'X', 'a1b2c3')
assert result == 'aXbXcX'
assert n == 3

# Test split
result = re.split(r'\s+', 'hello world  foo')
assert result == ['hello', 'world', 'foo']

# Test compile + pattern methods
p = re.compile(r'\d+')
assert p.findall('1 2 3') == ['1', '2', '3']
assert p.match('42abc').group() == '42'
assert p.search('abc 99 def').group() == '99'

# Test flags
m = re.match(r'[a-z]+', 'HELLO', re.IGNORECASE)
assert m is not None
assert m.group() == 'HELLO'

# Test fullmatch
assert re.fullmatch(r'\d+', '123') is not None
assert re.fullmatch(r'\d+', '123abc') is None

# Test finditer
matches = list(re.finditer(r'\d+', 'abc 42 def 100'))
assert len(matches) == 2
assert matches[0].group() == '42'
assert matches[1].group() == '100'

# Test flag constants exist
assert re.IGNORECASE == re.I
assert re.MULTILINE == re.M
assert re.DOTALL == re.S
assert re.VERBOSE == re.X
assert re.ASCII == re.A
assert re.UNICODE == re.U

# Test escape
escaped = re.escape('hello.world+foo?')
assert '\\.' in escaped
assert '\\+' in escaped

# Test compile with flags
p2 = re.compile(r'[a-z]+', re.IGNORECASE)
assert p2.match('HELLO') is not None

print("test_re passed")
