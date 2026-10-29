"""Drives `protopy --dap` the way VS Code does and checks the session.

Usage: dap_session.py <protopy> <regression-dir> [session_name...]

Sessions:
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
  * dap_exceptions.py: the "raised" and "userUnhandled" exception filters,
    exceptionInfo, no stop on iteration and other internal control flow, no
    stop with the filters off.
  * dap_threads.py: two threads at one breakpoint (one-thread stops, stepping
    one while the other stays held); dap_threads_busy.py: pausing one busy
    thread while main runs, and several collections while a thread is held.
  * dap_conditions.py: line conditions, hit counts and logpoints, conditions
    on the exception filters (exc bound), failing conditions, a condition
    calling a function with a breakpoint, a condition evaluated per thread.

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

    def stopped(self, reason, line, name=None, thread=1):
        e = self.event("stopped")
        check(e["body"]["reason"] == reason, "stopped for %r, expected %r" % (e["body"]["reason"], reason))
        check(e["body"]["threadId"] == thread, "stopped thread %r, expected %r" % (e["body"]["threadId"], thread))
        frames = self.ok("stackTrace", {"threadId": thread})["stackFrames"]
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

    def wait_output(self, text, timeout=60):
        """Reads until `text` is in the program's stdout (events are kept)."""
        deadline = time.time() + timeout
        while text not in self.output["stdout"]:
            check(time.time() < deadline, "no %r in stdout: %r" % (text, self.output["stdout"][-300:]))
            m = self.read()
            check(m is not None, "adapter closed the stream waiting for %r" % text)
            if m.get("type") == "event" and m.get("event") != "output":
                self.pending.append(m)

    def start(self, program, breakpoints, stop_on_entry=False, exception_filters=None, args=None, env=None,
              filter_options=None, extra_breakpoints=None):
        caps = self.ok("initialize", {"adapterID": "protopy", "pathFormat": "path",
                                      "linesStartAt1": True, "columnsStartAt1": True})
        check(caps.get("supportsConfigurationDoneRequest"), "no configurationDone support")
        self.caps = caps
        self.event("initialized")
        launch = {"program": program, "stopOnEntry": stop_on_entry, "args": args or []}
        if env:
            launch["env"] = env
        self.ok("launch", launch)
        result = self.ok("setBreakpoints", {"source": {"path": program},
                                            "breakpoints": [b if isinstance(b, dict) else {"line": b}
                                                            for b in breakpoints]})
        self.bp_result = result["breakpoints"]
        for path, bps in (extra_breakpoints or {}).items():
            self.ok("setBreakpoints", {"source": {"path": path}, "breakpoints": bps})
        if exception_filters is not None:
            self.ok("setExceptionBreakpoints", {"filters": exception_filters,
                                                "filterOptions": filter_options or []})
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
    # Output events come from the threads that drain the program's stdout
    # pipe and are not ordered with the stopped event (see session_output):
    # wait for the line, then check nothing else was printed.
    s.wait_output("before\n")
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
    # Output events are forwarded from the program's stdout pipe and are not
    # ordered with the stopped event: wait for the last line printed before
    # the breakpoint instead of reading the buffer at the stop.
    s.wait_output("lines read: 0\n")
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
    # Pause once the loop runs (startup can be slow on a loaded machine).
    deadline = time.time() + 60
    while "looping" not in s.output["stdout"]:
        check(time.time() < deadline, "the program did not start")
        m = s.read()
        check(m is not None, "adapter closed the stream")
        if m.get("type") == "event" and m.get("event") != "output":
            s.pending.append(m)
    time.sleep(0.2)
    s.ok("pause", {"threadId": 1})
    e = s.event("stopped")
    check(e["body"]["reason"] == "pause", "stopped for %r" % e["body"]["reason"])
    frames = s.ok("stackTrace", {"threadId": 1})["stackFrames"]
    check(frames[0]["line"] in (3, 4), "paused at line %r" % frames[0]["line"])
    fid = frames[0]["id"]
    check(s.evaluate("i > 0", fid) == "True", "loop variable")
    s.ok("evaluate", {"expression": "i = 10 ** 12", "frameId": fid, "context": "repl"})
    s.ok("continue", {"threadId": 1})
    check(s.finish() == 0, "exit code")
    # Paused before `i += 1` the loop adds one more.
    check(s.output["stdout"] in ("looping\nstopped looping at 1000000000000\n",
                                 "looping\nstopped looping at 1000000000001\n"),
          "stdout %r" % s.output["stdout"])


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


