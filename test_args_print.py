import argparse

print("Creating parser")
parser = argparse.ArgumentParser()
print(f"parser type: {type(parser)}")
print(f"parser.__class__: {parser.__class__}")
print(f"parser type name: {type(parser).__name__}")
print(f"ArgumentParser.parse_args type: {type(argparse.ArgumentParser.parse_args)}")
print(f"ArgumentParser.parse_args repr: {repr(argparse.ArgumentParser.parse_args)}")
print(f"parser.parse_args type: {type(parser.parse_args)}")
print(f"parser.parse_args repr: {repr(parser.parse_args)}")

try:
    print(f"parser.parse_args.__self__: repr={repr(parser.parse_args.__self__)}")
except Exception as e:
    print(f"Failed to get __self__: {e}")

try:
    print(f"parser.parse_args.__func__: repr={repr(parser.parse_args.__func__)}")
except Exception as e:
    print(f"Failed to get __func__: {e}")

try:
    print("Calling parse_args")
    args = parser.parse_args(['-'])
    print(f"Call finished, args={args}")
except Exception as e:
    print(f"Exception calling parse_args: {e}")
