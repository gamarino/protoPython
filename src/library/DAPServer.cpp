/**
 * `protopy --dap`: a Debug Adapter Protocol server (include/protoPython/DAPServer.h).
 *
 * Threads:
 *  - The calling (main) thread waits for `launch` + `configurationDone`, then
 *    runs the program through the runner, with line events enabled
 *    (include/protoPython/Debugger.h). The Python threads the program starts
 *    report events too (debugger::ThreadScope), each with its own ThreadState.
 *    When a line or exception event stops a thread (breakpoint, step, pause,
 *    entry, exception), only that thread parks in hold(), inside an unmanaged
 *    region of its context; the others go on. It wakes for two reasons: a
 *    resume request for it, or an inspection task (variables, evaluate) it
 *    runs itself, on its own context, so every ProtoObject is touched by the
 *    thread that owns it.
 *  - A reader thread, never registered with protoCore, reads requests and
 *    answers them. It touches no ProtoObject: what it reports of a stop are
 *    strings copied by the stopped thread, and what needs the objects goes to
 *    that thread as a task.
 *  - DapStdio's two pump threads turn the script's stdout/stderr into
 *    `output` events.
 */

#include <protoPython/DAPServer.h>
#include <protoPython/Debugger.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include "dap/DapJson.h"
#include "dap/DapStdio.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace protoPython {

