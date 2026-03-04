class my_local(object):
    def __new__(cls, *args, **kw):
        print("in new, cls:", cls)
        try:
            print("cls.__init__:", cls.__init__)
        except AttributeError as e:
            print("AttributeError on cls.__init__:", e)
            import traceback
            traceback.print_exc()

l = my_local()
