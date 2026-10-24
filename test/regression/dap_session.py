"""Drives `protopy --dap` the way VS Code does and checks the session.

Usage: dap_session.py <protopy> <regression-dir>

Three sessions:
  * dap_target.py: breakpoints (one on a blank line, moved to the next line
    with code), stepIn / next / stepOut, the call stack, Locals and Globals,
    expanding a dict and a list, evaluate in the selected frame, a statement in
    the Debug Console, the exit code.
  * dap_target.py with stopOnEntry.
  * dap_loop.py: pause a running loop, end it with an assignment typed in
    the Debug Console.
  * dap_gc.py: a thread allocates and collects while the main thread is held
    (and evaluates); launch `env`.
  * dap_output.py: what the script prints before and after a breakpoint, more
    than a pipe buffer of non-ASCII text, reading stdin (end of file, not the
    client's requests), an uncaught exception (its traceback on stderr, a
    failing exit code).

Paths are given as VS Code gives them on Windows (lower-case drive letter).
Every byte the adapter writes must belong to a Content-Length frame. A
watchdog kills a hung adapter, so a broken session fails instead of hanging.
"""

import json
import os
import subprocess
import sys
import threading
import time

PROTOPY = sys.argv[1]
HERE = os.path.abspath(sys.argv[2])


def client_path(name):
    p = os.path.join(HERE, name)
    if os.name == "nt" and len(p) > 1 and p[1] == ":":
        p = p[0].lower() + p[1:]
    return p


def same_path(a, b):
    na = os.path.normcase(os.path.normpath(a))
    nb = os.path.normcase(os.path.normpath(b))
    return na == nb


SESSIONS = []


class Failure(Exception):
    pass


def check(cond, what):
    if not cond:
        raise Failure(what)


