import argparse

parser = argparse.ArgumentParser()
parser.add_argument('infile', nargs='?', default='-')
args = parser.parse_args(['-'])

print(f"args is argparse.Namespace: {args is argparse.Namespace}")
print(f"args.__class__.__name__: {getattr(args, '__class__').__name__}")
print(f"args id: {id(args)}, Namespace id: {id(argparse.Namespace)}")
