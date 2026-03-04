import sys
sys.modules['foo'] = 'BAR'
import foo
print(foo)
