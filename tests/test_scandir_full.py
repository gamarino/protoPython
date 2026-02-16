import os

print("Testing os.scandir('.')")
try:
    with os.scandir(".") as it:
        print(f"Iterator: {it}")
        for entry in it:
            print(f"Entry: {entry.name}, Path: {entry.path}")
            print(f"  is_dir: {entry.is_dir()}, is_file: {entry.is_file()}, is_symlink: {entry.is_symlink()}")
            print(f"  inode: {entry.inode()}")
            st = entry.stat()
            print(f"  stat result: mode={st.st_mode}, size={st.st_size}")
            print(f"  stat indexed mode: {st[0]}")
            assert st[0] == st.st_mode
            
    print("Testing os.stat('.')")
    st = os.stat(".")
    print(f"os.stat('.') result: mode={st.st_mode}, size={st.st_size}")
    print(f"os.stat('.') indexed mode: {st[0]}")
    assert st[0] == st.st_mode

    print(f"Identity: uid={os.getuid()}, euid={os.geteuid()}, gid={os.getgid()}, egid={os.getegid()}")
except Exception as e:
    print(f"Error during scandir: {e}")

print("Verification complete.")
