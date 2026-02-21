
print(f"print: {print}")
print(f"str: {str}")
print(f"int: {int}")
print(f"tuple: {tuple}")
print(f"list: {list}")

print(f"eval('1') = {eval('1')}")
print(f"eval('1+1') = {eval('1+1')}")
x = 10
print(f"eval('x') = {eval('x')}")
print(f"eval('x+5') = {eval('x+5')}")
def f(): return 42
print(f"eval('f()') = {eval('f()')}")
import collections
print(f"collections is {collections}")
try:
    nt = collections.namedtuple('Point', ['x', 'y'])
    print(f"nt is {nt}")
except Exception as e:
    print(f"Error in namedtuple: {e}")
