import enum
class FlagBoundary(enum.StrEnum):
    STRICT = enum.auto()
    CONFORM = enum.auto()
    EJECT = enum.auto()
    KEEP = enum.auto()
print("MEMBERS:", FlagBoundary._member_names_)
for x in FlagBoundary:
    print("X:", x)
