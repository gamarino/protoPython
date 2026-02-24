import _contextvars as contextvars

var = contextvars.ContextVar('var', default=42)
print("Initial:", var.get())
tok = var.set(100)
print("Set to 100:", var.get())
var.reset(tok)
print("Reset:", var.get())

def f():
    print("In run:", var.get())
    var.set(200)
    print("In run (changed):", var.get())

ctx = contextvars.copy_context()
print("Run in ctx:")
ctx.run(f)
print("After run:", var.get())
print("OK")