class Session:
    def __init__(self):
        self.p = subprocess.Popen([PROTOPY, "--dap"], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        SESSIONS.append(self)
        self.watchdog = threading.Timer(120, self.p.kill)
        self.watchdog.start()
        self.seq = 0
        self.pending = []      # events read while waiting for a response
        self.output = {"stdout": "", "stderr": ""}
        self.adapter_stderr = b""
        threading.Thread(target=self._drain_stderr, daemon=True).start()

    def _drain_stderr(self):
        self.adapter_stderr = self.p.stderr.read()

    def send(self, command, arguments=None):
        self.seq += 1
        msg = {"seq": self.seq, "type": "request", "command": command,
               "arguments": arguments or {}}
        body = json.dumps(msg).encode("utf-8")
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.p.stdin.flush()
        return self.seq

    def read(self):
        header = b""
        while not header.endswith(b"\r\n\r\n"):
            c = self.p.stdout.read(1)
            if not c:
                return None
            header += c
            if len(header) > 200:
                break
        check(header.startswith(b"Content-Length: "), "unframed bytes from the adapter: %r" % header[:60])
        n = int(header[len(b"Content-Length: "):].split(b"\r\n")[0])
        body = b""
        while len(body) < n:
            chunk = self.p.stdout.read(n - len(body))
            if not chunk:
                return None
            body += chunk
        msg = json.loads(body.decode("utf-8"))
        if msg.get("type") == "event" and msg.get("event") == "output":
            cat = msg["body"].get("category", "console")
            self.output[cat] = self.output.get(cat, "") + msg["body"]["output"]
        return msg

    def request(self, command, arguments=None):
        seq = self.send(command, arguments)
        while True:
            m = self.read()
            check(m is not None, "adapter closed the stream waiting for '%s'" % command)
            if m.get("type") == "response" and m.get("request_seq") == seq:
                return m
            if m.get("type") == "event" and m.get("event") != "output":
                self.pending.append(m)

    def ok(self, command, arguments=None):
        r = self.request(command, arguments)
        check(r.get("success"), "%s failed: %s" % (command, r.get("message")))
        return r.get("body", {})

    def event(self, name):
        while True:
            for i, e in enumerate(self.pending):
                if e.get("event") == name:
                    return self.pending.pop(i)
            m = self.read()
            check(m is not None, "adapter closed the stream waiting for event '%s'" % name)
            if m.get("type") == "event" and m.get("event") != "output":
                self.pending.append(m)

    def stopped(self, reason, line, name=None):
        e = self.event("stopped")
        check(e["body"]["reason"] == reason, "stopped for %r, expected %r" % (e["body"]["reason"], reason))
        frames = self.ok("stackTrace", {"threadId": 1})["stackFrames"]
        check(frames, "empty call stack")
        top = frames[0]
        check(top["line"] == line, "stopped at line %s, expected %s (%s)" % (top["line"], line, reason))
        if name:
            check(top["name"] == name, "top frame %r, expected %r" % (top["name"], name))
        return frames

    def variables(self, ref):
        return {v["name"]: v for v in self.ok("variables", {"variablesReference": ref})["variables"]}

    def scope(self, frame_id, scope_name):
        for s in self.ok("scopes", {"frameId": frame_id})["scopes"]:
            if s["name"] == scope_name:
                return self.variables(s["variablesReference"])
        raise Failure("no %s scope" % scope_name)

    def evaluate(self, expr, frame_id, context="watch"):
        return self.ok("evaluate", {"expression": expr, "frameId": frame_id, "context": context})["result"]

    def finish(self):
        exited = self.event("exited")
        self.event("terminated")
        self.ok("disconnect", {})
        try:
            rc = self.p.wait(timeout=20)
        except subprocess.TimeoutExpired:
            self.p.kill()
            raise Failure("adapter did not exit after disconnect")
        finally:
            self.watchdog.cancel()
        code = exited["body"]["exitCode"]
        check(rc == code, "adapter exit status %s, program exit code %s" % (rc, code))
        return code

    def start(self, program, breakpoints, stop_on_entry=False):
        caps = self.ok("initialize", {"adapterID": "protopy", "pathFormat": "path",
                                      "linesStartAt1": True, "columnsStartAt1": True})
        check(caps.get("supportsConfigurationDoneRequest"), "no configurationDone support")
        self.event("initialized")
        self.ok("launch", {"program": program, "stopOnEntry": stop_on_entry})
        result = self.ok("setBreakpoints", {"source": {"path": program},
                                            "breakpoints": [{"line": n} for n in breakpoints]})
        self.ok("configurationDone")
        return [b["line"] for b in result["breakpoints"]]


def session_breakpoints_and_stepping():
    prog = client_path("dap_target.py")
    s = Session()
    lines = s.start(prog, [5, 17, 7])
    check(lines == [5, 17, 9], "breakpoint lines %r (7 is blank: expected it moved to 9)" % lines)

    frames = s.stopped("breakpoint", 9, "<module>")
    check(same_path(frames[0]["source"]["path"], prog), "source path %r" % frames[0]["source"]["path"])
    s.ok("continue", {"threadId": 1})

    frames = s.stopped("breakpoint", 17, "<module>")
    check(s.output["stdout"] == "before\n", "output before the stop: %r" % s.output["stdout"])
    fid = frames[0]["id"]
    loc = s.scope(fid, "Locals")
    check("data" in loc and "x" not in loc, "module locals %r" % sorted(loc))
    check(loc["data"]["variablesReference"] > 0, "dict not expandable")
    data = s.variables(loc["data"]["variablesReference"])
    check("'k'" in data and data["'k'"]["value"] == "[1, 2, 3]", "dict children %r" % data)
    items = s.variables(data["'k'"]["variablesReference"])
    check([items[k]["value"] for k in ("[0]", "[1]", "[2]")] == ["1", "2", "3"], "list children %r" % items)
    check(s.evaluate("data['k'][1] + 40", fid) == "42", "evaluate at module level")
    r = s.request("evaluate", {"expression": "undefined_name", "frameId": fid, "context": "watch"})
    check(not r["success"] and "NameError" in r.get("message", ""), "evaluate error: %r" % r.get("message"))

    s.ok("stepIn", {"threadId": 1})
    frames = s.stopped("step", 10, "outer")
    check([f["name"] for f in frames] == ["outer", "<module>"], "stack %r" % [f["name"] for f in frames])
    check(frames[1]["line"] == 17, "caller line %r" % frames[1]["line"])
    s.ok("next", {"threadId": 1})
    s.stopped("step", 11, "outer")
    # Stepping over add() stops at the breakpoint inside it.
    s.ok("next", {"threadId": 1})
    frames = s.stopped("breakpoint", 5, "add")
    fid = frames[0]["id"]
    loc = s.scope(fid, "Locals")
    check(loc.get("a", {}).get("value") == "5" and loc.get("b", {}).get("value") == "1", "add locals %r" % loc)
    check(s.evaluate("a * 10 + b", fid) == "51", "evaluate in a function frame")
    # The caller's frame, selected in the call stack.
    check(s.evaluate("n", frames[1]["id"]) == "5", "evaluate in the caller's frame")
    glob = s.scope(fid, "Globals")
    check("data" in glob and "outer" in glob, "globals %r" % sorted(glob))

    s.ok("stepOut", {"threadId": 1})
    s.stopped("step", 11, "outer")
    s.ok("next", {"threadId": 1})
    frames = s.stopped("step", 12, "outer")
    fid = frames[0]["id"]
    check(s.evaluate("r", fid) == "6", "r after the call")
    check(s.scope(fid, "Locals")["items"]["value"] == "[5, 10]", "items")
    # A statement in the Debug Console (module frame: writes the global).
    s.ok("evaluate", {"expression": "print('from console')", "frameId": frames[1]["id"], "context": "repl"})

    s.ok("continue", {"threadId": 1})
    code = s.finish()
    check(code == 0, "exit code %r" % code)
    check(s.output["stdout"] == "before\nfrom console\nafter 8\n", "stdout %r" % s.output["stdout"])


def session_stop_on_entry():
    prog = client_path("dap_target.py")
    s = Session()
    s.start(prog, [], stop_on_entry=True)
    s.stopped("entry", 1, "<module>")
    s.ok("next", {"threadId": 1})
    s.stopped("step", 4, "<module>")
    s.ok("continue", {"threadId": 1})
    check(s.finish() == 0, "exit code")


def session_output():
    prog = client_path("dap_output.py")
    s = Session()
    s.start(prog, [10])
    s.stopped("breakpoint", 10)
    out = s.output["stdout"].splitlines()
    check(out[:1] == ["before"], "first line %r" % out[:1])
    check(out.count("ñandú línea") == 5000, "non-ASCII lines: %d" % out.count("ñandú línea"))
    check(out[-1:] == ["lines read: 0"], "stdin read %r" % out[-1:])
    s.ok("continue", {"threadId": 1})
    code = s.finish()
    check(code != 0, "an uncaught exception must fail the exit code")
    check(s.output["stdout"].splitlines()[-1] == "after", "last stdout line")
    check("ValueError: boom" in s.output["stderr"], "traceback on stderr: %r" % s.output["stderr"][-300:])


def session_pause():
    # Pause a busy loop, then end it from the Debug Console.
    prog = client_path("dap_loop.py")
    s = Session()
    s.start(prog, [])
    time.sleep(1)  # let the loop run
    s.ok("pause", {"threadId": 1})
    e = s.event("stopped")
    check(e["body"]["reason"] == "pause", "stopped for %r" % e["body"]["reason"])
    frames = s.ok("stackTrace", {"threadId": 1})["stackFrames"]
    check(frames[0]["line"] in (2, 3), "paused at line %r" % frames[0]["line"])
    fid = frames[0]["id"]
    check(s.evaluate("i > 0", fid) == "True", "loop variable")
    s.ok("evaluate", {"expression": "i = 10 ** 12", "frameId": fid, "context": "repl"})
    s.ok("continue", {"threadId": 1})
    check(s.finish() == 0, "exit code")
    check(s.output["stdout"] == "stopped looping at 1000000000000\n", "stdout %r" % s.output["stdout"])


def session_gc_while_stopped():
    # A thread allocating (and collecting) while the main thread is held at a
    # breakpoint and answers inspection requests: the held thread must not stall
    # the collector's stop-the-world, or the worker never finishes.
    prog = client_path("dap_gc.py")
    s = Session()
    s.ok("initialize", {"adapterID": "protopy"})
    s.event("initialized")
    s.ok("launch", {"program": prog, "env": {"PROTOCORE_HEAP_LIMIT_CELLS": "1000000"}})
    s.ok("setBreakpoints", {"source": {"path": prog}, "breakpoints": [{"line": 17}]})
    s.ok("configurationDone")
    frames = s.stopped("breakpoint", 17)
    fid = frames[0]["id"]
    deadline = time.time() + 90
    while s.evaluate("len(done)", fid) != "1":
        check(time.time() < deadline, "the worker thread did not finish while main was held")
        s.scope(fid, "Locals")
        time.sleep(0.05)
    s.ok("continue", {"threadId": 1})
    check(s.finish() == 0, "exit code")
    check(s.output["stdout"].startswith("churned ") and s.output["stdout"].endswith(" 1000000\n"),
          "stdout %r" % s.output["stdout"])


def main():
    failures = 0
    for test in (session_breakpoints_and_stepping, session_stop_on_entry, session_output, session_pause,
                 session_gc_while_stopped):
        try:
            test()
            print("PASS", test.__name__)
        except Failure as e:
            failures += 1
            print("FAIL", test.__name__, "-", e)
        except Exception as e:  # a protocol-level surprise
            failures += 1
            print("FAIL", test.__name__, "- %s: %s" % (type(e).__name__, e))
        for leftover in SESSIONS:
            if leftover.p.poll() is None:
                leftover.p.kill()
            leftover.watchdog.cancel()
        del SESSIONS[:]
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