def exception_stop(s, line, name, type_name, mode):
    e = s.event("stopped")
    body = e["body"]
    check(body["reason"] == "exception", "stopped for %r, expected an exception" % body["reason"])
    check(body.get("allThreadsStopped") is False, "allThreadsStopped %r" % body.get("allThreadsStopped"))
    tid = body["threadId"]
    check(body.get("text") == type_name, "stop text %r, expected %r" % (body.get("text"), type_name))
    frames = s.ok("stackTrace", {"threadId": tid})["stackFrames"]
    check(frames[0]["line"] == line and frames[0]["name"] == name,
          "exception stop at %s:%s, expected %s:%s" % (frames[0]["name"], frames[0]["line"], name, line))
    info = s.ok("exceptionInfo", {"threadId": tid})
    check(info["exceptionId"] == type_name and info["breakMode"] == mode, "exceptionInfo %r" % info)
    check(info["details"]["typeName"] == type_name and "Traceback" in info["details"]["stackTrace"],
          "exceptionInfo details %r" % info["details"])
    return body, frames, info


def session_exception_raised():
    # "raised": errors of the runtime itself ({}[k], 1 / 0) and a user raise
    # inside a function (line, stack, locals intact), each handled; the
    # program then ends normally. Iteration (a for loop, a user __next__
    # raising StopIteration, next() with a default), a closed generator and
    # getattr with a default never stop.
    prog = client_path("dap_exceptions.py")
    s = Session()
    s.start(prog, [], exception_filters=["raised"])
    filters = {f["filter"]: f for f in s.caps.get("exceptionBreakpointFilters", [])}
    check(set(filters) == {"raised", "userUnhandled"}, "filters %r" % sorted(filters))
    check(s.caps.get("supportsExceptionInfoRequest"), "no exceptionInfo support")
    # The KeyError contextlib.suppress swallows is raised (by the runtime) all
    # the same; it is the first stop.
    body, frames, info = exception_stop(s, 60, "internal_flow", "KeyError", "always")
    check("native code" in body["description"], "origin in %r" % body["description"])
    s.ok("continue", {"threadId": body["threadId"]})
    body, frames, info = exception_stop(s, 7, "risky", "ValueError", "always")
    # Output events come from the threads draining the program's stdout pipe
    # and are not ordered with the stopped event: wait for the line.
    s.wait_output("internal 23")
    check(body["description"] == "ValueError: too big: 5", "description %r" % body["description"])
    check(info["description"] == "too big: 5", "message %r" % info["description"])
    check([f["name"] for f in frames] == ["risky", "handled", "<module>"], "stack %r" % [f["name"] for f in frames])
    check(frames[1]["line"] == 13, "caller line %r" % frames[1]["line"])
    loc = s.scope(frames[0]["id"], "Locals")
    check(loc.get("n", {}).get("value") == "5", "locals at the raise %r" % loc)
    check(s.evaluate("n * 2", frames[0]["id"]) == "10", "evaluate at the raise")
    s.ok("continue", {"threadId": body["threadId"]})
    body, frames, info = exception_stop(s, 20, "native", "ZeroDivisionError", "always")
    check("native code" in body["description"], "origin in %r" % body["description"])
    s.wait_output("caught too big: 5")
    s.ok("continue", {"threadId": body["threadId"]})
    check(s.finish() == 0, "exit code")
    check(s.output["stdout"].endswith("finished\n"), "stdout %r" % s.output["stdout"])


def session_exception_filters_off():
    prog = client_path("dap_exceptions.py")
    s = Session()
    s.start(prog, [], exception_filters=[], args=["crash"])
    code = s.finish()
    check(not [e for e in s.pending if e.get("event") == "stopped"], "a stop with no filter: %r" % s.pending)
    check(code != 0 and "ValueError: too big: 7" in s.output["stderr"], "uncaught: %r" % s.output["stderr"][-300:])


