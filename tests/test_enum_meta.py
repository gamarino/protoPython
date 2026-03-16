import enum
import dis
print("EnumMeta.__new__ bytecode:")
dis.dis(enum.EnumMeta.__new__)
