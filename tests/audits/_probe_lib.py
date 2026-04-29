"""Shared helpers for SP1 audit probes.

Each probe imports `expect`, calls it many times, then emits the
collected gaps as JSON on stdout via `emit()`.
"""
import json
import traceback
import sys

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


def emit(module_name):
    """Print collected gaps as JSON, suitable for redirecting to a file."""
    out = {
        "module": module_name,
        "total": len(_gaps),
        "passes": sum(1 for g in _gaps if g["status"] == "PASS"),
        "fails": sum(1 for g in _gaps if g["status"] == "FAIL"),
        "entries": _gaps,
    }
    print(json.dumps(out, indent=2))
