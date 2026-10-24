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
#include <deque>
#include <filesystem>
#include <fstream>
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

// --- the session ---------------------------------------------------------------

enum class Step { None, In, Over, Out };

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
        if (!reason && isLine && anyBreakpoint_.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(bpMu_);
            auto it = breakpoints_.find(code.path);
            if (it != breakpoints_.end() && it->second.count(line)) reason = "breakpoint";
        }
        if (!reason) return;
        ts->step.store(Step::None);
        hold(ctx, env, ts, reason, depth, Json::object());
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
        const char* mode = nullptr;
        if (raised) mode = "always";
        else if (event.handledInUserCode && !event.handledInUserCode()) mode = "userUnhandled";
        if (!mode) return;
        std::string description = d.message.empty() ? d.typeName : d.typeName + ": " + d.message;
        if (!event.origin.empty()) description += "  (raised in " + event.origin + ")";
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
                    trace += "  File \"" + it->path + "\", line " + std::to_string(it->line) + ", in " + it->name + "\n";
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
                              {"description", Json("Stop where an exception is raised in your code, or reaches it "
                                                   "from a library, even when it is handled.")},
                              {"default", Json(false)}}));
            filters.push(obj({{"filter", Json("userUnhandled")},
                              {"label", Json("Uncaught Exceptions")},
                              {"description", Json("Stop where an exception is raised when no try/except of your "
                                                   "code on that thread handles it.")},
                              {"default", Json(true)}}));
            Json caps = obj({{"supportsConfigurationDoneRequest", Json(true)},
                             {"supportsEvaluateForHovers", Json(true)},
                             {"supportsTerminateRequest", Json(true)},
                             {"supportsExceptionInfoRequest", Json(true)}});
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
        launch.program = args.getString("program");
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
        launch.cwd = args.getString("cwd");
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
    static std::vector<int> codeLines(const std::string& path, const std::vector<int>& requested) {
        std::vector<std::string> lines;
        std::ifstream f(u8Path(path), std::ios::binary);
        std::string l;
        while (f && std::getline(f, l)) lines.push_back(l);
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
        const std::string path = args.get("source").getString("path");
        std::vector<int> requested;
        const Json& bps = args.get("breakpoints");
        for (std::size_t i = 0; i < bps.size(); ++i) requested.push_back(static_cast<int>(bps.at(i).getInt("line")));
        if (bps.size() == 0) {
            const Json& lines = args.get("lines");
            for (std::size_t i = 0; i < lines.size(); ++i)
                if (lines.at(i).isNumber()) requested.push_back(static_cast<int>(lines.at(i).num()));
        }
        const std::vector<int> actual = codeLines(path, requested);
        {
            std::lock_guard<std::mutex> lk(bpMu_);
            const std::string key = debugger::normalizePath(path);
            if (actual.empty()) breakpoints_.erase(key);
            else breakpoints_[key] = std::set<int>(actual.begin(), actual.end());
            anyBreakpoint_.store(!breakpoints_.empty());
        }
        Json result = Json::array();
        for (std::size_t i = 0; i < actual.size(); ++i)
            result.push(obj({{"verified", Json(true)}, {"line", Json(actual[i])}}));
        respond(req, true, obj({{"breakpoints", result}}));
    }

    void handleSetExceptionBreakpoints(const Json& req, const Json& args) {
        std::vector<std::string> filters;
        const Json& f = args.get("filters");
        for (std::size_t i = 0; i < f.size(); ++i)
            if (f.at(i).isString()) filters.push_back(f.at(i).str());
        const Json& fo = args.get("filterOptions");
        for (std::size_t i = 0; i < fo.size(); ++i) filters.push_back(fo.at(i).getString("filterId"));
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
                    if (!f.path.empty() && f.path[0] != '<') {
                        const auto slash = f.path.find_last_of("/\\");
                        frame["source"] = obj({{"name", Json(slash == std::string::npos ? f.path : f.path.substr(slash + 1))},
                                               {"path", Json(f.path)}});
                    }
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
    std::unordered_map<std::string, std::set<int>> breakpoints_;
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
