try:
    __import__("annotationlib")
except Exception as e:
    import builtins
    builtins.print("Exception type:", type(e))
    builtins.print("Exception args:", e.args)