namespace {

using dap::Json;

std::filesystem::path u8Path(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// --- framing -----------------------------------------------------------------

bool readHeaderLine(std::istream& in, std::string& line) {
    line.clear();
    int c;
    bool any = false;
    while ((c = in.get()) != std::char_traits<char>::eof()) {
        any = true;
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        line.push_back(static_cast<char>(c));
    }
    return any;
}

// One message, or nullopt at end of input or on a frame that cannot be read.
std::optional<Json> readMessage(std::istream& in) {
    long long length = -1;
    std::string line;
    for (;;) {
        if (!readHeaderLine(in, line)) return std::nullopt;
        if (line.empty()) {
            if (length < 0) continue;  // stray blank line before a header
            break;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (key == "content-length") {
            try {
                length = std::stoll(line.substr(colon + 1));
            } catch (...) {
                return std::nullopt;
            }
        }
    }
    std::string body(static_cast<std::size_t>(length), '\0');
    in.read(body.data(), length);
    if (in.gcount() != length) return std::nullopt;
    auto msg = Json::parse(body);
    if (!msg) return Json::object();  // malformed JSON: ignored, the stream goes on
    return msg;
}

// --- path mappings (launch.pathMappings) ------------------------------------------

// A path as Windows writes it: a drive letter, a UNC prefix or a backslash.
bool windowsLike(const std::string& p) {
    return (p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':')
        || p.find('\\') != std::string::npos;
}

// '/' separators, no trailing separator (but "/" and "c:/" stay).
std::string slashed(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    while (p.size() > 1 && p.back() == '/' && !(p.size() == 3 && p[1] == ':')) p.pop_back();
    return p;
}

// `path` under `root`: the rest after it ('/' separators).
bool underRoot(const std::string& path, const std::string& root, bool caseInsensitive, std::string& rest) {
    const std::string pp = slashed(path), rr = slashed(root);
    if (pp.size() < rr.size()) return false;
    for (std::size_t i = 0; i < rr.size(); ++i) {
        char a = pp[i], b = rr[i];
        if (caseInsensitive) {
            a = static_cast<char>(std::tolower(static_cast<unsigned char>(a)));
            b = static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
        }
        if (a != b) return false;
    }
    if (pp.size() == rr.size()) { rest.clear(); return true; }
    if (rr.back() == '/') { rest = pp.substr(rr.size()); return true; }
    if (pp[rr.size()] != '/') return false;
    rest = pp.substr(rr.size() + 1);
    return true;
}

// `rest` ('/' separators) under `root`, in the separators `root` uses.
std::string joinRoot(const std::string& root, const std::string& rest) {
    const char sep = windowsLike(root) ? '\\' : '/';
    std::string r = root;
    while (r.size() > 1 && (r.back() == '/' || r.back() == '\\') && !(r.size() == 3 && r[1] == ':')) r.pop_back();
    if (rest.empty()) return r;
    std::string t = rest;
    if (sep == '\\') std::replace(t.begin(), t.end(), '/', '\\');
    if (r.back() != '/' && r.back() != '\\') r += sep;
    return r + t;
}

struct PathMapping {
    std::string localRoot;   // the client's side
    std::string remoteRoot;  // where the adapter runs
};

std::string baseName(const std::string& p) {
    const auto slash = p.find_last_of("/\\");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto nl = text.find('\n', start);
        if (nl == std::string::npos) {
            if (start < text.size()) lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(u8Path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// --- the session ---------------------------------------------------------------

enum class Step { None, In, Over, Out };

/** A breakpoint's hitCondition: N or == N, >= N, > N, < N, <= N, % N. */
struct HitCondition {
    enum Op { None, Eq, Ge, Gt, Lt, Le, Mod } op = None;
    long long n = 0;
    bool passes(long long hits) const {
        switch (op) {
            case None: return true;
            case Eq:   return hits == n;
            case Ge:   return hits >= n;
            case Gt:   return hits > n;
            case Lt:   return hits < n;
            case Le:   return hits <= n;
            case Mod:  return n > 0 && hits % n == 0;
        }
        return true;
    }
    // False when `text` is not one of the forms above.
    static bool parse(const std::string& text, HitCondition& out) {
        std::size_t i = 0;
        auto skip = [&] { while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i; };
        skip();
        out = HitCondition{};
        if (i >= text.size()) return true;  // empty: none
        auto at = [&](const char* s) { return text.compare(i, std::strlen(s), s) == 0; };
        if (at("==")) { out.op = Eq; i += 2; }
        else if (at(">=")) { out.op = Ge; i += 2; }
        else if (at("<=")) { out.op = Le; i += 2; }
        else if (at(">")) { out.op = Gt; i += 1; }
        else if (at("<")) { out.op = Lt; i += 1; }
        else if (at("%")) { out.op = Mod; i += 1; }
        else out.op = Eq;
        skip();
        const std::size_t start = i;
        long long n = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            n = n * 10 + (text[i] - '0');
            if (n > 1000000000000LL) return false;
            ++i;
        }
        if (i == start) return false;
        skip();
        if (i != text.size()) return false;
        if (out.op == Mod && n == 0) return false;
        out.n = n;
        return true;
    }
};

struct Breakpoint {
    std::string condition;
    HitCondition hit;
    std::string logMessage;
    long long hits = 0;  // hits whose condition passed
};

struct FrameSnap {
    int id;
    std::string name;
    std::string path;
    int line;
    debugger::StackEntry entry;  // its objects rooted by the held activations
};

struct VarRef {
    enum Kind { Locals, Globals, Object } kind;
    std::size_t frameIndex;
    std::size_t pin;
};

/** One Python thread of the program: what the client sees of it and, while it
 *  is stopped, the stop. The atomics are read by the thread at every line
 *  event; the rest is guarded by Session::mu_. */
struct ThreadState {
    int id = 0;
    std::string name;
    std::atomic<Step> step{Step::None};
    std::atomic<std::size_t> stepDepth{0};
    std::atomic<bool> pauseRequested{false};

    bool stopped = false;
    bool resume = false;
    std::size_t stopDepth = 0;
    std::vector<FrameSnap> frames;
    std::deque<std::function<void()>> tasks;
    PythonEnvironment* env = nullptr;
    proto::ProtoContext* ctx = nullptr;
    // The stop's exception (reason "exception").
    bool hasException = false;
    std::string excType, excMessage, excBreakMode, excTrace;

    debugger::Pins pins;  // touched by the thread itself only
};

// The ThreadState of the calling thread, while it runs the program's code.
thread_local ThreadState* tl_thread = nullptr;

class Session : public debugger::Listener {
public:
    Session() = default;

    int run(const DAPServer::ProgramRunner& runner) {
        stdio_.startForwarding([this](const char* category, const std::string& text) {
            sendOutput(category, text);
        });
        reader_ = std::thread([this] { readLoop(); });

        // The program starts once the client configured the session.
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return configDone_ || readerDone_; });
        }
        int exitCode = 0;
        bool ran = false;
        if (launched_ && configDone_ && !disconnecting_) {
            applyLaunchEnvironment();
            auto main = std::make_shared<ThreadState>();
            main->id = 1;
            main->name = "MainThread";
            {
                std::lock_guard<std::mutex> lk(mu_);
                threads_[1] = main;
            }
            tl_thread = main.get();
            debugger::setListener(this);
            debugger::enableOnThisThread(true);
            ran = true;
            try {
                exitCode = runner(launch_);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "protopy: %s\n", e.what());
                exitCode = 70;
            } catch (...) {
                exitCode = 70;
            }
            debugger::enableOnThisThread(false);
            // Daemon threads may still run: they stop no more, and any
            // stopped one goes on (the process ends below).
            shuttingDown_.store(true);
            debugger::setExceptionEvents(false);
            releaseAll();
            debugger::setListener(nullptr);
            tl_thread = nullptr;
        }
        programDone_.store(true);
        // The script's output goes before the end of the session.
        stdio_.finish(nullptr);
        if (ran) {
            sendEvent("exited", obj({{"exitCode", Json(exitCode)}}));
            sendEvent("terminated", Json::object());
        }
        // The client answers `terminated` with `disconnect`.
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::seconds(30), [&] { return readerDone_ || disconnecting_; });
        }
        if (reader_.joinable()) {
            bool done;
            {
                std::lock_guard<std::mutex> lk(mu_);
                done = readerDone_;
            }
            if (done) reader_.join();
            else reader_.detach();  // still blocked reading: the process ends anyway
        }
        return exitCode;
    }

    // --- debugger::Listener: on the program's threads -------------------------
    void onLine(proto::ProtoContext* ctx, PythonEnvironment* env, const debugger::CodeInfo& code,
                int line, debugger::LineEventKind kind, std::size_t depth) override {
        ThreadState* ts = tl_thread;
        if (!ts) return;
        const Step st = ts->step.load(std::memory_order_acquire);
        const bool entry = ts->id == 1 && entryPending_.load(std::memory_order_relaxed);
        const bool pause = ts->pauseRequested.load(std::memory_order_relaxed);
        if (st == Step::None && !entry && !pause && !anyBreakpoint_.load(std::memory_order_relaxed)) return;
        if (shuttingDown_.load(std::memory_order_relaxed)) return;
        const bool isLine = kind == debugger::LineEventKind::Line;

        const char* reason = nullptr;
        if (entry && isLine && code.path == programPath_) {
            entryPending_.store(false);
            reason = "entry";
        }
        if (!reason && pause) {
            ts->pauseRequested.store(false);
            reason = "pause";
        }
        if (!reason) {
            const std::size_t base = ts->stepDepth.load(std::memory_order_relaxed);
            switch (st) {
                case Step::In:    if (isLine || depth < base) reason = "step"; break;
                case Step::Over:  if (isLine ? depth <= base : depth < base) reason = "step"; break;
                case Step::Out:   if (depth < base) reason = "step"; break;
                case Step::None:  break;
            }
        }
        Json extra = Json::object();
        if (!reason && isLine && anyBreakpoint_.load(std::memory_order_relaxed)) {
            std::optional<Breakpoint> bp;
            {
                std::lock_guard<std::mutex> lk(bpMu_);
                auto it = breakpoints_.find(code.key);
                if (it != breakpoints_.end()) {
                    auto b = it->second.find(line);
                    if (b != it->second.end()) bp = b->second;
                }
            }
            if (bp && breakpointStops(ctx, env, *bp, code.key, line, extra)) reason = "breakpoint";
        }
        if (!reason) return;
        ts->step.store(Step::None);
        hold(ctx, env, ts, reason, depth, extra);
    }

    // A breakpoint's condition, hit count and log message, on the thread at
    // the line (its top frame). True when it stops.
    bool breakpointStops(proto::ProtoContext* ctx, PythonEnvironment* env, const Breakpoint& bp,
                         const std::string& path, int line, Json& extra) {
        if (bp.condition.empty() && bp.hit.op == HitCondition::None && bp.logMessage.empty()) return true;
        std::vector<debugger::StackEntry> stack;
        debugger::currentStack(stack);
        if (stack.empty()) return true;
        if (!bp.condition.empty()) {
            std::string error;
            const auto r = debugger::evaluateCondition(ctx, env, stack[0], bp.condition, nullptr, nullptr, error);
            if (r == debugger::ConditionResult::Error) {
                const std::string msg = "breakpoint condition `" + bp.condition + "` failed: " + error;
                sendOutput("stderr", msg + "\n");
                extra["description"] = Json(msg);
                return true;
            }
            if (r == debugger::ConditionResult::False) return false;
        }
        long long hits = 1;
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            auto it = breakpoints_.find(path);
            if (it != breakpoints_.end()) {
                auto b = it->second.find(line);
                if (b != it->second.end()) hits = ++b->second.hits;
            }
        }
        if (!bp.hit.passes(hits)) return false;
        if (!bp.logMessage.empty()) {
            debugger::flushStandardStreams(ctx, env);
            sendOutput("console", debugger::interpolateLogMessage(ctx, env, stack[0], bp.logMessage) + "\n");
            return false;
        }
        return true;
    }

    void onException(proto::ProtoContext* ctx, PythonEnvironment* env, const debugger::ExceptionEvent& event,
                     std::size_t depth) override {
        ThreadState* ts = tl_thread;
        if (!ts || shuttingDown_.load(std::memory_order_relaxed)) return;
        const bool raised = breakOnRaised_.load(std::memory_order_relaxed);
        const bool userUnhandled = breakOnUserUnhandled_.load(std::memory_order_relaxed);
        if (!raised && !userUnhandled) return;
        debugger::ExceptionDescription d;
        if (!debugger::describeException(ctx, env, event.exception, d)) d.typeName = "exception";
        if (d.internal) return;
        std::string conditionRaised, conditionUserUnhandled;
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            conditionRaised = conditionRaised_;
            conditionUserUnhandled = conditionUserUnhandled_;
        }
        // A filter's condition, in the frame the exception is in, with the
        // exception bound as `exc`. A condition that fails stops (and says so).
        std::string conditionError;
        auto passes = [&](const std::string& condition) {
            if (condition.empty()) return true;
            std::vector<debugger::StackEntry> stack;
            debugger::currentStack(stack);
            if (stack.empty()) return true;
            std::string error;
            const auto r = debugger::evaluateCondition(ctx, env, stack[0], condition, "exc", event.exception, error);
            if (r == debugger::ConditionResult::Error) {
                conditionError = "exception breakpoint condition `" + condition + "` failed: " + error;
                return true;
            }
            return r == debugger::ConditionResult::True;
        };
        const char* mode = nullptr;
        if (raised && passes(conditionRaised)) mode = "always";
        else if (userUnhandled && event.handledInUserCode && !event.handledInUserCode() && passes(conditionUserUnhandled))
            mode = "userUnhandled";
        if (!mode) return;
        std::string description = d.message.empty() ? d.typeName : d.typeName + ": " + d.message;
        if (!event.origin.empty()) description += "  (raised in " + event.origin + ")";
        if (!conditionError.empty()) {
            sendOutput("stderr", conditionError + "\n");
            description += " [" + conditionError + "]";
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            ts->hasException = true;
            ts->excType = d.typeName;
            ts->excMessage = d.message;
            ts->excBreakMode = mode;
        }
        // A step in progress ends at the exception.
        ts->step.store(Step::None);
        hold(ctx, env, ts, "exception", depth,
             obj({{"description", Json(description)}, {"text", Json(d.typeName)}}));
    }

    void onThreadStart(proto::ProtoContext*, PythonEnvironment*, const std::string& name) override {
        auto ts = std::make_shared<ThreadState>();
        {
            std::lock_guard<std::mutex> lk(mu_);
            ts->id = nextThreadId_++;
            ts->name = name.empty() ? "Thread-" + std::to_string(ts->id) : name;
            threads_[ts->id] = ts;
        }
        tl_thread = ts.get();
        sendEvent("thread", obj({{"reason", Json("started")}, {"threadId", Json(ts->id)}}));
    }

    void onCodeLoaded(const debugger::CodeInfo& code) override {
        if (code.file.empty()) return;
        {
            std::lock_guard<std::mutex> lk(srcMu_);
            loaded_.push_back(&code);
        }
        sendEvent("loadedSource", obj({{"reason", Json("new")}, {"source", sourceFor(code)}}));
    }

    void onThreadExit() override {
        ThreadState* ts = tl_thread;
        if (!ts) return;
        tl_thread = nullptr;
        const int id = ts->id;
        {
            std::lock_guard<std::mutex> lk(mu_);
            threads_.erase(id);  // the last reference: ts is gone
        }
        sendEvent("thread", obj({{"reason", Json("exited")}, {"threadId", Json(id)}}));
    }

