# Re-export _collections_abc as collections.abc for "import collections.abc" before collections is loaded.
from _collections_abc import *
from _collections_abc import __all__
