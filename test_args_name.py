import argparse

parser = argparse.ArgumentParser()
parser.add_argument('infile', nargs='?', default='-')
args = parser.parse_args(['-'])

print(f"args name: {getattr(args, '__name__', 'NO NAME')}")
print(f"args basis: {getattr(args, '__bases__', 'NO BASES')}")
