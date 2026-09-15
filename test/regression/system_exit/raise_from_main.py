# The common script idiom: main()'s return value becomes the exit status
# (expected: 5).


def main():
    return 5


if __name__ == "__main__":
    raise SystemExit(main())