private:
    // --- holding a program thread ---------------------------------------------
    void hold(proto::ProtoContext* ctx, PythonEnvironment* env, ThreadState* ts, const char* reason,
              std::size_t depth, Json extra) {
        // What the script printed so far reaches the console before the stop.
        debugger::flushStandardStreams(ctx, env);
        // threading's name: Thread.name may have changed since the start.
        const std::string threadName = debugger::currentThreadName(ctx, env);
        std::vector<debugger::StackEntry> stack;
        debugger::currentStack(stack);
        {
            std::lock_guard<std::mutex> lk(mu_);
            // The session ended (releaseAll ran) after this thread decided
            // to stop: it must not wait for a resume that never comes.
            if (shuttingDown_.load()) {
                ts->hasException = false;
                return;
            }
            if (!threadName.empty()) ts->name = threadName;
            ts->frames.clear();
            for (std::size_t i = 0; i < stack.size(); ++i) {
                const auto& e = stack[i];
                const int id = nextFrameId_++;
                frameOwners_[id] = FrameOwner{ts->id, i};
                ts->frames.push_back(FrameSnap{id, e.code->name, e.code->displayPath, e.line, e});
            }
            if (ts->hasException) {
                std::string trace = "Traceback (most recent call last):\n";
                for (auto it = ts->frames.rbegin(); it != ts->frames.rend(); ++it)
                    trace += "  File \"" + clientLabel(*it->entry.code) + "\", line " + std::to_string(it->line) + ", in " + it->name + "\n";
                ts->excTrace = trace + (ts->excMessage.empty() ? ts->excType : ts->excType + ": " + ts->excMessage) + "\n";
            }
            ts->resume = false;
            ts->stopped = true;
            ts->stopDepth = depth;
            ts->env = env;
            ts->ctx = ctx;
        }
        Json body = obj({{"reason", Json(reason)}, {"threadId", Json(ts->id)}, {"allThreadsStopped", Json(false)}});
        for (const auto& m : extra.members()) body[m.first] = m.second;
        sendEvent("stopped", body);

        for (;;) {
            std::function<void()> task;
            {
                // Parked for as long as the user takes: out of the collector's
                // quorum, so the other threads run and collect. The lock is
                // released before the region ends.
                proto::ProtoContext::UnmanagedScope unmanaged(ctx);
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return ts->resume || !ts->tasks.empty(); });
                if (!ts->tasks.empty()) {
                    task = std::move(ts->tasks.front());
                    ts->tasks.pop_front();
                }
            }
            if (!task) break;
            task();
        }
        ts->pins.release();
        std::lock_guard<std::mutex> lk(mu_);
        forgetStopLocked(*ts);
    }

    // Drops what the client may still name of the thread's stop (mu_ held).
    void forgetStopLocked(ThreadState& ts) {
        for (const auto& f : ts.frames) frameOwners_.erase(f.id);
        for (auto it = varRefs_.begin(); it != varRefs_.end();) {
            if (it->second.threadId == ts.id) it = varRefs_.erase(it);
            else ++it;
        }
        ts.stopped = false;
        ts.frames.clear();
        ts.hasException = false;
        ts.env = nullptr;
        ts.ctx = nullptr;
    }

    // Lets every stopped thread go (the end of the session).
    void releaseAll() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& kv : threads_) {
                kv.second->step.store(Step::None);
                kv.second->pauseRequested.store(false);
                kv.second->resume = true;
            }
        }
        cv_.notify_all();
    }

    std::shared_ptr<ThreadState> threadLocked(long long id) {
        auto it = threads_.find(static_cast<int>(id));
        return it == threads_.end() ? nullptr : it->second;
    }

    // Runs `fn(ctx, env)` on the stopped thread `ts` and waits for it. False
    // when that thread is not stopped.
    bool onThread(const std::shared_ptr<ThreadState>& ts,
                  const std::function<void(proto::ProtoContext*, PythonEnvironment*)>& fn) {
        auto done = std::make_shared<bool>(false);
        std::unique_lock<std::mutex> lk(mu_);
        if (!ts || !ts->stopped || ts->resume) return false;
        ThreadState* t = ts.get();
        t->tasks.push_back([this, t, fn, done] {
            proto::ProtoContext* ctx;
            PythonEnvironment* env;
            {
                std::lock_guard<std::mutex> g(mu_);
                ctx = t->ctx;
                env = t->env;
            }
            try {
                fn(ctx, env);
            } catch (...) {
            }
            std::lock_guard<std::mutex> g(mu_);
            *done = true;
            cv_.notify_all();
        });
        cv_.notify_all();
        cv_.wait(lk, [&] { return *done; });
        return true;
    }

    // --- the reader thread -------------------------------------------------------
    void readLoop() {
        std::istream& in = stdio_.protocolIn();
        for (;;) {
            auto msg = readMessage(in);
            if (!msg) break;
            if (msg->getString("type") != "request") continue;
            if (handle(*msg)) break;
        }
        bool exitNow;
        {
            std::lock_guard<std::mutex> lk(mu_);
            readerDone_ = true;
            exitNow = launched_ && configDone_ && !programDone_.load();
        }
        cv_.notify_all();
        // The client is gone (or asked to stop) while the program runs: end it.
        if (exitNow) terminateProcess();
    }

    [[noreturn]] void terminateProcess() {
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }

    // True when the loop ends.
    bool handle(const Json& req) {
        const std::string command = req.getString("command");
        const Json& args = req.get("arguments");
        if (command == "initialize") {
            Json filters = Json::array();
            filters.push(obj({{"filter", Json("raised")},
                              {"label", Json("Raised Exceptions")},
                              {"supportsCondition", Json(true)},
                              {"conditionDescription", Json("A Python expression; the exception is `exc`. "
                                                            "Example: isinstance(exc, KeyError)")},
                              {"description", Json("Stop where an exception is raised in your code, or reaches it "
                                                   "from a library, even when it is handled.")},
                              {"default", Json(false)}}));
            filters.push(obj({{"filter", Json("userUnhandled")},
                              {"label", Json("Uncaught Exceptions")},
                              {"supportsCondition", Json(true)},
                              {"conditionDescription", Json("A Python expression; the exception is `exc`. "
                                                            "Example: not isinstance(exc, KeyboardInterrupt)")},
                              {"description", Json("Stop where an exception is raised when no try/except of your "
                                                   "code on that thread handles it.")},
                              {"default", Json(true)}}));
            Json caps = obj({{"supportsConfigurationDoneRequest", Json(true)},
                             {"supportsEvaluateForHovers", Json(true)},
                             {"supportsTerminateRequest", Json(true)},
                             {"supportsExceptionInfoRequest", Json(true)},
                             {"supportsExceptionFilterOptions", Json(true)},
                             {"supportsConditionalBreakpoints", Json(true)},
                             {"supportsHitConditionalBreakpoints", Json(true)},
                             {"supportsLogPoints", Json(true)},
                             {"supportsLoadedSourcesRequest", Json(true)}});
            caps["exceptionBreakpointFilters"] = filters;
            respond(req, true, caps);
            sendEvent("initialized", Json::object());
        } else if (command == "launch") {
            handleLaunch(req, args);
        } else if (command == "attach") {
            respondError(req, "protopy --dap supports 'launch' only");
        } else if (command == "setBreakpoints") {
            handleSetBreakpoints(req, args);
        } else if (command == "setExceptionBreakpoints") {
            handleSetExceptionBreakpoints(req, args);
        } else if (command == "setFunctionBreakpoints") {
            respond(req, true, obj({{"breakpoints", Json::array()}}));
        } else if (command == "configurationDone") {
            respond(req, true, Json::object());
            {
                std::lock_guard<std::mutex> lk(mu_);
                configDone_ = true;
            }
            cv_.notify_all();
        } else if (command == "threads") {
            Json threads = Json::array();
            {
                std::lock_guard<std::mutex> lk(mu_);
                for (const auto& kv : threads_)
                    threads.push(obj({{"id", Json(kv.first)}, {"name", Json(kv.second->name)}}));
            }
            if (threads.size() == 0) threads.push(obj({{"id", Json(1)}, {"name", Json("MainThread")}}));
            respond(req, true, obj({{"threads", threads}}));
        } else if (command == "continue" || command == "next" || command == "stepIn" || command == "stepOut") {
            handleResume(req, command, args);
        } else if (command == "pause") {
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (auto ts = threadLocked(args.getInt("threadId", 1))) ts->pauseRequested.store(true);
            }
            respond(req, true, Json::object());
        } else if (command == "stackTrace") {
            handleStackTrace(req, args);
        } else if (command == "scopes") {
            handleScopes(req, args);
        } else if (command == "variables") {
            handleVariables(req, args);
        } else if (command == "evaluate") {
            handleEvaluate(req, args);
        } else if (command == "source") {
            handleSource(req, args);
        } else if (command == "loadedSources") {
            Json sources = Json::array();
            std::vector<const debugger::CodeInfo*> loaded;
            {
                std::lock_guard<std::mutex> lk(srcMu_);
                loaded = loaded_;
            }
            for (const auto* c : loaded) sources.push(sourceFor(*c));
            respond(req, true, obj({{"sources", sources}}));
        } else if (command == "exceptionInfo") {
            handleExceptionInfo(req, args);
        } else if (command == "disconnect" || command == "terminate") {
            respond(req, true, Json::object());
            shuttingDown_.store(true);
            {
                std::lock_guard<std::mutex> lk(mu_);
                disconnecting_ = true;
            }
            releaseAll();
            return true;
        } else {
            respondError(req, "unsupported request '" + command + "'");
        }
        return false;
    }

    void handleLaunch(const Json& req, const Json& args) {
        DAPServer::LaunchRequest launch;
        std::vector<PathMapping> mappings;
        const Json& pm = args.get("pathMappings");
        if (!pm.isNull() && !pm.isArray()) {
            respondError(req, "launch: 'pathMappings' must be a list of {\"localRoot\", \"remoteRoot\"}");
            return;
        }
        for (std::size_t i = 0; i < pm.size(); ++i) {
            PathMapping m{pm.at(i).getString("localRoot"), pm.at(i).getString("remoteRoot")};
            if (m.localRoot.empty() || m.remoteRoot.empty()) {
                respondError(req, "launch: pathMappings[" + std::to_string(i) +
                                  "] needs both 'localRoot' (the client's folder) and 'remoteRoot' (the same folder "
                                  "where protopy runs)");
                return;
            }
            mappings.push_back(std::move(m));
        }
        // Longest roots first: the most specific mapping wins.
        std::stable_sort(mappings.begin(), mappings.end(), [](const PathMapping& a, const PathMapping& b) {
            return slashed(a.localRoot).size() > slashed(b.localRoot).size();
        });
        {
            std::lock_guard<std::mutex> lk(srcMu_);
            mappings_ = mappings;
        }
        launch.program = toAdapter(args.getString("program"));
        if (launch.program.empty()) {
            respondError(req, "launch: 'program' is required");
            return;
        }
        std::error_code ec;
        if (!std::filesystem::is_regular_file(u8Path(launch.program), ec)) {
            respondError(req, "launch: cannot open program '" + launch.program + "'");
            return;
        }
        launch.program = debugger::displayPath(launch.program);
        const Json& a = args.get("args");
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a.at(i).isString()) launch.args.push_back(a.at(i).str());
        launch.cwd = args.getString("cwd").empty() ? std::string() : toAdapter(args.getString("cwd"));
        for (const auto& kv : args.get("env").members())
            if (kv.second.isString()) launch.env.emplace_back(kv.first, kv.second.str());
        {
            std::lock_guard<std::mutex> lk(mu_);
            launch_ = launch;
            launched_ = true;
            programPath_ = debugger::normalizePath(launch.program);
            entryPending_.store(args.getBool("stopOnEntry", false));
        }
        respond(req, true, Json::object());
    }

    void applyLaunchEnvironment() {
        for (const auto& kv : launch_.env) {
#if defined(_WIN32)
            _putenv_s(kv.first.c_str(), kv.second.c_str());
#else
            setenv(kv.first.c_str(), kv.second.c_str(), 1);
#endif
        }
        if (!launch_.cwd.empty()) {
            std::error_code ec;
            std::filesystem::current_path(u8Path(launch_.cwd), ec);
            if (ec) sendOutput("stderr", "protopy: cannot change to directory '" + launch_.cwd + "'\n");
        }
    }

    // A breakpoint on a blank or comment line moves to the next line with code,
    // as the line has no instructions of its own.
    static std::vector<int> codeLines(const std::vector<std::string>& lines, const std::vector<int>& requested) {
        auto isCode = [&](int n) {
            if (n < 1 || n > static_cast<int>(lines.size())) return false;
            const std::string& s = lines[static_cast<std::size_t>(n - 1)];
            for (char c : s) {
                if (c == ' ' || c == '\t' || c == '\r' || c == '\f') continue;
                return c != '#';
            }
            return false;
        };
        std::vector<int> out;
        for (int n : requested) {
            int m = n;
            if (!lines.empty()) {
                while (m <= static_cast<int>(lines.size()) && m < n + 200 && !isCode(m)) ++m;
                if (!isCode(m)) m = n;
            }
            out.push_back(m);
        }
        return out;
    }

    void handleSetBreakpoints(const Json& req, const Json& args) {
        const Json& source = args.get("source");
        std::string key;
        std::vector<std::string> lines;
        if (!resolveSource(source, key, lines)) {
            Json result = Json::array();
            const Json& bps = args.get("breakpoints");
            for (std::size_t i = 0; i < bps.size(); ++i)
                result.push(obj({{"verified", Json(false)}, {"message", Json("unknown source")}}));
            respond(req, true, obj({{"breakpoints", result}}));
            return;
        }
        std::vector<int> requested;
        std::vector<Breakpoint> specs;
        const Json& bps = args.get("breakpoints");
        for (std::size_t i = 0; i < bps.size(); ++i) {
            requested.push_back(static_cast<int>(bps.at(i).getInt("line")));
            Breakpoint b;
            b.condition = bps.at(i).getString("condition");
            b.logMessage = bps.at(i).getString("logMessage");
            if (!HitCondition::parse(bps.at(i).getString("hitCondition"), b.hit)) b.hits = -1;  // invalid
            specs.push_back(std::move(b));
        }
        if (bps.size() == 0) {
            const Json& lines = args.get("lines");
            for (std::size_t i = 0; i < lines.size(); ++i)
                if (lines.at(i).isNumber()) requested.push_back(static_cast<int>(lines.at(i).num()));
        }
        const std::vector<int> actual = codeLines(lines, requested);
        // The breakpoints name the source as the request did.
        Json echo = Json::object();
        if (source.contains("sourceReference") && source.getInt("sourceReference") > 0)
            echo["sourceReference"] = Json(source.getInt("sourceReference"));
        if (!source.getString("path").empty()) echo["path"] = Json(source.getString("path"));
        if (!source.getString("name").empty()) echo["name"] = Json(source.getString("name"));
        Json result = Json::array();
        std::map<int, Breakpoint> table;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            Breakpoint b = i < specs.size() ? specs[i] : Breakpoint{};
            if (b.hits < 0) {
                result.push(obj({{"verified", Json(false)}, {"line", Json(actual[i])}, {"source", echo},
                                 {"message", Json("invalid hit count '" + bps.at(i).getString("hitCondition") +
                                                  "': use N, == N, >= N, > N, < N, <= N or % N")}}));
                continue;
            }
            table[actual[i]] = std::move(b);
            result.push(obj({{"verified", Json(true)}, {"line", Json(actual[i])}, {"source", echo}}));
        }
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            if (table.empty()) breakpoints_.erase(key);
            else breakpoints_[key] = std::move(table);
            anyBreakpoint_.store(!breakpoints_.empty());
        }
        respond(req, true, obj({{"breakpoints", result}}));
    }

    void handleSetExceptionBreakpoints(const Json& req, const Json& args) {
        std::vector<std::string> filters;
        const Json& f = args.get("filters");
        for (std::size_t i = 0; i < f.size(); ++i)
            if (f.at(i).isString()) filters.push_back(f.at(i).str());
        const Json& fo = args.get("filterOptions");
        std::string conditionRaised, conditionUserUnhandled;
        for (std::size_t i = 0; i < fo.size(); ++i) {
            const std::string id = fo.at(i).getString("filterId");
            filters.push_back(id);
            if (id == "raised") conditionRaised = fo.at(i).getString("condition");
            if (id == "userUnhandled") conditionUserUnhandled = fo.at(i).getString("condition");
        }
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            conditionRaised_ = conditionRaised;
            conditionUserUnhandled_ = conditionUserUnhandled;
        }
        bool raised = false, userUnhandled = false;
        Json result = Json::array();
        for (const auto& name : filters) {
            const bool known = name == "raised" || name == "userUnhandled";
            if (name == "raised") raised = true;
            if (name == "userUnhandled") userUnhandled = true;
            Json b = obj({{"verified", Json(known)}});
            if (!known) b["message"] = Json("unknown exception filter '" + name + "'");
            result.push(std::move(b));
        }
        breakOnRaised_.store(raised);
        breakOnUserUnhandled_.store(userUnhandled);
        debugger::setExceptionEvents(raised || userUnhandled);
        respond(req, true, obj({{"breakpoints", result}}));
    }

    void handleResume(const Json& req, const std::string& command, const Json& args) {
        Step st = Step::None;
        if (command == "next") st = Step::Over;
        else if (command == "stepIn") st = Step::In;
        else if (command == "stepOut") st = Step::Out;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (auto ts = threadLocked(args.getInt("threadId", 1))) {
                if (ts->stopped && !ts->resume) {
                    ts->stepDepth.store(ts->stopDepth);
                    ts->step.store(st);
                    ts->resume = true;
                }
            }
        }
        cv_.notify_all();
        respond(req, true, command == "continue" ? obj({{"allThreadsContinued", Json(false)}}) : Json::object());
    }

    void handleStackTrace(const Json& req, const Json& args) {
        const long long start = args.getInt("startFrame", 0);
        const long long levels = args.getInt("levels", 0);
        Json frames = Json::array();
        long long total = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto ts = threadLocked(args.getInt("threadId", 1));
            if (ts && ts->stopped) {
                total = static_cast<long long>(ts->frames.size());
                for (long long i = start; i < total; ++i) {
                    if (levels > 0 && static_cast<long long>(frames.size()) >= levels) break;
                    const FrameSnap& f = ts->frames[static_cast<std::size_t>(i)];
                    Json frame = obj({{"id", Json(f.id)}, {"name", Json(f.name)}, {"line", Json(f.line)}, {"column", Json(1)}});
                    if (f.entry.code) frame["source"] = sourceFor(*f.entry.code);
                    frames.push(std::move(frame));
                }
            }
        }
        respond(req, true, obj({{"stackFrames", frames}, {"totalFrames", Json(total)}}));
    }

    struct FrameOwner {
        int threadId;
        std::size_t index;
    };
    struct VarRefEntry {
        int threadId;
        VarRef ref;
    };

    int newVarRefLocked(int threadId, VarRef r) {
        const int id = nextVarRef_++;
        varRefs_.emplace(id, VarRefEntry{threadId, r});
        return id;
    }

    // The stopped thread and frame a frameId names (mu_ held).
    bool frameLocked(long long frameId, std::shared_ptr<ThreadState>& ts, std::size_t& index) {
        auto it = frameOwners_.find(static_cast<int>(frameId));
        if (it == frameOwners_.end()) return false;
        ts = threadLocked(it->second.threadId);
        index = it->second.index;
        return ts && ts->stopped && index < ts->frames.size();
    }

    void handleScopes(const Json& req, const Json& args) {
        Json scopes = Json::array();
        {
            std::lock_guard<std::mutex> lk(mu_);
            std::shared_ptr<ThreadState> ts;
            std::size_t idx = 0;
            if (frameLocked(args.getInt("frameId", 0), ts, idx)) {
                scopes.push(obj({{"name", Json("Locals")},
                                 {"presentationHint", Json("locals")},
                                 {"variablesReference", Json(newVarRefLocked(ts->id, VarRef{VarRef::Locals, idx, 0}))},
                                 {"expensive", Json(false)}}));
                if (ts->frames[idx].name != "<module>")
                    scopes.push(obj({{"name", Json("Globals")},
                                     {"variablesReference", Json(newVarRefLocked(ts->id, VarRef{VarRef::Globals, idx, 0}))},
                                     {"expensive", Json(false)}}));
            }
        }
        respond(req, true, obj({{"scopes", scopes}}));
    }

    // A value of the stopped thread `ts`: on that thread.
    Json describe(ThreadState* ts, PythonEnvironment* env, const debugger::Value& v, bool withName) {
        int ref = 0;
        if (v.expandable && v.object) {
            const std::size_t pin = ts->pins.add(env, v.object);
            std::lock_guard<std::mutex> lk(mu_);
            ref = newVarRefLocked(ts->id, VarRef{VarRef::Object, 0, pin});
        }
        Json j = Json::object();
        if (withName) {
            j["name"] = Json(v.name);
            j["value"] = Json(v.value);
        } else {
            j["result"] = Json(v.value);
        }
        if (!v.type.empty()) j["type"] = Json(v.type);
        j["variablesReference"] = Json(ref);
        return j;
    }

    void handleVariables(const Json& req, const Json& args) {
        const long long ref = args.getInt("variablesReference", 0);
        std::optional<VarRef> target;
        std::shared_ptr<ThreadState> ts;
        debugger::StackEntry frame{};
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = varRefs_.find(static_cast<int>(ref));
            if (it != varRefs_.end()) {
                ts = threadLocked(it->second.threadId);
                target = it->second.ref;
                if (!ts || !ts->stopped) target.reset();
                else if (target->kind != VarRef::Object) {
                    if (target->frameIndex < ts->frames.size()) frame = ts->frames[target->frameIndex].entry;
                    else target.reset();
                }
            }
        }
        Json vars = Json::array();
        if (target) {
            ThreadState* t = ts.get();
            onThread(ts, [&](proto::ProtoContext* ctx, PythonEnvironment* env) {
                std::vector<debugger::Value> values;
                switch (target->kind) {
                    case VarRef::Locals:  debugger::frameLocals(ctx, env, frame, values); break;
                    case VarRef::Globals: debugger::frameGlobals(ctx, env, frame, values); break;
                    case VarRef::Object:  debugger::children(ctx, env, t->pins.get(target->pin), values); break;
                }
                for (const auto& v : values) vars.push(describe(t, env, v, true));
            });
        }
        respond(req, true, obj({{"variables", vars}}));
    }

    void handleEvaluate(const Json& req, const Json& args) {
        const std::string expr = args.getString("expression");
        const std::string context = args.getString("context");
        std::optional<debugger::StackEntry> frame;
        std::shared_ptr<ThreadState> ts;
        {
            std::lock_guard<std::mutex> lk(mu_);
            std::size_t idx = 0;
            if (args.contains("frameId")) {
                if (frameLocked(args.getInt("frameId", 0), ts, idx)) frame = ts->frames[idx].entry;
            } else {
                // No frame selected: the top frame of a stopped thread.
                for (const auto& kv : threads_) {
                    if (kv.second->stopped && !kv.second->frames.empty()) {
                        ts = kv.second;
                        frame = ts->frames[0].entry;
                        break;
                    }
                }
            }
        }
        if (!frame) {
            respondError(req, "evaluate: the program is not stopped");
            return;
        }
        bool ok = false;
        Json body;
        std::string error;
        ThreadState* t = ts.get();
        const bool ran = onThread(ts, [&](proto::ProtoContext* ctx, PythonEnvironment* env) {
            debugger::Value v;
            ok = debugger::evaluate(ctx, env, *frame, expr, context == "repl", v, error);
            if (ok) body = describe(t, env, v, false);
            // A statement run in the console may have printed.
            debugger::flushStandardStreams(ctx, env);
        });
        if (!ran) respondError(req, "evaluate: the program is not stopped");
        else if (!ok) respondError(req, error);
        else respond(req, true, body);
    }

    // --- paths and sources ------------------------------------------------------
    // A client path as the adapter's file system names it.
    std::string toAdapter(const std::string& clientPath) {
        std::lock_guard<std::mutex> lk(srcMu_);
        for (const auto& m : mappings_) {
            std::string rest;
            if (underRoot(clientPath, m.localRoot, windowsLike(m.localRoot), rest)) return joinRoot(m.remoteRoot, rest);
        }
        return clientPath;
    }

    // An adapter path as the client names it; false when no mapping covers it
    // (there are mappings, and the client cannot open the adapter's path).
    bool toClientLocked(const std::string& adapterPath, std::string& out) {
        if (mappings_.empty()) { out = adapterPath; return true; }
        const PathMapping* best = nullptr;
        std::string bestRest;
        for (const auto& m : mappings_) {
            std::string rest;
            if (underRoot(adapterPath, m.remoteRoot, windowsLike(m.remoteRoot), rest)
                    && (!best || slashed(m.remoteRoot).size() > slashed(best->remoteRoot).size())) {
                best = &m;
                bestRest = rest;
            }
        }
        if (!best) return false;
        out = joinRoot(best->localRoot, bestRest);
        return true;
    }

    int refLocked(int textId, const std::string& adapterPath) {
        const std::string k = textId ? "t" + std::to_string(textId) : "f" + adapterPath;
        auto it = refByKey_.find(k);
        if (it != refByKey_.end()) return it->second;
        refs_.push_back(SourceRef{textId, adapterPath});
        const int ref = static_cast<int>(refs_.size());
        refByKey_.emplace(k, ref);
        return ref;
    }

    // The DAP source of a code object: its path (as the client names it) when
    // the client can open it, else a reference the `source` request serves.
    Json sourceFor(const debugger::CodeInfo& code) {
        Json src = Json::object();
        std::lock_guard<std::mutex> lk(srcMu_);
        if (code.textId) {
            src["name"] = Json(code.file.empty() ? std::string("<string>") : code.file);
            src["sourceReference"] = Json(refLocked(code.textId, std::string()));
            src["origin"] = Json("compiled from a string (exec, eval, compile)");
        } else if (code.file.empty() || code.file[0] == '<') {
            src["name"] = Json(code.file);
        } else {
            std::string client;
            if (toClientLocked(code.displayPath, client)) {
                src["name"] = Json(baseName(client));
                src["path"] = Json(client);
            } else {
                src["name"] = Json(baseName(code.displayPath));
                src["sourceReference"] = Json(refLocked(0, code.displayPath));
                src["origin"] = Json(code.displayPath);
            }
        }
        if (code.library) src["presentationHint"] = Json("deemphasize");
        return src;
    }

    // How a traceback text names the code's file.
    std::string clientLabel(const debugger::CodeInfo& code) {
        if (code.file.empty() || code.file[0] == '<') return code.file;
        std::lock_guard<std::mutex> lk(srcMu_);
        std::string client;
        return toClientLocked(code.displayPath, client) ? client : code.displayPath;
    }

    // The breakpoint key and the lines of a request's `source`.
    bool resolveSource(const Json& source, std::string& key, std::vector<std::string>& lines) {
        const long long ref = source.getInt("sourceReference", 0);
        std::string text;
        if (ref > 0) {
            SourceRef r;
            {
                std::lock_guard<std::mutex> lk(srcMu_);
                if (ref > static_cast<long long>(refs_.size())) return false;
                r = refs_[static_cast<std::size_t>(ref - 1)];
            }
            if (r.textId) {
                std::string name;
                if (!debugger::sourceText(r.textId, name, text)) return false;
                key = "<text:" + std::to_string(r.textId) + ">";
            } else {
                readFile(r.adapterPath, text);
                key = debugger::normalizePath(r.adapterPath);
            }
        } else {
            const std::string path = source.getString("path");
            if (path.empty()) return false;
            const std::string adapterPath = toAdapter(path);
            readFile(adapterPath, text);
            key = debugger::normalizePath(adapterPath);
        }
        lines = splitLines(text);
        return true;
    }

    void handleSource(const Json& req, const Json& args) {
        Json source = args.get("source");
        if (args.getInt("sourceReference", 0) > 0) {
            source = Json::object();
            source["sourceReference"] = Json(args.getInt("sourceReference", 0));
        }
        const long long ref = source.getInt("sourceReference", 0);
        std::string text;
        bool ok = false;
        if (ref > 0) {
            SourceRef r;
            {
                std::lock_guard<std::mutex> lk(srcMu_);
                ok = ref <= static_cast<long long>(refs_.size());
                if (ok) r = refs_[static_cast<std::size_t>(ref - 1)];
            }
            if (ok) {
                std::string name;
                ok = r.textId ? debugger::sourceText(r.textId, name, text) : readFile(r.adapterPath, text);
            }
        } else if (!source.getString("path").empty()) {
            ok = readFile(toAdapter(source.getString("path")), text);
        }
        if (!ok) {
            respondError(req, "source: not available");
            return;
        }
        respond(req, true, obj({{"content", Json(text)}, {"mimeType", Json("text/x-python")}}));
    }

    void handleExceptionInfo(const Json& req, const Json& args) {
        Json body;
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto ts = threadLocked(args.getInt("threadId", 1));
            if (ts && ts->stopped && ts->hasException) {
                found = true;
                body = obj({{"exceptionId", Json(ts->excType)},
                            {"description", Json(ts->excMessage)},
                            {"breakMode", Json(ts->excBreakMode)}});
                body["details"] = obj({{"typeName", Json(ts->excType)},
                                       {"message", Json(ts->excMessage)},
                                       {"stackTrace", Json(ts->excTrace)}});
            }
        }
        if (found) respond(req, true, body);
        else respondError(req, "exceptionInfo: the thread is not stopped at an exception");
    }

    // --- outgoing --------------------------------------------------------------
    static Json obj(std::initializer_list<std::pair<const char*, Json>> members) {
        Json j = Json::object();
        for (const auto& m : members) j[m.first] = m.second;
        return j;
    }

    void write(Json msg) {
        std::lock_guard<std::mutex> lk(writeMu_);
        msg["seq"] = Json(seq_++);
        const std::string body = msg.dump();
        std::ostream& out = stdio_.protocolOut();
        out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
        out.flush();
    }

    void respond(const Json& req, bool success, const Json& body) {
        Json r = obj({{"type", Json("response")},
                      {"request_seq", Json(req.getInt("seq"))},
                      {"success", Json(success)},
                      {"command", Json(req.getString("command"))}});
        r["body"] = body;
        write(std::move(r));
    }

    void respondError(const Json& req, const std::string& message) {
        Json r = obj({{"type", Json("response")},
                      {"request_seq", Json(req.getInt("seq"))},
                      {"success", Json(false)},
                      {"command", Json(req.getString("command"))},
                      {"message", Json(message)}});
        r["body"] = obj({{"error", obj({{"id", Json(1)}, {"format", Json(message)}})}});
        write(std::move(r));
    }

    void sendEvent(const char* name, const Json& body) {
        Json e = obj({{"type", Json("event")}, {"event", Json(name)}});
        e["body"] = body;
        write(std::move(e));
    }

    void sendOutput(const std::string& category, const std::string& text) {
        sendEvent("output", obj({{"category", Json(category)}, {"output", Json(text)}}));
    }

    // --- state -------------------------------------------------------------------
    dap::DapStdio stdio_;
    std::mutex writeMu_;
    long long seq_ = 1;
    std::thread reader_;

    std::mutex mu_;
    std::condition_variable cv_;
    bool launched_ = false;
    bool configDone_ = false;
    bool disconnecting_ = false;
    bool readerDone_ = false;
    std::atomic<bool> programDone_{false};
    std::atomic<bool> shuttingDown_{false};
    DAPServer::LaunchRequest launch_;
    std::string programPath_;

    std::mutex bpMu_;
    std::unordered_map<std::string, std::map<int, Breakpoint>> breakpoints_;
    std::string conditionRaised_, conditionUserUnhandled_;

    // Paths and sources (guarded by srcMu_).
    struct SourceRef {
        int textId = 0;           // code compiled from a string
        std::string adapterPath;  // else a file the client cannot open
    };
    std::mutex srcMu_;
    std::vector<PathMapping> mappings_;
    std::vector<SourceRef> refs_;  // sourceReference - 1
    std::unordered_map<std::string, int> refByKey_;
    std::vector<const debugger::CodeInfo*> loaded_;
    std::atomic<bool> anyBreakpoint_{false};
    std::atomic<bool> entryPending_{false};
    std::atomic<bool> breakOnRaised_{false};
    std::atomic<bool> breakOnUserUnhandled_{false};

    // The program's threads and their stops (guarded by mu_). Frame and
    // variable ids are unique across threads: each names its owner.
    std::map<int, std::shared_ptr<ThreadState>> threads_;
    int nextThreadId_ = 2;
    std::unordered_map<int, FrameOwner> frameOwners_;
    int nextFrameId_ = 1;
    std::unordered_map<int, VarRefEntry> varRefs_;
    int nextVarRef_ = 1;
};

} // namespace

int DAPServer::run(const ProgramRunner& runner) {
    // Never destroyed: a daemon thread may still be inside the listener when
    // the process ends.
    static Session* session = new Session();
    return session->run(runner);
}

} // namespace protoPython
