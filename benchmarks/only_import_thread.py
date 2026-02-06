# Only import _thread and call main (so we see if initialize runs).
def main():
    import _thread
    _thread.log_thread_ident("after_import")
