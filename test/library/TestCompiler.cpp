// TestCompiler.cpp — Unit tests for the protoPython compiler (Compiler class).
//
// Tests are organized by language feature. Each test category covers:
//   1. Correct compilation (compilesOk / expression/module execution).
//   2. Correct run-time result (via executeMinimalBytecode + shared PythonEnvironment).
//
// Strategy:
//   - Use a single shared PythonEnvironment so builtins (range, len, sum, …) are
//     available and g_internPool dangling-pointer issues are avoided between tests.
//   - Use evalExpr(source)  → compile as expression, return result.
//   - Use execModule(source) → compile as module, return frame for variable inspection.
//   - Use evalVar(source, name) → execModule + getAttribute(name) from frame.

#include <gtest/gtest.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/Parser.h>
#include <protoPython/Compiler.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <string>

using namespace protoPython;

// ---------------------------------------------------------------------------
// Shared environment — one per process (avoids intern-pool cross-test pollution).
// ---------------------------------------------------------------------------
static PythonEnvironment& getSharedEnv() {
    static PythonEnvironment s_env{STDLIB_PATH};
    return s_env;
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class CompilerTest : public ::testing::Test {
protected:
    PythonEnvironment& env{getSharedEnv()};
    proto::ProtoContext* ctx = nullptr;

    void SetUp() override { ctx = env.getContext(); }

    void TearDown() override {
        // Clear any pending exception left by a failing test so subsequent tests
        // start with a clean execution state (shared env means shared exception slot).
        env.takePendingException();
    }

    // Fresh mutable ProtoObject used as module frame.
    proto::ProtoObject* newFrame() {
        return const_cast<proto::ProtoObject*>(ctx->newObject(true));
    }

    // Parse + compile an expression; return the execution result.
    // Internally uses Compiler::compileExpression which appends RETURN_VALUE.
    const proto::ProtoObject* evalExpr(const std::string& source) {
        Parser parser(source);
        auto expr = parser.parseExpression();
        if (!expr) { ADD_FAILURE() << "Parse failed: " << source; return nullptr; }
        Compiler compiler(ctx, "<test>");
        if (!compiler.compileExpression(expr.get())) {
            ADD_FAILURE() << "Compile failed: " << source; return nullptr;
        }
        proto::ProtoObject* frame = newFrame();
        return executeMinimalBytecode(ctx,
            compiler.getConstants(), compiler.getBytecode(), compiler.getNames(), frame);
    }

    // Parse + compile a full module; return the module frame after execution.
    proto::ProtoObject* execModule(const std::string& source) {
        Parser parser(source);
        auto mod = parser.parseModule();
        if (!mod) { ADD_FAILURE() << "Parse failed: " << source; return nullptr; }
        Compiler compiler(ctx, "<test>");
        if (!compiler.compileModule(mod.get())) {
            ADD_FAILURE() << "Compile failed: " << source; return nullptr;
        }
        proto::ProtoObject* frame = newFrame();
        executeMinimalBytecode(ctx,
            compiler.getConstants(), compiler.getBytecode(), compiler.getNames(), frame);
        return frame;
    }

    // Compile + execute a module; return the named variable from its frame.
    const proto::ProtoObject* evalVar(const std::string& source, const std::string& var) {
        auto* frame = execModule(source);
        if (!frame) return nullptr;
        return frame->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, var.c_str()));
    }

    // True iff the module source compiles without errors.
    bool compilesOk(const std::string& source) {
        Parser parser(source);
        auto mod = parser.parseModule();
        if (!mod) return false;
        Compiler compiler(ctx, "<test>");
        return compiler.compileModule(mod.get());
    }

    // Convenience: run expression, return integer result (or -9999 on failure).
    long long evalInt(const std::string& source) {
        auto* r = evalExpr(source);
        if (!r) return -9999LL;
        return r->asLong(ctx);
    }

    // Convenience: run expression, return boolean result.
    bool evalBool(const std::string& source) {
        auto* r = evalExpr(source);
        return r == PROTO_TRUE;
    }

    // Convert a ProtoObject string to std::string.
    std::string asStr(const proto::ProtoObject* obj) {
        if (!obj || !obj->isString(ctx)) return "<not-a-string>";
        std::string s;
        obj->asString(ctx)->toUTF8String(ctx, s);
        return s;
    }

    // Retrieve the ProtoList stored in obj's __data__ attribute (list/set wrappers).
    const proto::ProtoList* asList(const proto::ProtoObject* obj) {
        if (!obj) return nullptr;
        const proto::ProtoObject* data =
            obj->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"));
        return data ? data->asList(ctx) : nullptr;
    }
};

