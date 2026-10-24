// Minimal protoPython debug adapter glue for VS Code.
//
// The protopy executable itself IS the debug adapter: `protopy --dap` speaks the
// Debug Adapter Protocol over stdin/stdout. This extension only registers the
// `protopy` debug type for Python files and tells VS Code how to spawn it.
//
// It coexists with the Python / Python Debugger (debugpy) extensions: Python
// files keep their language, grammar and default debugger. The provider below is
// registered for the `protopy` type only, so VS Code calls it only for a
// configuration of that type, or for F5 without a launch.json after the user
// picked "protoPython (protopy)" in the debugger list.
//
// protopy is taken from the `protopy.path` setting if set, else from the default
// install location (when present), else from PATH. With a `remote` attribute
// the adapter runs through that command instead (ssh, wsl, docker exec, ...):
// `<command...> <remote.path> --dap`. Everything about paths (pathMappings,
// files the client cannot open) is done by the adapter itself.
const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const os = require("os");

function defaultInstallPath() {
  if (process.platform === "win32") {
    const base = process.env.LOCALAPPDATA || path.join(os.homedir(), "AppData", "Local");
    return path.join(base, "Programs", "proto", "bin", "protopy.exe");
  }
  return path.join(os.homedir(), ".local", "bin", "protopy");
}

function protopyCommand() {
  const configured = vscode.workspace.getConfiguration("protopy").get("path");
  if (configured) return configured;
  const installed = defaultInstallPath();
  return fs.existsSync(installed) ? installed : "protopy";
}

const configurationProvider = {
  // F5 with no launch.json, once protopy was chosen: debug the active file.
  resolveDebugConfiguration(folder, config) {
    if (!config.type && !config.request && !config.name) {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === "python") {
        config.type = "protopy";
        config.name = "protopy: current file";
        config.request = "launch";
        config.program = "${file}";
        config.cwd = "${fileDirname}";
      }
    }
    if (config.remote !== undefined) {
      const cmd = config.remote && config.remote.command;
      if (!Array.isArray(cmd) || cmd.length === 0 || cmd.some((a) => typeof a !== "string")) {
        return vscode.window
          .showErrorMessage('protoPython: "remote.command" must be a non-empty list of strings, e.g. ["ssh", "-T", "me@host"].')
          .then(() => undefined);
      }
      if (!Array.isArray(config.pathMappings) || config.pathMappings.length === 0) {
        return vscode.window
          .showErrorMessage(
            'protoPython: a "remote" launch needs "pathMappings": [{"localRoot": "${workspaceFolder}", ' +
              '"remoteRoot": "<the same folder on the remote side>"}], so breakpoints and the call stack ' +
              "name files on both sides. \"program\" and \"cwd\" are remote paths (or local ones under a localRoot)."
          )
          .then(() => undefined);
      }
    }
    if (!config.program) {
      return vscode.window
        .showInformationMessage("protoPython: open a Python file or set 'program' in launch.json.")
        .then(() => undefined);
    }
    return config;
  },

  // "Run and Debug" → "Show all automatic debug configurations".
  provideDebugConfigurations() {
    return [
      {
        type: "protopy",
        request: "launch",
        name: "protopy: current file",
        program: "${file}",
        cwd: "${fileDirname}",
      },
    ];
  },
};

function activate(context) {
  const factory = {
    createDebugAdapterDescriptor(session) {
      const remote = session.configuration && session.configuration.remote;
      if (remote && Array.isArray(remote.command) && remote.command.length > 0) {
        const [command, ...prefix] = remote.command;
        return new vscode.DebugAdapterExecutable(command, [...prefix, remote.path || "protopy", "--dap"]);
      }
      return new vscode.DebugAdapterExecutable(protopyCommand(), ["--dap"]);
    },
  };
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory("protopy", factory),
    vscode.debug.registerDebugConfigurationProvider("protopy", configurationProvider),
    vscode.debug.registerDebugConfigurationProvider(
      "protopy",
      { provideDebugConfigurations: configurationProvider.provideDebugConfigurations },
      vscode.DebugConfigurationProviderTriggerKind.Dynamic
    )
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
