# SystemExit without a code exits with status 0 (expected: 0).
print("before exit")
raise SystemExit
