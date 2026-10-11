# Drives the part of exceptions_with_probe where the interpreter differs from
# CPython (test/compiler/protopyc_exceptions_with.sh): the output of the
# compiled module must equal exceptions_with_cpython.expected, CPython 3's
# output on the same source.
import exceptions_with_probe as m

for name, fn in (("return_in_handler", m.return_in_handler),
                 ("break_in_handler", m.break_in_handler),
                 ("unmatched_with_finally", m.unmatched_with_finally),
                 ("handler_raises_with_finally", m.handler_raises_with_finally),
                 ("else_raises_with_finally", m.else_raises_with_finally)):
    del m.LOG[:]
    print(name, fn(), m.LOG)
print("END")