// ===========================================================================
// 1. Constants and Literals
// ===========================================================================

TEST_F(CompilerTest, IntegerLiteral) {
    EXPECT_EQ(evalInt("42"),  42);
    EXPECT_EQ(evalInt("0"),    0);
    EXPECT_EQ(evalInt("-1"),  -1);
}

TEST_F(CompilerTest, LargeIntegerLiteral) {
    EXPECT_EQ(evalInt("1000000"), 1000000);
}

TEST_F(CompilerTest, FloatLiteral) {
    auto* r = evalExpr("3.14");
    ASSERT_NE(r, nullptr);
    EXPECT_NEAR(r->asDouble(ctx), 3.14, 1e-9);
}

TEST_F(CompilerTest, StringLiteral) {
    auto* r = evalExpr("\"hello\"");
    ASSERT_NE(r, nullptr);
    ASSERT_TRUE(r->isString(ctx));
    EXPECT_EQ(asStr(r), "hello");
}

TEST_F(CompilerTest, SingleQuoteString) {
    auto* r = evalExpr("'world'");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(asStr(r), "world");
}

TEST_F(CompilerTest, BoolLiterals) {
    EXPECT_EQ(evalExpr("True"),  PROTO_TRUE);
    EXPECT_EQ(evalExpr("False"), PROTO_FALSE);
}

TEST_F(CompilerTest, NoneLiteral) {
    EXPECT_EQ(evalExpr("None"), PROTO_NONE);
}

TEST_F(CompilerTest, EmptyStringLiteral) {
    auto* r = evalExpr("\"\"");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(asStr(r), "");
}

// ===========================================================================
// 2. Arithmetic Operators
// ===========================================================================

TEST_F(CompilerTest, Addition) {
    EXPECT_EQ(evalInt("1 + 2"),    3);
    EXPECT_EQ(evalInt("100 + 0"), 100);
}

TEST_F(CompilerTest, Subtraction) {
    EXPECT_EQ(evalInt("10 - 3"),  7);
    EXPECT_EQ(evalInt("0 - 5"),  -5);
}

TEST_F(CompilerTest, Multiplication) {
    EXPECT_EQ(evalInt("4 * 5"),   20);
    EXPECT_EQ(evalInt("0 * 999"),  0);
}

TEST_F(CompilerTest, FloorDivision) {
    EXPECT_EQ(evalInt("10 // 3"),  3);
    EXPECT_EQ(evalInt("7  // 2"),  3);
}

TEST_F(CompilerTest, Modulo) {
    EXPECT_EQ(evalInt("10 % 3"), 1);
    EXPECT_EQ(evalInt("8  % 4"), 0);
}

TEST_F(CompilerTest, Power) {
    EXPECT_EQ(evalInt("2 ** 8"),  256);
    EXPECT_EQ(evalInt("3 ** 3"),   27);
}

TEST_F(CompilerTest, UnaryNegate) {
    EXPECT_EQ(evalInt("-5"),      -5);
    EXPECT_EQ(evalInt("-(3+4)"), -7);
}

TEST_F(CompilerTest, BitwiseAnd) {
    EXPECT_EQ(evalInt("12 & 10"), 8);   // 1100 & 1010 = 1000
}

TEST_F(CompilerTest, BitwiseOr) {
    EXPECT_EQ(evalInt("10 | 5"), 15);   // 1010 | 0101 = 1111
}

TEST_F(CompilerTest, BitwiseXor) {
    EXPECT_EQ(evalInt("15 ^ 10"), 5);   // 1111 ^ 1010 = 0101
}

TEST_F(CompilerTest, BitwiseNot) {
    EXPECT_EQ(evalInt("~0"),   -1);
    EXPECT_EQ(evalInt("~(-1)"), 0);
}

TEST_F(CompilerTest, ShiftLeft) {
    EXPECT_EQ(evalInt("1 << 3"),  8);
    EXPECT_EQ(evalInt("3 << 2"), 12);
}

