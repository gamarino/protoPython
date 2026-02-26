
print("Starting repro_io.py - safe delay test")
import builtins

print("Importing abc...")
try:
    import abc
    print("abc imported successfully")
except Exception as e:
    print(f"abc import failed: {e}")

print("Importing io...")
try:
    import io
    print("io imported successfully")
except Exception as e:
    print(f"io import failed: {e}")

print("Assigning builtins.bytearray = list")
builtins.bytearray = list

print(f"bytearray check: {bytearray}")
print(f"bytearray is list? {bytearray is list}")

b = bytearray()
print(f"bytearray() created: {b}, type: {type(b)}")
