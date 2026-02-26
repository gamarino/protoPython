try:
    print("Importing codeop...")
    import codeop
    print("codeop imported successfully")
    
    # Try to use something from it to ensure it's fully loaded and functional
    source = "print('Hello from compiled code')"
    code = codeop.compile_command(source)
    if code:
        exec(code)
except Exception as e:
    print(f"Error during codeop test: {e}")
    import traceback
    traceback.print_exc()