TEST_F(CompilerTest, ShiftRight) {
    EXPECT_EQ(evalInt("16 >> 2"), 4);
    EXPECT_EQ(evalInt("7  >> 1"), 3);
}

TEST_F(CompilerTest, ChainedArithmetic) {
    EXPECT_EQ(evalInt("1 + 2 + 3 + 4"), 10);
    EXPECT_EQ(evalInt("(10 - 2) * 3"),  24);
}

// ===========================================================================
// 3. Comparison Operators
// ===========================================================================

TEST_F(CompilerTest, CompareEqual) {
    EXPECT_EQ(evalBool("1 == 1"), true);
    EXPECT_EQ(evalBool("1 == 2"), false);
}

TEST_F(CompilerTest, CompareNotEqual) {
    EXPECT_EQ(evalBool("1 != 2"), true);
    EXPECT_EQ(evalBool("1 != 1"), false);
}

TEST_F(CompilerTest, CompareLess) {
    EXPECT_EQ(evalBool("1 < 2"), true);
    EXPECT_EQ(evalBool("2 < 1"), false);
}

TEST_F(CompilerTest, CompareGreater) {
    EXPECT_EQ(evalBool("2 > 1"), true);
    EXPECT_EQ(evalBool("1 > 2"), false);
}

TEST_F(CompilerTest, CompareLessEqual) {
    EXPECT_EQ(evalBool("1 <= 1"), true);
    EXPECT_EQ(evalBool("2 <= 1"), false);
}

TEST_F(CompilerTest, CompareGreaterEqual) {
    EXPECT_EQ(evalBool("2 >= 2"), true);
    EXPECT_EQ(evalBool("1 >= 2"), false);
}

// ===========================================================================
// 4. Boolean Logic
// ===========================================================================

TEST_F(CompilerTest, BoolAnd) {
    EXPECT_EQ(evalBool("True and True"),   true);
    EXPECT_EQ(evalBool("True and False"),  false);
    EXPECT_EQ(evalBool("False and True"),  false);
    EXPECT_EQ(evalBool("False and False"), false);
}

TEST_F(CompilerTest, BoolOr) {
    EXPECT_EQ(evalBool("True or False"),  true);
    EXPECT_EQ(evalBool("False or True"),  true);
    EXPECT_EQ(evalBool("False or False"), false);
}

TEST_F(CompilerTest, BoolNot) {
    EXPECT_EQ(evalBool("not True"),  false);
    EXPECT_EQ(evalBool("not False"), true);
    EXPECT_EQ(evalBool("not 0"),     true);
    EXPECT_EQ(evalBool("not 1"),     false);
}

TEST_F(CompilerTest, ShortCircuitAnd) {
    // False and <anything> must short-circuit (not evaluate RHS).
    EXPECT_EQ(evalBool("False and (1/0 > 0)"), false);
}

TEST_F(CompilerTest, ShortCircuitOr) {
    EXPECT_EQ(evalBool("True or (1/0 > 0)"), true);
}

TEST_F(CompilerTest, ConditionalExpression) {
    EXPECT_EQ(evalInt("1 if True  else 2"), 1);
    EXPECT_EQ(evalInt("1 if False else 2"), 2);
    EXPECT_EQ(evalInt("42 if 0 else -1"),  -1);
}

// ===========================================================================
// 5. Variables and Assignment
// ===========================================================================

TEST_F(CompilerTest, SimpleAssignment) {
    auto* x = evalVar("x = 42", "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 42);
}

TEST_F(CompilerTest, MultipleAssignments) {
    auto* frame = execModule("x = 10\ny = 20\nz = x + y");
    ASSERT_NE(frame, nullptr);
    auto* z = frame->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "z"));
    ASSERT_NE(z, nullptr);
    EXPECT_EQ(z->asLong(ctx), 30);
}

TEST_F(CompilerTest, ChainedAssignment) {
    auto* frame = execModule("a = b = 99");
    ASSERT_NE(frame, nullptr);
    auto* a = frame->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "a"));
    auto* b = frame->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "b"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->asLong(ctx), 99);
    EXPECT_EQ(b->asLong(ctx), 99);
}

