/**
 * `protopy --dap`: a Debug Adapter Protocol server (include/protoPython/DAPServer.h).
 *
 * Threads:
 *  - The calling (main) thread waits for `launch` + `configurationDone`, then
 *    runs the program through the runner, with line events enabled
 *    (include/protoPython/Debugger.h). When a line event stops it (breakpoint,
 *    step, pause, entry), it parks in hold(), inside an unmanaged region of its
 *    context, and wakes for two reasons: a resume request, or an inspection
 *    task (variables, evaluate) it runs itself, on its own context, so every
 *    ProtoObject is touched by the thread that owns it.
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

struct FrameSnap {
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

enum class Step { None, In, Over, Out, Pause };

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
            debugger::setListener(nullptr);
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

    // --- debugger::Listener: on the program's thread -------------------------
    void onLine(proto::ProtoContext* ctx, PythonEnvironment* env, const debugger::CodeInfo& code,
                int line, debugger::LineEventKind kind, std::size_t depth) override {
        const Step st = step_.load(std::memory_order_acquire);
        const bool entry = entryPending_.load(std::memory_order_relaxed);
        if (st == Step::None && !entry && !anyBreakpoint_.load(std::memory_order_relaxed)) return;
        const bool isLine = kind == debugger::LineEventKind::Line;

        const char* reason = nullptr;
        if (entry && isLine && code.path == programPath_) {
            entryPending_.store(false);
            reason = "entry";
        }
        if (!reason) {
            const std::size_t base = stepDepth_.load(std::memory_order_relaxed);
            switch (st) {
                case Step::In:    if (isLine || depth < base) reason = "step"; break;
                case Step::Over:  if (isLine ? depth <= base : depth < base) reason = "step"; break;
                case Step::Out:   if (depth < base) reason = "step"; break;
                case Step::Pause: reason = "pause"; break;
                case Step::None:  break;
            }
        }
        if (!reason && isLine && anyBreakpoint_.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lk(bpMu_);
            auto it = breakpoints_.find(code.path);
            if (it != breakpoints_.end() && it->second.count(line)) reason = "breakpoint";
        }
        if (!reason) return;
        step_.store(Step::None);
        hold(ctx, env, reason, depth);
    }

private:
    // --- holding the program's thread ----------------------------------------
    void hold(proto::ProtoContext* ctx, PythonEnvironment* env, const char* reason, std::size_t depth) {
        // What the script printed so far reaches the console before the stop.
        debugger::flushStandardStreams(ctx, env);
        std::vector<debugger::StackEntry> stack;
        debugger::currentStack(stack);
        std::vector<FrameSnap> frames;
        for (const auto& e : stack)
            frames.push_back(FrameSnap{e.code->name, e.code->displayPath, e.line, e});
        {
            std::lock_guard<std::mutex> lk(mu_);
            frames_ = std::move(frames);
            varRefs_.clear();
            nextVarRef_ = 1;
            resume_ = false;
            stopped_ = true;
            stopDepth_ = depth;
            stopEnv_ = env;
            stopCtx_ = ctx;
        }
        sendEvent("stopped", obj({{"reason", Json(reason)}, {"threadId", Json(1)}, {"allThreadsStopped", Json(true)}}));

        for (;;) {
            std::function<void()> task;
            {
                // Parked for as long as the user takes: out of the collector's
                // quorum. The lock is released before the region ends.
                proto::ProtoContext::UnmanagedScope unmanaged(ctx);
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [&] { return resume_ || !tasks_.empty(); });
                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }
            }
            if (!task) break;
            task();
        }
        pins_.release();
        std::lock_guard<std::mutex> lk(mu_);
        stopped_ = false;
        frames_.clear();
        varRefs_.clear();
        stopEnv_ = nullptr;
        stopCtx_ = nullptr;
    }

    // Runs `fn(ctx, env)` on the held thread and waits for it. False when the
    // program is not held.
    bool onProgramThread(const std::function<void(proto::ProtoContext*, PythonEnvironment*)>& fn) {
        auto done = std::make_shared<bool>(false);
        std::unique_lock<std::mutex> lk(mu_);
        if (!stopped_ || resume_) return false;
        tasks_.push_back([this, fn, done] {
            proto::ProtoContext* ctx;
            PythonEnvironment* env;
            {
                std::lock_guard<std::mutex> g(mu_);
                ctx = stopCtx_;
                env = stopEnv_;
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
            Json caps = obj({{"supportsConfigurationDoneRequest", Json(true)},
                             {"supportsEvaluateForHovers", Json(true)},
                             {"supportsTerminateRequest", Json(true)}});
            respond(req, true, caps);
            sendEvent("initialized", Json::object());
        } else if (command == "launch") {
            handleLaunch(req, args);
        } else if (command == "attach") {
            respondError(req, "protopy --dap supports 'launch' only");
        } else if (command == "setBreakpoints") {
            handleSetBreakpoints(req, args);
        } else if (command == "setExceptionBreakpoints" || command == "setFunctionBreakpoints") {
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
            threads.push(obj({{"id", Json(1)}, {"name", Json("MainThread")}}));
            respond(req, true, obj({{"threads", threads}}));
        } else if (command == "continue" || command == "next" || command == "stepIn" || command == "stepOut") {
            handleResume(req, command);
        } else if (command == "pause") {
            step_.store(Step::Pause);
            respond(req, true, Json::object());
        } else if (command == "stackTrace") {
            handleStackTrace(req, args);
        } else if (command == "scopes") {
            handleScopes(req, args);
        } else if (command == "variables") {
            handleVariables(req, args);
        } else if (command == "evaluate") {
            handleEvaluate(req, args);
        } else if (command == "disconnect" || command == "terminate") {
            respond(req, true, Json::object());
            {
                std::lock_guard<std::mutex> lk(mu_);
                disconnecting_ = true;
            }
            cv_.notify_all();
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

    void handleResume(const Json& req, const std::string& command) {
        Step st = Step::None;
        if (command == "next") st = Step::Over;
        else if (command == "stepIn") st = Step::In;
        else if (command == "stepOut") st = Step::Out;
        {
            std::lock_guard<std::mutex> lk(mu_);
            stepDepth_.store(stopDepth_);
            step_.store(st);
            resume_ = true;
        }
        cv_.notify_all();
        respond(req, true, command == "continue" ? obj({{"allThreadsContinued", Json(true)}}) : Json::object());
    }

    void handleStackTrace(const Json& req, const Json& args) {
        const long long start = args.getInt("startFrame", 0);
        const long long levels = args.getInt("levels", 0);
        Json frames = Json::array();
        long long total;
        {
            std::lock_guard<std::mutex> lk(mu_);
            total = static_cast<long long>(frames_.size());
            for (long long i = start; i < total; ++i) {
                if (levels > 0 && static_cast<long long>(frames.size()) >= levels) break;
                const FrameSnap& f = frames_[static_cast<std::size_t>(i)];
                Json frame = obj({{"id", Json(i + 1)}, {"name", Json(f.name)}, {"line", Json(f.line)}, {"column", Json(1)}});
                if (!f.path.empty() && f.path[0] != '<') {
                    const auto slash = f.path.find_last_of("/\\");
                    frame["source"] = obj({{"name", Json(slash == std::string::npos ? f.path : f.path.substr(slash + 1))},
                                           {"path", Json(f.path)}});
                }
                frames.push(std::move(frame));
            }
        }
        respond(req, true, obj({{"stackFrames", frames}, {"totalFrames", Json(total)}}));
    }

    int newVarRef(VarRef r) {
        const int id = nextVarRef_++;
        varRefs_.emplace(id, r);
        return id;
    }

    void handleScopes(const Json& req, const Json& args) {
        const long long frameId = args.getInt("frameId", 0);
        Json scopes = Json::array();
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (frameId >= 1 && frameId <= static_cast<long long>(frames_.size())) {
                const std::size_t idx = static_cast<std::size_t>(frameId - 1);
                scopes.push(obj({{"name", Json("Locals")},
                                 {"presentationHint", Json("locals")},
                                 {"variablesReference", Json(newVarRef(VarRef{VarRef::Locals, idx, 0}))},
                                 {"expensive", Json(false)}}));
                if (frames_[idx].name != "<module>")
                    scopes.push(obj({{"name", Json("Globals")},
                                     {"variablesReference", Json(newVarRef(VarRef{VarRef::Globals, idx, 0}))},
                                     {"expensive", Json(false)}}));
            }
        }
        respond(req, true, obj({{"scopes", scopes}}));
    }

    // Variables of the held thread's values: on that thread.
    Json describe(PythonEnvironment* env, const debugger::Value& v, bool withName) {
        int ref = 0;
        if (v.expandable && v.object) {
            const std::size_t pin = pins_.add(env, v.object);
            std::lock_guard<std::mutex> lk(mu_);
            ref = newVarRef(VarRef{VarRef::Object, 0, pin});
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
        debugger::StackEntry frame{};
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = varRefs_.find(static_cast<int>(ref));
            if (it != varRefs_.end()) {
                target = it->second;
                if (target->kind != VarRef::Object) {
                    if (target->frameIndex < frames_.size()) frame = frames_[target->frameIndex].entry;
                    else target.reset();
                }
            }
        }
        Json vars = Json::array();
        if (target) {
            onProgramThread([&](proto::ProtoContext* ctx, PythonEnvironment* env) {
                std::vector<debugger::Value> values;
                switch (target->kind) {
                    case VarRef::Locals:  debugger::frameLocals(ctx, env, frame, values); break;
                    case VarRef::Globals: debugger::frameGlobals(ctx, env, frame, values); break;
                    case VarRef::Object:  debugger::children(ctx, env, pins_.get(target->pin), values); break;
                }
                for (const auto& v : values) vars.push(describe(env, v, true));
            });
        }
        respond(req, true, obj({{"variables", vars}}));
    }

    void handleEvaluate(const Json& req, const Json& args) {
        const std::string expr = args.getString("expression");
        const long long frameId = args.getInt("frameId", 1);
        const std::string context = args.getString("context");
        std::optional<debugger::StackEntry> frame;
        {
            std::lock_guard<std::mutex> lk(mu_);
            const long long idx = frameId >= 1 ? frameId - 1 : 0;
            if (idx < static_cast<long long>(frames_.size())) frame = frames_[static_cast<std::size_t>(idx)].entry;
        }
        if (!frame) {
            respondError(req, "evaluate: the program is not stopped");
            return;
        }
        bool ok = false;
        Json body;
        std::string error;
        const bool ran = onProgramThread([&](proto::ProtoContext* ctx, PythonEnvironment* env) {
            debugger::Value v;
            ok = debugger::evaluate(ctx, env, *frame, expr, context == "repl", v, error);
            if (ok) body = describe(env, v, false);
            // A statement run in the console may have printed.
            debugger::flushStandardStreams(ctx, env);
        });
        if (!ran) respondError(req, "evaluate: the program is not stopped");
        else if (!ok) respondError(req, error);
        else respond(req, true, body);
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
    DAPServer::LaunchRequest launch_;
    std::string programPath_;

    std::mutex bpMu_;
    std::unordered_map<std::string, std::set<int>> breakpoints_;
    std::atomic<bool> anyBreakpoint_{false};

    std::atomic<Step> step_{Step::None};
    std::atomic<std::size_t> stepDepth_{0};
    std::atomic<bool> entryPending_{false};

    // A stop (guarded by mu_).
    bool stopped_ = false;
    bool resume_ = false;
    std::size_t stopDepth_ = 0;
    std::vector<FrameSnap> frames_;
    std::deque<std::function<void()>> tasks_;
    std::unordered_map<int, VarRef> varRefs_;
    int nextVarRef_ = 1;
    PythonEnvironment* stopEnv_ = nullptr;
    proto::ProtoContext* stopCtx_ = nullptr;
    debugger::Pins pins_;  // the held thread's only
};

} // namespace

int DAPServer::run(const ProgramRunner& runner) {
    Session session;
    return session.run(runner);
}

} // namespace protoPython