def session_exception_user_unhandled():
    # "userUnhandled": exceptions a clause handles (a tuple of types,
    # contextlib.suppress) do not stop; one no clause of the user's code
    # matches stops at its raise, the whole stack intact.
    prog = client_path("dap_exceptions.py")
    s = Session()
    s.start(prog, [], exception_filters=["userUnhandled"], args=["crash"])
    body, frames, info = exception_stop(s, 7, "risky", "ValueError", "userUnhandled")
    check([f["name"] for f in frames] == ["risky", "wrong_handler", "<module>"],
          "stack %r" % [f["name"] for f in frames])
    s.wait_output("caught too big: 5")
    check(s.scope(frames[0]["id"], "Locals").get("n", {}).get("value") == "7", "locals")
    s.ok("continue", {"threadId": body["threadId"]})
    code = s.finish()
    check(code != 0 and "ValueError: too big: 7" in s.output["stderr"], "traceback %r" % s.output["stderr"][-300:])
    check("not reached" not in s.output["stdout"], "stdout %r" % s.output["stdout"])


def session_threads():
    # Two threads stop at the same breakpoint: two stops, two thread ids, only
    # the thread that stopped is held. Stepping one leaves the other stopped;
    # a continued thread runs to its end while the other is still held.
    prog = client_path("dap_threads.py")
    s = Session()
    s.start(prog, [5])
    first = s.event("stopped")["body"]
    second = s.event("stopped")["body"]
    for b in (first, second):
        check(b["reason"] == "breakpoint" and b.get("allThreadsStopped") is False, "stop %r" % b)
    a, b = first["threadId"], second["threadId"]
    check(a != b and 1 not in (a, b), "thread ids %r %r" % (a, b))
    names = {t["id"]: t["name"] for t in s.ok("threads")["threads"]}
    check(names.get(1) == "MainThread", "threads %r" % names)
    check(names.get(a, "").startswith("Thread-") and "(worker)" in names.get(a, ""), "thread name %r" % names.get(a))
    ks = {}
    for tid in (a, b):
        frames = s.ok("stackTrace", {"threadId": tid})["stackFrames"]
        check(frames[0]["line"] == 5 and frames[0]["name"] == "worker", "thread %s at %r" % (tid, frames[0]))
        ks[tid] = s.scope(frames[0]["id"], "Locals")["k"]["value"]
    check(sorted(ks.values()) == ["1", "2"], "each thread's own locals: %r" % ks)
    s.ok("next", {"threadId": a})
    e = s.event("stopped")["body"]
    check(e["reason"] == "step" and e["threadId"] == a, "step stop %r" % e)
    frames_a = s.ok("stackTrace", {"threadId": a})["stackFrames"]
    check(frames_a[0]["line"] == 6, "stepped to %r" % frames_a[0]["line"])
    frames_b = s.ok("stackTrace", {"threadId": b})["stackFrames"]
    check(frames_b[0]["line"] == 5, "the other thread moved: %r" % frames_b[0]["line"])
    check(s.evaluate("total", frames_a[0]["id"]) == str(int(ks[a]) * 100), "stepped thread's locals")
    r = s.ok("continue", {"threadId": a})
    check(r.get("allThreadsContinued") is False, "continue response %r" % r)
    s.wait_output("worker %s " % ks[a])
    check("worker %s " % ks[b] not in s.output["stdout"], "the held thread ran: %r" % s.output["stdout"])
    check(s.ok("stackTrace", {"threadId": b})["stackFrames"][0]["line"] == 5, "held thread still at the breakpoint")
    s.ok("continue", {"threadId": b})
    check(s.finish() == 0, "exit code")
    out = s.output["stdout"]
    check("worker 1 103" in out and "worker 2 203" in out and out.endswith("done\n"), "stdout %r" % out)
    exited = [e for e in s.pending if e.get("event") == "thread" and e["body"]["reason"] == "exited"]
    check(len(exited) >= 2, "thread exited events %r" % exited)


