print(f"DEBUG: module starting with __name__='{__name__}'")
if __name__ == '__main__':
    print("DEBUG: Entered __main__ block! (FAIL)")
else:
    print("DEBUG: Skipped __main__ block. (PASS)")
