# A fork child runs Python code and exits.  protoCore's collector thread is
# not in the child: an allocation past the heap's soft limit waited for a
# collection that never came, and the exit joined the missing collector, so
# the child hung even when it did nothing (test_os ForkTests.test_fork).
import os
import sys

if not hasattr(os, "fork"):
    print("fork_child_runs: ok")
    sys.exit(0)

events_r, events_w = os.pipe()
os.register_at_fork(before=lambda: os.write(events_w, b"B"),
                    after_in_parent=lambda: os.write(events_w, b"P"),
                    after_in_child=lambda: os.write(events_w, b"C"))
for bad in ({}, {"before": 1}):
    try:
        os.register_at_fork(**bad)
    except TypeError:
        pass
    else:
        raise AssertionError("register_at_fork accepted %r" % (bad,))

# The child allocates a lot and exits through the normal interpreter exit.
pid = os.fork()
if pid == 0:
    data = [str(i) * 10 for i in range(300_000)]
    d = {i: [i] for i in range(100_000)}
    assert len(data) == 300_000 and len(d) == 100_000
    sys.exit(7)
_, status = os.waitpid(pid, 0)
assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 7, status

# A child that runs off the end of the script exits 0.
pid = os.fork()
if pid == 0:
    pass
else:
    _, status = os.waitpid(pid, 0)
    assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, status
    os.close(events_w)
    seen = b""
    while chunk := os.read(events_r, 64):
        seen += chunk
    # Each fork: before, then after_in_parent (parent) and after_in_child.
    assert sorted(seen) == sorted(b"BPCBPC"), seen
    print("fork_child_runs: ok")