def session_pause_thread():
    # Pause one busy thread: the main thread keeps printing meanwhile.
    prog = client_path("dap_threads_busy.py")
    s = Session()
    s.start(prog, [])
    s.wait_output("tick 2\n")
    started = [e for e in s.pending if e.get("event") == "thread" and e["body"]["reason"] == "started"]
    check(started, "no thread started event")
    names = {t["name"]: t["id"] for t in s.ok("threads")["threads"]}
    check("spinner" in names, "threads %r" % names)
    spinner = names["spinner"]
    s.ok("pause", {"threadId": spinner})
    e = s.event("stopped")["body"]
    check(e["reason"] == "pause" and e["threadId"] == spinner, "pause stop %r" % e)
    frames = s.ok("stackTrace", {"threadId": spinner})["stackFrames"]
    check(frames[0]["name"] == "spin" and frames[0]["line"] in (10, 11), "paused at %r" % frames[0])
    ticks = s.output["stdout"].count("tick")
    s.wait_output("tick %d\n" % (ticks + 3))
    check(s.evaluate("n > 0", frames[0]["id"]) == "True", "paused thread's local")
    s.ok("evaluate", {"expression": "flag.append(1)", "frameId": frames[0]["id"], "context": "repl"})
    s.ok("continue", {"threadId": spinner})
    check(s.finish() == 0, "exit code")
    check(s.output["stdout"].endswith("spun True\n"), "stdout %r" % s.output["stdout"][-200:])


def session_gc_thread_stopped():
    # A thread held at a breakpoint while the main thread allocates through
    # several collections: the held thread's locals are intact afterwards.
    prog = client_path("dap_threads_busy.py")
    s = Session()
    s.start(prog, [17], args=["gc"],
            env={"PROTOCORE_HEAP_LIMIT_CELLS": "1000000", "PROTOCORE_HEAP_TRACE": "1"})
    e = s.event("stopped")["body"]
    check(e["reason"] == "breakpoint" and e["threadId"] != 1, "stop %r" % e)
    keeper = e["threadId"]
    before = s.output["stderr"].count("protoCore heap:")
    s.wait_output("churned ", timeout=120)
    cycles = s.output["stderr"].count("protoCore heap:") - before
    check(cycles >= 3, "only %d collections while the thread was held" % cycles)
    frames = s.ok("stackTrace", {"threadId": keeper})["stackFrames"]
    check(frames[0]["name"] == "keep" and frames[0]["line"] == 17, "held at %r" % frames[0])
    loc = s.scope(frames[0]["id"], "Locals")
    check(loc["tag"]["value"] == "'ab'", "tag %r" % loc["tag"])
    mine = s.variables(loc["mine"]["variablesReference"])
    check(len(mine) == 2000 and mine["[1999]"]["value"] == "'ababab'", "mine: %d entries" % len(mine))
    check(s.evaluate("sum(len(x) for x in mine)", frames[0]["id"]) == "12000", "mine's contents")
    s.ok("continue", {"threadId": keeper})
    check(s.finish() == 0, "exit code")
    check("kept 2000" in s.output["stdout"], "stdout %r" % s.output["stdout"])


def collect_stops(s, expr):
    """Continues every stop until the program ends: (reason, thread, value of
    `expr` in the top frame, description) of each stop, and the exit code."""
    stops = []
    while True:
        m = None
        for i, e in enumerate(s.pending):
            if e.get("event") in ("stopped", "exited"):
                m = s.pending.pop(i)
                break
        if m is None:
            m = s.read()
            check(m is not None, "adapter closed the stream")
            if m.get("type") != "event" or m.get("event") not in ("stopped", "exited"):
                if m.get("type") == "event" and m.get("event") != "output":
                    s.pending.append(m)
                continue
        if m["event"] == "exited":
            s.pending.append(m)
            return stops, s.finish()
        b = m["body"]
        frames = s.ok("stackTrace", {"threadId": b["threadId"]})["stackFrames"]
        r = s.request("evaluate", {"expression": expr, "frameId": frames[0]["id"], "context": "watch"})
        stops.append((b["reason"], b["threadId"], r["body"]["result"] if r.get("success") else None,
                      b.get("description", "")))
        s.ok("continue", {"threadId": b["threadId"]})


