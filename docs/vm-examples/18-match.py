def kind(value):
    match value:
        case 0:
            return "zero"
        case [x, y]:
            return "pair"
        case _:
            return "other"
