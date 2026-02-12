
import subprocess
import time

def test_repl_multiline():
    process = subprocess.Popen(['./src/runtime/protopy', '-i'], 
                               stdin=subprocess.PIPE, 
                               stdout=subprocess.PIPE, 
                               stderr=subprocess.PIPE,
                               text=True,
                               cwd='build')

    # Try to define a function
    commands = [
        "def foo():",
        "print('line 1')", # REPL will auto-indent this
        "print('line 2')", # REPL will stay at same indent
        "",                # One newline closes the indent of the last line? No, it needs an empty line.
        "",                # Empty line finishes the block
        "foo()",
        "exit()"
    ]

    for cmd in commands:
        process.stdin.write(cmd + "\n")
        process.stdin.flush()
        time.sleep(0.1)

    stdout, stderr = process.communicate(timeout=2)
    print("STDOUT:")
    print(stdout)
    print("STDERR:")
    print(stderr)

if __name__ == "__main__":
    test_repl_multiline()
