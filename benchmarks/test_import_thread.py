# Minimal test: import _thread and log. Run: protopy --path lib/python3.14 --script benchmarks/test_import_thread.py 2>&1
def main():
    try:
        import _thread
        _thread.log_thread_ident("after_import")
        return 0
    except Exception as e:
        import sys
        # Try to log via C if we have it later
        return 1
    return 0
