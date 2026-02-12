import sys
def my_hook(etype, evalue, etb):
    print(f"HOOK CAUGHT: {etype.__name__}")

sys.excepthook = my_hook
print("Raising error...")
1 / 0
