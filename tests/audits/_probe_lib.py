"""Shared helpers for SP1 audit probes.

Each probe imports `expect`, calls it many times, then emits the
collected gaps as JSONL on stdout via `emit()`.

The output format is JSONL (one JSON object per line):
- Line 1 is a header: {"module": "...", "format": "jsonl-strings",
                       "total": "N", "passes": "K", "fails": "M"}.
- Each subsequent line is one entry as a JSON dict where ALL VALUES
  ARE STRINGS.  This works around protopy bug #1: `json.dumps(int)`
  returns "<NoneType object at (nil)>".  We stringify every value
  manually, then pass through json.dumps which works correctly for
  strings.  Triage code (python3, not protopy) reads back and treats
  values as strings throughout; integer fields are interpreted at
  read time if needed.
"""
import json
import traceback

_gaps = []


def expect(name, fn, validate=None):
    """Run fn(); record success/failure with full context.

    Args:
        name: stable identifier for the gap (e.g. "doctest.testmod_basic").
        fn: zero-arg callable that exercises the API.
        validate: optional callable(result) -> bool; if provided and returns
            False, the entry is recorded as FAIL with reason "validate".
    """
    try:
        result = fn()
    except Exception as e:
        _gaps.append({
            "id": name,
            "status": "FAIL",
            "exc_type": type(e).__name__,
            "exc_msg": str(e),
            "traceback": traceback.format_exc(),
        })
        return
    if validate is not None:
        try:
            ok = validate(result)
        except Exception as e:
            _gaps.append({
                "id": name,
                "status": "FAIL",
                "exc_type": "ValidateError",
                "exc_msg": "validate raised %s: %s" % (type(e).__name__, e),
                "traceback": traceback.format_exc(),
            })
            return
        if not ok:
            _gaps.append({
                "id": name,
                "status": "FAIL",
                "exc_type": "ValidateMismatch",
                "exc_msg": "result did not satisfy validator: %r" % (result,),
                "traceback": "",
            })
            return
    _gaps.append({
        "id": name,
        "status": "PASS",
        "result": repr(result)[:200],
    })


def _serialize_entry(entry):
    """Build a single-line JSON object from a dict.

    All values are converted to strings via str() before being passed
    to json.dumps — this avoids protopy bug #1 (json.dumps of int).
    Result is a flat JSON object with string-only values.
    """
    parts = []
    for k, v in entry.items():
        parts.append(json.dumps(str(k)) + ":" + json.dumps(str(v)))
    return "{" + ",".join(parts) + "}"


def emit(module_name):
    """Print collected gaps as JSONL on stdout (one entry per line)."""
    passes = sum(1 for g in _gaps if g["status"] == "PASS")
    fails = sum(1 for g in _gaps if g["status"] == "FAIL")
    header = {
        "module": module_name,
        "format": "jsonl-strings",
        "total": str(len(_gaps)),
        "passes": str(passes),
        "fails": str(fails),
    }
    print(_serialize_entry(header))
    for g in _gaps:
        print(_serialize_entry(g))
