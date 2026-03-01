import sys

def check_import(module_name):
    print(f"Trying to import {module_name}...")
    sys.stdout.flush()
    try:
        __import__(module_name)
        print(f"Successfully imported {module_name}!")
    except Exception as e:
        print(f"Failed to import {module_name}: {e}")
    sys.stdout.flush()

# Inverse from test_base64: array, os, binascii, base64, unittest
modules = [
    "array",
    "os",
    "binascii",
    "base64",
    "unittest"
]

for mod in modules:
    check_import(mod)

print("All imports tested!")
