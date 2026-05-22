import sys, traceback, unittest, test_grammar
class R(unittest.TextTestResult):
    def addError(self, t, e):
        print("E:", t, "->", e[0].__name__, str(e[1]))
        super().addError(t, e)
    def addFailure(self, t, e):
        print("F:", t, "->", e[0].__name__, str(e[1]))
        super().addFailure(t, e)

class Run(unittest.TextTestRunner):
    resultclass = R

suite = unittest.TestLoader().loadTestsFromName('test_funcdef', test_grammar.GrammarTests)
Run(verbosity=0, stream=sys.stdout).run(suite)