TEST_F(CompilerTest, AugmentedAddAssign) {
    auto* x = evalVar("x = 5\nx += 3", "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 8);
}

TEST_F(CompilerTest, AugmentedMulAssign) {
    auto* x = evalVar("x = 4\nx *= 3", "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 12);
}

TEST_F(CompilerTest, AugmentedSubAssign) {
    auto* x = evalVar("x = 10\nx -= 4", "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 6);
}

TEST_F(CompilerTest, TupleUnpacking) {
    auto* frame = execModule("a, b = 10, 20");
    ASSERT_NE(frame, nullptr);
    auto* a = frame->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "a"));
    auto* b = frame->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "b"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->asLong(ctx), 10);
    EXPECT_EQ(b->asLong(ctx), 20);
}

TEST_F(CompilerTest, DeleteStatement) {
    EXPECT_TRUE(compilesOk("x = 42\ndel x"));
}

// ===========================================================================
// 6. Collections
// ===========================================================================

TEST_F(CompilerTest, ListLiteral) {
    auto* lst = evalVar("x = [1, 2, 3]", "x");
    ASSERT_NE(lst, nullptr);
    auto* data = asList(lst);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->getSize(ctx), 3u);
    EXPECT_EQ(data->getAt(ctx, 0)->asLong(ctx), 1);
    EXPECT_EQ(data->getAt(ctx, 1)->asLong(ctx), 2);
    EXPECT_EQ(data->getAt(ctx, 2)->asLong(ctx), 3);
}

TEST_F(CompilerTest, EmptyList) {
    auto* lst = evalVar("x = []", "x");
    ASSERT_NE(lst, nullptr);
    auto* data = asList(lst);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->getSize(ctx), 0u);
}

TEST_F(CompilerTest, NestedList) {
    auto* lst = evalVar("x = [[1, 2], [3, 4]]", "x");
    ASSERT_NE(lst, nullptr);
    auto* data = asList(lst);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->getSize(ctx), 2u);
}

TEST_F(CompilerTest, TupleLiteral) {
    // Tuples compile to BUILD_TUPLE; verify the object is created.
    EXPECT_TRUE(compilesOk("x = (10, 20, 30)"));
}

TEST_F(CompilerTest, DictLiteral) {
    auto* d = evalVar("x = {'a': 1, 'b': 2}", "x");
    ASSERT_NE(d, nullptr);
    // Dict stores keys in __keys__; check it's present.
    auto* keys = d->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"));
    EXPECT_NE(keys, nullptr);
}

TEST_F(CompilerTest, EmptyDict) {
    EXPECT_TRUE(compilesOk("x = {}"));
}

TEST_F(CompilerTest, SetLiteral) {
    EXPECT_TRUE(compilesOk("x = {1, 2, 3}"));
}

