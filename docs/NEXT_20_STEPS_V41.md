# protoPython: Next 20 Steps (v41)

Plan for 20 incremental milestones. Each step: implement → update docs → commit.

**Prerequisite**: Next 20 Steps (v40) completed (steps 725–744). See [NEXT_20_STEPS_V40.md](NEXT_20_STEPS_V40.md).

**Status**: Done.

---

## Steps 745–764

| Step | Description | Status |
|------|-------------|--------|
| 745 | Create NEXT_20_STEPS_V41.md | done |
| 746–747 | struct: calcsize for supported formats | done |
| 748–750 | pathlib.Path: mkdir, exists, is_file, is_dir, read_text, write_text | deferred (native Path requires C++ fs API) |
| 751 | io.TextIOWrapper: minimal | deferred |
| 752 | shutil.move: copy then unlink | done |
| 753 | shutil.rmtree: recursive delete | done |
| 754–755 | tempfile.TemporaryFile, NamedTemporaryFile enhancements | deferred |
| 756–758 | Foundation tests PathlibMkdirExists, StructCalcsize, ShutilMove | done |
| 759–764 | STUBS, todo, IMPLEMENTATION_PLAN, commit | done |

---

## Summary

Steps 745–764 will deliver: struct.calcsize; pathlib.Path enhancements; shutil.move and rmtree; tempfile enhancements; foundation tests; STUBS, todo, IMPLEMENTATION_PLAN updated; single commit.
