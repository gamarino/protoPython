# Minimal: write and exit (use cwd to avoid /tmp issues)
import os
path = os.path.join(os.getcwd(), "benchmarks", "reports", "diag_minimal.txt")
try:
    os.makedirs(os.path.dirname(path), exist_ok=True)
except Exception:
    path = "diag_minimal.txt"
with open(path, "w") as f:
    f.write("ok\n")
