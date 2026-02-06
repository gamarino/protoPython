# Minimal: write to cwd so we see if script runs and open works.
try:
    f = open("check_open_result.txt", "w")
    f.write("ok")
    f.close()
except Exception as e:
    f = open("check_open_err.txt", "w")
    f.write(str(e))
    f.close()
