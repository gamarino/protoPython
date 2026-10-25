// The per-language half of the extension's integration tests: everything the
// shared runner (runTest.js) and suite (suite/extension.test.js) need to know
// about this runtime. The other files under test/ are identical in protoST,
// protoJS, protoPython, protoScala and ProtoClojure; only this file and the
// fixture differ.
module.exports = {
  debugType: "protopy",
  exe: "protopy",
  extensionId: "protopython-placeholder.protopy-debug",
  // The configuration section of the `<section>.path` setting.
  settingSection: "protopy",
  languageId: "python",
  fixture: "fixture.py",
  // A line inside `twice` where the local below is already assigned.
  breakpointLine: 3,
  local: { name: "doubled", value: "42" },
  // Where the `raised` exception filter stops (the `raise` line).
  exceptionLine: 7,
  // Printed by the fixture before the breakpoint and at its end.
  stdoutBefore: "e2e-before",
  stdoutAfter: "e2e-after",
  // Built-in extensions that also debug this language (F5 would ask which).
  disableBuiltins: [],
  // The Linux build used by the remote (WSL) test.
  linuxExe: "/home/gamarino/proto-linux/pp-inst/bin/protopy",
  linuxLibPath: "/home/gamarino/proto-linux/pp-inst/lib:/home/gamarino/proto-linux/prefix/lib",
};
