def subgen():
    yield "a"
    yield "b"
    return "done"

def main_gen():
    res = yield from subgen()
    yield res
    yield "c"

print("Starting test...")
g = main_gen()

lit_a = "a"
print("Literal a:", lit_a)

for i in range(4):
    try:
        val = next(g)
        print("Val:", val)
        if i == 0:
            print("Val == 'a'?", val == "a")
    except StopIteration:
        print("Fin")
        break
