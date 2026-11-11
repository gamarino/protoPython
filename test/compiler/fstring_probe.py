# f-string fields in a module built by protopyc: conversion and format spec
# (the generated code ignored both before round 10 of 1.2.2).
class F(float):
    def __format__(self, spec):
        return 'F<' + spec + '>'


def fields(x, s, n):
    return f'{x:.2f}|{s:>5}|{n:#x}|{n:08b}|{x!r}|{s!r}|{x}|{0.1 + 0.2}|{F(1.5)}|{F(1.5):x}|{n:{"0"}{6}}|{x=}'