def session_line_conditions():
    # condition, hitCondition (N, >= N, % N), the condition's errors, an
    # invalid hitCondition, and a logpoint: which iterations of a loop stop.
    prog = client_path("dap_conditions.py")
    cases = [
        ({"condition": "i == 4"}, ["4"]),
        ({"condition": "i > 100"}, []),
        ({"hitCondition": "3"}, ["3"]),
        ({"hitCondition": ">= 5"}, ["5", "6"]),
        ({"hitCondition": "% 2"}, ["2", "4", "6"]),
        ({"condition": "i % 2 == 1", "hitCondition": "2"}, ["3"]),
    ]
    for spec, expected in cases:
        s = Session()
        s.start(prog, [dict(spec, line=12)])
        check(s.caps.get("supportsConditionalBreakpoints") and s.caps.get("supportsHitConditionalBreakpoints")
              and s.caps.get("supportsLogPoints"), "capabilities %r" % s.caps)
        stops, code = collect_stops(s, "i")
        check(code == 0, "exit code")
        check([v for _, _, v, _ in stops] == expected, "%r stopped at i in %r, expected %r" % (spec, stops, expected))

    s = Session()
    s.start(prog, [{"line": 12, "condition": "1 / 0"}])
    stops, code = collect_stops(s, "i")
    check(len(stops) == 6 and "breakpoint condition `1 / 0` failed: ZeroDivisionError" in stops[0][3],
          "condition errors stop and say so: %r" % stops[:1])
    check("failed: ZeroDivisionError" in s.output["stderr"], "condition error output %r" % s.output["stderr"])

    s = Session()
    s.start(prog, [{"line": 12, "hitCondition": "every 2"}])
    check(s.bp_result[0]["verified"] is False and "hit count" in s.bp_result[0].get("message", ""),
          "invalid hitCondition: %r" % s.bp_result)
    stops, code = collect_stops(s, "i")
    check(stops == [] and code == 0, "an invalid hitCondition installs nothing: %r" % stops)

    # A logpoint never stops; {expr} interpolated, {{ }} literal, an error
    # shown in place, a condition filters what is logged.
    s = Session()
    s.start(prog, [{"line": 12, "logMessage": "i={i} {{lit}} t={total} bad={nope}", "condition": "i < 3"}])
    stops, code = collect_stops(s, "i")
    check(stops == [] and code == 0, "a logpoint stopped: %r" % stops)
    lines = [l for l in s.output["console"].splitlines() if l.startswith("i=")]
    check(lines == ["i=1 {lit} t=0 bad=<error: NameError: name 'nope' is not defined>",
                    "i=2 {lit} t=1 bad=<error: NameError: name 'nope' is not defined>"],
          "log lines %r" % lines)
    check("total 21" in s.output["stdout"], "stdout %r" % s.output["stdout"])


def session_exception_conditions():
    # A filter's condition sees the exception as `exc` in the raising frame.
    prog = client_path("dap_conditions.py")
    s = Session()
    s.start(prog, [], exception_filters=[],
            filter_options=[{"filterId": "raised", "condition": "isinstance(exc, KeyError) and kind is KeyError"}])
    filters = {f["filter"]: f for f in s.caps["exceptionBreakpointFilters"]}
    check(s.caps.get("supportsExceptionFilterOptions") and filters["raised"].get("supportsCondition")
          and "exc" in filters["raised"].get("conditionDescription", ""), "capabilities %r" % filters)
    stops, code = collect_stops(s, "kind.__name__")
    check([(r, v) for r, _, v, _ in stops] == [("exception", "'KeyError'")], "stops %r" % stops)
    check(code == 0, "exit code")

    s = Session()
    s.start(prog, [], exception_filters=[], filter_options=[{"filterId": "raised", "condition": "isinstance(exc, OSError)"}])
    stops, code = collect_stops(s, "kind")
    check(stops == [] and code == 0, "a false condition stopped: %r" % stops)

    s = Session()
    s.start(prog, [], exception_filters=[], filter_options=[{"filterId": "raised", "condition": "exc.no_such"}])
    stops, code = collect_stops(s, "kind.__name__")
    check(len(stops) == 2 and all("exception breakpoint condition `exc.no_such` failed: AttributeError" in d
                                  for _, _, _, d in stops), "a failing condition stops: %r" % stops)
    check("failed: AttributeError" in s.output["stderr"], "stderr %r" % s.output["stderr"])

    # The condition calls a function with a breakpoint: no stop there.
    s = Session()
    s.start(prog, [6], exception_filters=[],
            filter_options=[{"filterId": "raised", "condition": "probe(1) or isinstance(exc, KeyError)"}])
    stops, code = collect_stops(s, "kind.__name__")
    check([(r, v) for r, _, v, _ in stops] == [("exception", "'KeyError'")], "stops %r" % stops)


