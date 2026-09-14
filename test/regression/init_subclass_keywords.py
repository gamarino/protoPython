# Regression test: object.__init_subclass__ takes no keyword arguments.
#
# Class keywords that no __init_subclass__ consumes reach
# object.__init_subclass__, which ends every super() chain and must raise
# TypeError.  protoPython's object.__init_subclass__ dropped them silently.


def expect_type_error(label, fn, name):
    try:
        fn()
    except TypeError as exc:
        expected = name + ".__init_subclass__() takes no keyword arguments"
        assert str(exc) == expected, "%s: %r" % (label, str(exc))
    else:
        raise AssertionError(label + ": no TypeError")


def plain():
    class Bad(flag=1):
        pass


expect_type_error("class statement", plain, "plain.<locals>.Bad")


class Base:
    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)


def forwarded():
    class Bad2(Base, other=2):
        pass


expect_type_error("forwarded by super()", forwarded, "forwarded.<locals>.Bad2")
expect_type_error("type() keywords", lambda: type("T", (), {}, flag=1), "T")

# Consumed keywords, metaclass= and keyword-free chains still work.
seen = []


class Consumer:
    def __init_subclass__(cls, flag=None, **kwargs):
        super().__init_subclass__(**kwargs)
        seen.append(flag)


class Ok(Consumer, flag=3):
    pass


class Meta(type):
    pass


class WithMeta(Consumer, metaclass=Meta):
    pass


type("Plain", (), {})
assert seen == [3, None], seen
print("init subclass keywords OK")
