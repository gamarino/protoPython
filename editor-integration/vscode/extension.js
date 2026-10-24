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
// install location (when present), else from PATH.
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
    createDebugAdapterDescriptor(/* session */) {
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
