try:
    import ssl
    print("SSL imported successfully!")
except Exception as e:
    print(f"FAILED to import ssl: {type(e).__name__}: {e}")
    # Don't use traceback yet, it seems broken
