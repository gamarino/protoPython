"""Importing a submodule first loads its package, whose __init__ imports that submodule."""
import asyncio.base_events
import asyncio
import json.decoder

assert asyncio.base_events.BaseEventLoop.__name__ == "BaseEventLoop"
assert asyncio.BaseEventLoop is asyncio.base_events.BaseEventLoop
assert "BaseEventLoop" in asyncio.__all__ and asyncio.base_events is asyncio.base_events
assert json.decoder.JSONDecoder is json.JSONDecoder and json.loads("[1]") == [1]
print("submodule import first OK")