def session_condition_per_thread():
    # One conditional breakpoint, two threads: each evaluates it on its own
    # values, so only the thread with k == 2 stops.
    prog = client_path("dap_conditions.py")
    s = Session()
    s.start(prog, [{"line": 25, "condition": "k == 2"}], args=["threads"])
    stops, code = collect_stops(s, "k")
    check([(r, v) for r, _, v, _ in stops] == [("breakpoint", "2")] and stops[0][1] != 1, "stops %r" % stops)
    check("worker 1 10" in s.output["stdout"] and "worker 2 20" in s.output["stdout"], "stdout %r" % s.output["stdout"])


def session_step_into_function_entry():
    # Step In stops at a function's first statement, never on its docstring
    # or on the comment / blank lines after it: a plain function, a decorated
    # one (through its wrapper), a method, a nested def, json.dumps.
    prog = client_path("dap_entry.py")
    s = Session()
    s.start(prog, [44, 45, 46, 47, 48])
    s.stopped("breakpoint", 44)
    s.ok("stepIn", {"threadId": 1})
    s.stopped("step", 10, "plain")
    s.ok("continue", {"threadId": 1})
    s.stopped("breakpoint", 45)
    s.ok("stepIn", {"threadId": 1})
    s.stopped("step", 17, "deco.<locals>.wrapper")
    s.ok("stepIn", {"threadId": 1})
    s.stopped("step", 25, "decorated")
    s.ok("continue", {"threadId": 1})
    s.stopped("breakpoint", 46)
    s.ok("stepIn", {"threadId": 1})
    s.stopped("step", 32, "K.method")
    s.ok("continue", {"threadId": 1})
    s.stopped("breakpoint", 47)
    s.ok("stepIn", {"threadId": 1})
    s.stopped("step", 37, "outer")
    s.ok("next", {"threadId": 1})
    s.stopped("step", 41, "outer")
    s.ok("stepIn", {"threadId": 1})
    s.stopped("step", 40, "outer.<locals>.inner")
    s.ok("continue", {"threadId": 1})
    s.stopped("breakpoint", 48)
    s.ok("stepIn", {"threadId": 1})
    frames = s.stopped("step", frames_line_of_dumps(), "dumps")
    check(frames[0]["source"]["name"] == "__init__.py", "json.dumps frame %r" % frames[0]["source"])
    s.ok("continue", {"threadId": 1})
    check(s.finish() == 0, "exit code")
    check(s.output["stdout"] == "done [1]\n", "stdout %r" % s.output["stdout"])


def frames_line_of_dumps():
    """The first statement of json.dumps in the standard library protopy uses:
    the line after its docstring's comment (`# cached encoder`)."""
    lib = os.path.join(os.path.dirname(HERE), os.pardir, "lib", "python3.14", "json", "__init__.py")
    with open(os.path.normpath(lib), encoding="utf-8") as f:
        lines = f.read().splitlines()
    start = next(i for i, l in enumerate(lines) if l.startswith("def dumps("))
    for i in range(start, len(lines)):
        if lines[i].strip() == "# cached encoder":
            return i + 2  # 1-based, the line after the comment
    raise Failure("no '# cached encoder' in json.dumps")


FAKE_ROOT = r"C:\Fake\Proj"  # the client's side of the mapping, on every platform