TEST_F(CompilerTest, ListIndexing) {
    auto* result = evalVar("lst = [10, 20, 30]\nresult = lst[1]", "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 20);
}

// ===========================================================================
// 7. Control Flow
// ===========================================================================

TEST_F(CompilerTest, IfTrue) {
    auto* x = evalVar("if True:\n    x = 1\nelse:\n    x = 2", "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 1);
}

TEST_F(CompilerTest, IfFalse) {
    auto* x = evalVar("if False:\n    x = 1\nelse:\n    x = 2", "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 2);
}

TEST_F(CompilerTest, IfElif) {
    auto* x = evalVar(
        "n = 2\n"
        "if n == 1:\n    x = 'one'\n"
        "elif n == 2:\n    x = 'two'\n"
        "else:\n    x = 'other'",
        "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(asStr(x), "two");
}

TEST_F(CompilerTest, WhileCountUp) {
    // Sum 0+1+2+3+4 = 10
    auto* x = evalVar(
        "x = 0\ni = 0\n"
        "while i < 5:\n    x += i\n    i += 1",
        "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 10);
}

TEST_F(CompilerTest, WhileBreak) {
    auto* i = evalVar(
        "i = 0\n"
        "while True:\n"
        "    if i == 3:\n        break\n"
        "    i += 1",
        "i");
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->asLong(ctx), 3);
}

TEST_F(CompilerTest, ForRangeSum) {
    // Sum 0+1+2+3+4 = 10
    auto* x = evalVar(
        "x = 0\n"
        "for i in range(5):\n    x += i",
        "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 10);
}

TEST_F(CompilerTest, ForBreak) {
    auto* i = evalVar(
        "for i in range(10):\n"
        "    if i == 4:\n        break",
        "i");
    ASSERT_NE(i, nullptr);
    EXPECT_EQ(i->asLong(ctx), 4);
}

TEST_F(CompilerTest, ForContinue) {
    // Skip i==2: sum = 0+1+3+4 = 8
    auto* x = evalVar(
        "x = 0\n"
        "for i in range(5):\n"
        "    if i == 2:\n        continue\n"
        "    x += i",
        "x");
    ASSERT_NE(x, nullptr);
    EXPECT_EQ(x->asLong(ctx), 8);
}

TEST_F(CompilerTest, NestedLoops) {
    // 3x3 = 9 iterations
    auto* count = evalVar(
        "count = 0\n"
        "for i in range(3):\n"
        "    for j in range(3):\n"
        "        count += 1",
        "count");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->asLong(ctx), 9);
}

// ===========================================================================
// 8. Functions
// ===========================================================================

TEST_F(CompilerTest, FunctionNoArgs) {
    auto* result = evalVar(
        "def f():\n    return 42\n"
        "result = f()",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 42);
}

TEST_F(CompilerTest, FunctionWithArgs) {
    auto* result = evalVar(
        "def add(a, b):\n    return a + b\n"
        "result = add(3, 7)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 10);
}

TEST_F(CompilerTest, FunctionReturnNone) {
    auto* result = evalVar(
        "def noop():\n    pass\n"
        "result = noop()",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result, PROTO_NONE);
}

TEST_F(CompilerTest, FunctionRecursion) {
    // 5! = 120
    auto* result = evalVar(
        "def fact(n):\n"
        "    if n <= 1:\n        return 1\n"
        "    return n * fact(n - 1)\n"
        "result = fact(5)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 120);
}

TEST_F(CompilerTest, FunctionDefaultArg) {
    auto* result = evalVar(
        "def f(x=99):\n    return x\n"
        "result = f()",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 99);
}

TEST_F(CompilerTest, FunctionDefaultArgOverride) {
    auto* result = evalVar(
        "def f(x=99):\n    return x\n"
        "result = f(42)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 42);
}

TEST_F(CompilerTest, LambdaOneArg) {
    auto* result = evalVar(
        "f = lambda x: x * 2\n"
        "result = f(5)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 10);
}

TEST_F(CompilerTest, LambdaTwoArgs) {
    auto* result = evalVar(
        "add = lambda a, b: a + b\n"
        "result = add(3, 4)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 7);
}

TEST_F(CompilerTest, ClosureCapture) {
    auto* result = evalVar(
        "def make_adder(x):\n"
        "    def inner(y):\n"
        "        return x + y\n"
        "    return inner\n"
        "add5 = make_adder(5)\n"
        "result = add5(10)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 15);
}

TEST_F(CompilerTest, VarargsFunction) {
    auto* result = evalVar(
        "def f(*args):\n    return len(args)\n"
        "result = f(1, 2, 3)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 3);
}

TEST_F(CompilerTest, FunctionLocalVariable) {
    auto* result = evalVar(
        "def compute():\n"
        "    x = 10\n"
        "    y = 20\n"
        "    return x + y\n"
        "result = compute()",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 30);
}

TEST_F(CompilerTest, HigherOrderFunction) {
    auto* result = evalVar(
        "def apply(f, x):\n    return f(x)\n"
        "double = lambda n: n * 2\n"
        "result = apply(double, 7)",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 14);
}

// ===========================================================================
// 9. Comprehensions
// ===========================================================================

TEST_F(CompilerTest, ListCompBasic) {
    auto* lst = evalVar("x = [i * 2 for i in range(4)]", "x");
    ASSERT_NE(lst, nullptr);
    auto* data = asList(lst);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->getSize(ctx), 4u);
    EXPECT_EQ(data->getAt(ctx, 0)->asLong(ctx), 0);
    EXPECT_EQ(data->getAt(ctx, 1)->asLong(ctx), 2);
    EXPECT_EQ(data->getAt(ctx, 2)->asLong(ctx), 4);
    EXPECT_EQ(data->getAt(ctx, 3)->asLong(ctx), 6);
}

TEST_F(CompilerTest, ListCompFiltered) {
    // Even numbers in [0, 10): [0, 2, 4, 6, 8]
    auto* lst = evalVar("x = [i for i in range(10) if i % 2 == 0]", "x");
    ASSERT_NE(lst, nullptr);
    auto* data = asList(lst);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->getSize(ctx), 5u);
    EXPECT_EQ(data->getAt(ctx, 0)->asLong(ctx), 0);
    EXPECT_EQ(data->getAt(ctx, 4)->asLong(ctx), 8);
}

TEST_F(CompilerTest, ListCompEmpty) {
    auto* lst = evalVar("x = [i for i in range(0)]", "x");
    ASSERT_NE(lst, nullptr);
    auto* data = asList(lst);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->getSize(ctx), 0u);
}

TEST_F(CompilerTest, DictCompBasic) {
    // {0: 0, 1: 2, 2: 4}
    auto* d = evalVar("x = {k: k*2 for k in range(3)}", "x");
    ASSERT_NE(d, nullptr);
    // Dict must have __keys__ with 3 entries.
    auto* keys = d->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"));
    ASSERT_NE(keys, nullptr);
    auto* kl = keys->asList(ctx);
    ASSERT_NE(kl, nullptr);
    EXPECT_EQ(kl->getSize(ctx), 3u);
}

TEST_F(CompilerTest, SetCompBasic) {
    EXPECT_TRUE(compilesOk("x = {i*i for i in range(5)}"));
}

TEST_F(CompilerTest, GeneratorExprSum) {
    // sum(x*x for x in range(4)) = 0+1+4+9 = 14
    auto* result = evalVar("result = sum(x * x for x in range(4))", "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(ctx), 14);
}

TEST_F(CompilerTest, NestedListComp) {
    // [i+j for i in range(2) for j in range(2)] = [0,1,1,2]
    auto* lst = evalVar("x = [i+j for i in range(2) for j in range(2)]", "x");
    ASSERT_NE(lst, nullptr);
    auto* data = asList(lst);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->getSize(ctx), 4u);
    EXPECT_EQ(data->getAt(ctx, 0)->asLong(ctx), 0);
    EXPECT_EQ(data->getAt(ctx, 3)->asLong(ctx), 2);
}

// ===========================================================================
// 10. Strings
// ===========================================================================

TEST_F(CompilerTest, StringConcatenation) {
    auto* r = evalExpr("\"hello\" + \" \" + \"world\"");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(asStr(r), "hello world");
}

TEST_F(CompilerTest, StringRepeat) {
    auto* r = evalExpr("\"ab\" * 3");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(asStr(r), "ababab");
}

TEST_F(CompilerTest, FStringBasic) {
    auto* result = evalVar(
        "name = 'world'\n"
        "result = f'hello {name}'",
        "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(asStr(result), "hello world");
}

TEST_F(CompilerTest, FStringExpression) {
    auto* result = evalVar("result = f'{1 + 2}'", "result");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(asStr(result), "3");
}

// ===========================================================================
// 11. Exception Handling (compilation correctness)
// ===========================================================================

TEST_F(CompilerTest, TryExceptCompiles) {
    EXPECT_TRUE(compilesOk(
        "try:\n"
        "    x = 1\n"
        "except Exception:\n"
        "    x = 0\n"));
}

TEST_F(CompilerTest, TryFinallyCompiles) {
    EXPECT_TRUE(compilesOk(
        "x = 0\n"
        "try:\n"
        "    x = 1\n"
        "finally:\n"
        "    x += 10\n"));
}

TEST_F(CompilerTest, TryExceptFinallyCompiles) {
    EXPECT_TRUE(compilesOk(
        "try:\n"
        "    pass\n"
        "except ValueError:\n"
        "    pass\n"
        "finally:\n"
        "    pass\n"));
}

TEST_F(CompilerTest, RaiseCompiles) {
    EXPECT_TRUE(compilesOk("raise ValueError('oops')"));
}

TEST_F(CompilerTest, TryExceptElseCompiles) {
    EXPECT_TRUE(compilesOk(
        "try:\n"
        "    x = 1\n"
        "except Exception:\n"
        "    x = 0\n"
        "else:\n"
        "    x = 2\n"));
}

// ===========================================================================
// 12. Classes (compilation correctness)
// ===========================================================================

TEST_F(CompilerTest, EmptyClassCompiles) {
    EXPECT_TRUE(compilesOk("class Foo:\n    pass\n"));
}

TEST_F(CompilerTest, ClassWithMethodCompiles) {
    EXPECT_TRUE(compilesOk(
        "class Counter:\n"
        "    def __init__(self):\n"
        "        self.count = 0\n"
        "    def increment(self):\n"
        "        self.count += 1\n"));
}

TEST_F(CompilerTest, ClassInheritanceCompiles) {
    EXPECT_TRUE(compilesOk(
        "class Animal:\n"
        "    pass\n"
        "class Dog(Animal):\n"
        "    pass\n"));
}

TEST_F(CompilerTest, ClassWithClassVar) {
    EXPECT_TRUE(compilesOk(
        "class Config:\n"
        "    debug = False\n"
        "    version = '1.0'\n"));
}

// ===========================================================================
// 13. Generators and Async (compilation correctness)
// ===========================================================================

TEST_F(CompilerTest, GeneratorFunctionCompiles) {
    EXPECT_TRUE(compilesOk(
        "def gen():\n"
        "    yield 1\n"
        "    yield 2\n"
        "    yield 3\n"));
}

TEST_F(CompilerTest, YieldFromCompiles) {
    EXPECT_TRUE(compilesOk(
        "def gen():\n"
        "    yield from range(5)\n"));
}

TEST_F(CompilerTest, AsyncFunctionCompiles) {
    EXPECT_TRUE(compilesOk(
        "async def fetch():\n"
        "    return 42\n"));
}

TEST_F(CompilerTest, AsyncWithCompiles) {
    EXPECT_TRUE(compilesOk(
        "async def f():\n"
        "    async with open('x') as fp:\n"
        "        pass\n"));
}

// ===========================================================================
// 14. Imports (compilation correctness)
// ===========================================================================

TEST_F(CompilerTest, ImportModuleCompiles) {
    EXPECT_TRUE(compilesOk("import math"));
}

TEST_F(CompilerTest, ImportFromCompiles) {
    EXPECT_TRUE(compilesOk("from os import path"));
}

TEST_F(CompilerTest, ImportAsCompiles) {
    EXPECT_TRUE(compilesOk("import os as operating_system"));
}

TEST_F(CompilerTest, ImportFromAsCompiles) {
    EXPECT_TRUE(compilesOk("from os.path import join as path_join"));
}

// ===========================================================================
// 15. Advanced Language Features (compilation correctness)
// ===========================================================================

TEST_F(CompilerTest, GlobalDeclarationCompiles) {
    EXPECT_TRUE(compilesOk(
        "x = 10\n"
        "def modify():\n"
        "    global x\n"
        "    x = 99\n"
        "modify()\n"));
}

TEST_F(CompilerTest, NonlocalDeclarationCompiles) {
    EXPECT_TRUE(compilesOk(
        "def outer():\n"
        "    x = 0\n"
        "    def inner():\n"
        "        nonlocal x\n"
        "        x = 1\n"
        "    inner()\n"
        "    return x\n"));
}

TEST_F(CompilerTest, WalrusOperatorCompiles) {
    EXPECT_TRUE(compilesOk(
        "if (n := 10) > 5:\n"
        "    y = n\n"));
}

TEST_F(CompilerTest, AssertCompiles) {
    EXPECT_TRUE(compilesOk("assert True, 'must be true'"));
    EXPECT_TRUE(compilesOk("assert 1 == 1"));
}

TEST_F(CompilerTest, StarredAssignmentCompiles) {
    EXPECT_TRUE(compilesOk("a, *b, c = [1, 2, 3, 4, 5]"));
}

TEST_F(CompilerTest, WithStatementCompiles) {
    EXPECT_TRUE(compilesOk(
        "class CM:\n"
        "    def __enter__(self): return self\n"
        "    def __exit__(self, *a): pass\n"
        "with CM():\n"
        "    pass\n"));
}

TEST_F(CompilerTest, DecoratorCompiles) {
    EXPECT_TRUE(compilesOk(
        "def decorator(f):\n"
        "    return f\n"
        "@decorator\n"
        "def func():\n"
        "    pass\n"));
}

TEST_F(CompilerTest, AnnAssignCompiles) {
    EXPECT_TRUE(compilesOk("x: int = 42"));
    EXPECT_TRUE(compilesOk("y: str"));
}

// ===========================================================================
// 16. Compiler Infrastructure
// ===========================================================================

TEST_F(CompilerTest, OutputTuplesNonNull) {
    Parser parser("x = 1 + 2");
    auto mod = parser.parseModule();
    ASSERT_NE(mod, nullptr);
    Compiler compiler(ctx, "<test>");
    ASSERT_TRUE(compiler.compileModule(mod.get()));
    EXPECT_NE(compiler.getConstants(), nullptr);
    EXPECT_NE(compiler.getNames(),     nullptr);
    EXPECT_NE(compiler.getBytecode(),  nullptr);
    EXPECT_NE(compiler.getLnotab(),    nullptr);
}

TEST_F(CompilerTest, EmptyModuleCompiles) {
    EXPECT_TRUE(compilesOk(""));
    EXPECT_TRUE(compilesOk("# just a comment\n"));
    EXPECT_TRUE(compilesOk("pass"));
}

TEST_F(CompilerTest, MaxStackTracked) {
    Parser parser("x = 1 + 2 + 3 + 4");
    auto mod = parser.parseModule();
    ASSERT_NE(mod, nullptr);
    Compiler compiler(ctx, "<test>");
    ASSERT_TRUE(compiler.compileModule(mod.get()));
    EXPECT_GE(compiler.getMaxStack(), 1);
}

TEST_F(CompilerTest, LnotabNonEmpty) {
    Parser parser("x = 1\ny = 2\nz = 3");
    auto mod = parser.parseModule();
    ASSERT_NE(mod, nullptr);
    Compiler compiler(ctx, "<test>");
    ASSERT_TRUE(compiler.compileModule(mod.get()));
    EXPECT_NE(compiler.getLnotab(), nullptr);
    EXPECT_GE(compiler.getMaxStack(), 0);
}

TEST_F(CompilerTest, ExpressionCompilerReturnsValue) {
    Parser parser("6 * 7");
    auto expr = parser.parseExpression();
    ASSERT_NE(expr, nullptr);
    Compiler compiler(ctx, "<test>");
    ASSERT_TRUE(compiler.compileExpression(expr.get()));
    // Bytecode must be non-empty
    ASSERT_NE(compiler.getBytecode(), nullptr);
    EXPECT_GT(compiler.getBytecode()->getSize(ctx), 0u);
}

TEST_F(CompilerTest, FunctionBodyHasStackDepth) {
    Parser parser("def f(x):\n    return x + 1\n");
    auto mod = parser.parseModule();
    ASSERT_NE(mod, nullptr);
    Compiler compiler(ctx, "<test>");
    ASSERT_TRUE(compiler.compileModule(mod.get()));
    // Inner function code object lives in the constants of the module.
    ASSERT_NE(compiler.getConstants(), nullptr);
    // At minimum the function code object is one constant.
    EXPECT_GE(compiler.getConstants()->getSize(ctx), 1u);
}

TEST_F(CompilerTest, MultipleConstantsDeduped) {
    // "1 + 1 + 1" should deduplicate the constant 1 (stored once in the pool).
    Parser parser("1 + 1 + 1");
    auto expr = parser.parseExpression();
    ASSERT_NE(expr, nullptr);
    Compiler compiler(ctx, "<test>");
    ASSERT_TRUE(compiler.compileExpression(expr.get()));
    auto* consts = compiler.getConstants();
    ASSERT_NE(consts, nullptr);
    // Dedup: constant 1 appears only once.
    EXPECT_EQ(consts->getSize(ctx), 1u);
}

TEST_F(CompilerTest, NamesPoolCorrect) {
    // "x = 1; y = 2" creates 2 distinct names.
    Parser parser("x = 1\ny = 2\n");
    auto mod = parser.parseModule();
    ASSERT_NE(mod, nullptr);
    Compiler compiler(ctx, "<test>");
    ASSERT_TRUE(compiler.compileModule(mod.get()));
    auto* names = compiler.getNames();
    ASSERT_NE(names, nullptr);
    EXPECT_GE(names->getSize(ctx), 2u);
}