def session_path_mappings():
    # The client names the program's folder C:\Fake\Proj; the adapter runs it
    # from dap_mapped/. Breakpoints and stacks use the client's paths; a file
    # outside the mapping (dap_helper_outside.py, the standard library) and
    # code compiled from a string come back as sourceReferences whose text
    # the adapter serves, and breakpoints on them work.
    real = os.path.join(HERE, "dap_mapped")
    client_main = "c:\\fake\\proj\\main.py"  # drive and case as VS Code may send them
    s = Session()
    s.ok("initialize", {"adapterID": "protopy"})
    s.event("initialized")
    caps_ok = s.request("launch", {"program": client_main, "pathMappings": [{"localRoot": FAKE_ROOT}]})
    check(not caps_ok["success"] and "remoteRoot" in caps_ok.get("message", ""), "bad mapping: %r" % caps_ok)
    s.ok("launch", {"program": client_main, "cwd": FAKE_ROOT,
                    "pathMappings": [{"localRoot": FAKE_ROOT, "remoteRoot": real}]})
    r = s.ok("setBreakpoints", {"source": {"path": client_main}, "breakpoints": [{"line": 9}]})
    check(r["breakpoints"][0]["verified"] and r["breakpoints"][0]["source"]["path"] == client_main,
          "breakpoint by client path %r" % r)
    s.ok("configurationDone")
    frames = s.stopped("breakpoint", 9, "<module>")
    check(frames[0]["source"].get("path", "").lower() == client_main, "client path in the stack %r" % frames[0]["source"])
    loaded = s.ok("loadedSources")["sources"]
    by_name = {src["name"]: src for src in loaded}
    check("main.py" in by_name and by_name["main.py"].get("path", "").lower() == client_main, "main %r" % by_name.get("main.py"))
    helper = by_name.get("dap_helper_outside.py")
    check(helper and "path" not in helper and helper.get("sourceReference", 0) > 0
          and same_path(helper.get("origin", ""), os.path.join(HERE, "dap_helper_outside.py")), "helper %r" % helper)
    gen = by_name.get("<generated>")
    check(gen and gen.get("sourceReference", 0) > 0 and "path" not in gen, "generated %r" % gen)
    lib = [src for src in loaded if src.get("presentationHint") == "deemphasize"]
    check(lib and all("path" not in src and src.get("sourceReference", 0) > 0 for src in lib),
          "standard library modules by reference: %r" % lib[:3])
    events = [e for e in s.pending if e.get("event") == "loadedSource"]
    check({e["body"]["source"]["name"] for e in events} >= {"main.py", "dap_helper_outside.py", "<generated>"},
          "loadedSource events %r" % [e["body"]["source"]["name"] for e in events][:10])
    with open(os.path.join(HERE, "dap_helper_outside.py"), encoding="utf-8", newline="") as f:  # as on disk
        helper_text = f.read()
    got = s.ok("source", {"sourceReference": helper["sourceReference"], "source": helper})
    check(got["content"] == helper_text, "helper text %r" % got["content"][:80])
    got = s.ok("source", {"sourceReference": gen["sourceReference"], "source": gen})
    check(got["content"] == "def gen_f(x):\n    y = x * 3\n    return y\n", "generated text %r" % got["content"])
    got = s.ok("source", {"source": {"path": client_main}})
    check("dap_helper_outside.helper(4)" in got["content"], "source by client path")
    r = s.ok("setBreakpoints", {"source": {"name": helper["name"], "sourceReference": helper["sourceReference"]},
                                "breakpoints": [{"line": 2}]})
    check(r["breakpoints"][0]["verified"] and r["breakpoints"][0]["source"]["sourceReference"] == helper["sourceReference"],
          "breakpoint on a reference %r" % r)
    s.ok("setBreakpoints", {"source": {"name": gen["name"], "sourceReference": gen["sourceReference"]},
                            "breakpoints": [{"line": 2}]})
    s.ok("continue", {"threadId": 1})
    frames = s.stopped("breakpoint", 2, "helper")
    check(frames[0]["source"].get("sourceReference") == helper["sourceReference"] and "path" not in frames[0]["source"],
          "helper frame %r" % frames[0]["source"])
    check(frames[1]["source"].get("path", "").lower() == client_main, "caller frame %r" % frames[1]["source"])
    check(s.evaluate("n", frames[0]["id"]) == "4", "helper locals")
    s.ok("continue", {"threadId": 1})
    frames = s.stopped("breakpoint", 2, "gen_f")
    check(frames[0]["source"].get("sourceReference") == gen["sourceReference"], "generated frame %r" % frames[0]["source"])
    check(s.evaluate("x", frames[0]["id"]) == "5", "generated locals")
    s.ok("continue", {"threadId": 1})
    check(s.finish() == 0, "exit code")
    check(s.output["stdout"] == "helper 10\ngen 15\n", "stdout %r" % s.output["stdout"])


def main():
    failures = 0
    only = sys.argv[3:]
    for test in (session_breakpoints_and_stepping, session_stop_on_entry, session_output, session_pause,
                 session_gc_while_stopped, session_exception_raised, session_exception_filters_off,
                 session_exception_user_unhandled, session_threads, session_pause_thread,
                 session_gc_thread_stopped, session_line_conditions, session_exception_conditions,
                 session_condition_per_thread, session_path_mappings, session_step_into_function_entry):
        if only and test.__name__ not in only:
            continue
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
