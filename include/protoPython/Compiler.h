#ifndef PROTOPYTHON_COMPILER_H
#define PROTOPYTHON_COMPILER_H

#include <protoPython/Parser.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace protoPython {
    
constexpr int CO_OPTIMIZED = 1;
constexpr int CO_NEWLOCALS = 2;
constexpr int CO_VARARGS = 4;
constexpr int CO_VARKEYWORDS = 8;
constexpr int CO_NESTED = 16;

/** Compiles AST to protoPython bytecode (constants list, names list, flat bytecode). */
class Compiler {
public:
    Compiler(proto::ProtoContext* ctx, const std::string& filename);

    /** Compile an expression (eval mode). Returns true on success; use getConstants(), getNames(), getBytecode(). */
    bool compileExpression(ASTNode* expr);
    /** Compile a module (exec mode). Returns true on success. */
    bool compileModule(ModuleNode* mod);
    /** compile(..., "single"): expression statements outside functions and
     *  classes pass their value to sys.displayhook instead of dropping it. */
    void setInteractive(bool on) { interactive_ = on; }

    /** Attribute write groups (tryCompileAttrGroup): a run of
     *  `p.a = x; p.b = y` statements published as one new version of `p`.
     *  On by default; PROTOPY_ATTR_GROUPS=off in the environment turns them
     *  off, and setAttrWriteGroups changes it for code compiled afterwards
     *  (the tests compare both paths in one process). */
    static void setAttrWriteGroups(bool on);
    static bool attrWriteGroups();

    const proto::ProtoTuple* getConstants();
    const proto::ProtoTuple* getNames();
    const proto::ProtoTuple* getBytecode();
    const proto::ProtoTuple* getLnotab();
    int getFirstLine() const { return firstLine_; }

private:
    proto::ProtoContext* ctx_ = nullptr;
    std::string filename_;
    const proto::ProtoList* constantsVec_ = nullptr;
    const proto::ProtoList* namesVec_ = nullptr;
    const proto::ProtoList* bytecodeVec_ = nullptr;
    std::unordered_map<std::string, int> namesIndex_;
    std::unordered_map<long long, int> constIntIndex_;
    std::unordered_map<double, int> constFloatIndex_;
    std::unordered_map<std::string, int> constStrIndex_;

    // Result holders for get*() methods
    const proto::ProtoTuple* constants_ = nullptr;
    const proto::ProtoTuple* names_ = nullptr;
    const proto::ProtoTuple* bytecode_ = nullptr;

    int addConstant(const proto::ProtoObject* obj);
    int addName(const std::string& name);
    void emit(int op, int arg = 0);

    /** Sprint-11 peephole specialiser — rewrites accumulator-loop
        sequences into fused single-dispatch opcodes.  Returns the
        rewritten ProtoList; if no rewrite applied, returns the input
        unchanged. */
    const proto::ProtoList* specialiseBytecode(const proto::ProtoList* in);

    bool compileNode(ASTNode* node);
    bool compileConstant(ConstantNode* n);
    bool compileName(NameNode* n, bool pushNull = false);
    bool compileBinOp(BinOpNode* n);
    bool compileUnaryOp(UnaryOpNode* n);
    bool compileCall(CallNode* n);
    bool compileAttribute(AttributeNode* n, bool pushNull = false);
    bool compileSubscript(SubscriptNode* n);
    bool compileSlice(SliceNode* n);
    bool compileListLiteral(ListLiteralNode* n);
    bool compileDictLiteral(DictLiteralNode* n);
    bool compileTupleLiteral(TupleLiteralNode* n);
    bool compileSetLiteral(SetLiteralNode* n);
    /** Detect "perhaps you missed a comma?" patterns in a sequence
        element and emit a SyntaxWarning.  Returns true if the warning
        was promoted to a SyntaxError (filter='error'). */
    bool warnIfMissedComma(ASTNode* elem);
    bool compileAssign(AssignNode* n);
    bool compileAnnAssign(AnnAssignNode* n);
    bool compileNamedExpr(NamedExprNode* n);
    bool compileAugAssign(AugAssignNode* n);
    bool compileStarred(StarredNode* n);
    bool compileDeleteNode(DeleteNode* n);
    bool compileAssert(AssertNode* n);
    bool compileListComp(ListCompNode* n);
    bool compileDictComp(DictCompNode* n);
    bool compileSetComp(SetCompNode* n);
    bool compileGeneratorExp(GeneratorExpNode* n);
    bool compileLambda(LambdaNode* n);
    bool compileJoinedStr(JoinedStrNode* n);
    bool compileFormattedValue(FormattedValueNode* n);
    bool compileWhile(WhileNode* n);
    bool compileFor(ForNode* n);
    bool compileBreak(BreakNode* n);
    bool compileContinue(ContinueNode* n);
    bool compileIf(IfNode* n);
    bool compileGlobal(GlobalNode* n);
    bool compileNonlocal(NonlocalNode* n);
    bool compileReturn(ReturnNode* n);
    bool compileYield(YieldNode* n);
    bool compileImport(ImportNode* n);
    bool compileImportFrom(ImportFromNode* n);
    bool compileTry(TryNode* n);
    bool compileActiveExceptionClause(ASTNode* body, int baseStack, bool reraiseAfter);
    bool compileRaise(RaiseNode* n);
    bool compileWith(WithNode* n);
    bool compileFunctionDef(FunctionDefNode* n);
    bool compileAsyncFunctionDef(AsyncFunctionDefNode* n);
    bool compileClassDef(ClassDefNode* n);
    bool compileAwait(AwaitNode* n);
    bool compileAsyncFor(AsyncForNode* n);
    bool compileAsyncWith(AsyncWithNode* n);
    bool compileCondExpr(ConditionalExprNode* n);
    bool compileTypeAlias(TypeAliasNode* n);
    /** PEP 695: a class or def with type parameters, compiled through an
     *  annotation scope (see the section comment in Compiler.cpp). */
    bool compileGenericClassDef(ClassDefNode* n);
    bool compileGenericFunctionDef(FunctionDefNode* n);
    /** The shared body of compileGenericFunctionDef, for `def` and `async def`. */
    template <class DefNode> bool compileGenericDef(DefNode* n);
    /** Compile the annotation scope `scope`, call it with `args` evaluated in
     *  the current scope, and leave the result on the stack. */
    bool emitAnnotationScopeCall(FunctionDefNode* scope,
        const std::vector<std::unique_ptr<ASTNode>>& args);
    /** match/case (PEP 634); the layout is described in Compiler.cpp.
     *  Every intermediate value lives on the operand stack. */
    bool compileMatch(MatchNode* n);
    /** The state of one pattern test: `onTop` counts the values the case has
     *  pushed (the candidate included) that a failing test must pop, and
     *  `fails` records each failing jump (instruction index, values to pop). */
    struct PatternState {
        int onTop = 0;
        std::vector<std::pair<int, int>> fails;
    };
    /** Pre: the candidate is on top of the stack, counted in ps.onTop.
     *  Success: the candidate is consumed (ps.onTop - 1) and the pattern's
     *  names are bound.  Failure: a jump recorded in ps.fails.  Answers false
     *  for an invalid pattern (a SyntaxError). */
    bool compilePattern(MatchPatternNode* pat, PatternState& ps);
    /** Emits `jumpOp` to the fail label for the values now on the stack. */
    void emitPatternFailJump(int jumpOp, PatternState& ps);
    /** Emits the fail labels of `ps`: one POP_TOP per value, falling through
     *  to the label that pops none, where the depth is `baseDepth`. */
    void emitPatternFailBlock(PatternState& ps, int baseDepth);
    /** [.. c] -> [.. c fn(c, extra...)], `fn` a built-in looked up by name,
     *  `extraArgs` emitting `nExtra` further arguments (or null). */
    bool emitCallOnCandidate(const char* fn, const std::function<bool()>& extraArgs, int nExtra);
    bool compileComprehension(const std::vector<Comprehension>& generators, size_t index, std::function<bool()> innerBody);
    bool compileComprehensionScope(const char* scopeName, std::vector<Comprehension>& generators,
                                   const std::vector<ASTNode*>& exprs, bool isGenExpr,
                                   const std::function<void(Compiler&)>& emitStart,
                                   const std::function<bool(Compiler&, int)>& emitElement,
                                   const std::function<void(Compiler&)>& emitEnd);
    bool compileWithItems(const std::vector<WithItem>& items, size_t index, ASTNode* body);
    bool compileSuite(SuiteNode* n);
    enum class TargetCtx { Load, Store, Delete };
    bool compileTarget(ASTNode* target, TargetCtx ctx);
    bool emitNameOp(const std::string& id, TargetCtx ctx, bool pushNull = false);
    /** True if executing this node leaves a value on the stack (expression stmt). */
    static bool statementLeavesValue(ASTNode* node);
    /** See setInteractive(). */
    bool interactive_ = false;
    /** Drop the value `stmt` left on the stack: POP_TOP, or in interactive
     *  mode, for an expression statement, sys.displayhook(value). */
    void emitStatementValueEnd(ASTNode* stmt);
    /** Collect names from function body: globals from GlobalNode, locals (ordered) from Name/Assign. */
    static void collectLocalsFromBody(ASTNode* body,
        std::unordered_set<std::string>& globalsOut,
        std::unordered_set<std::string>& nonlocalsOut,
        std::vector<std::string>& localsOrdered);
    /** Collect names that are free in any nested def (captured by closures). */
    static void collectCapturedNames(ASTNode* body,
        const std::unordered_set<std::string>& globalsInScope,
        std::unordered_set<std::string>& capturedOut);
    /** True if body contains locals(), exec(), or eval()—dynamic locals access that forces MAPPED. */
    static bool hasDynamicLocalsAccess(ASTNode* node);
    std::unordered_set<std::string> globalNames_;
    std::unordered_set<std::string> nonlocalNames_;
    /** Optional: name -> slot index for LOAD_FAST/STORE_FAST. Set when compiling a function body.
     *  Empty when forceMapped_ is true — in that mode every local lives on the frame as an
     *  attribute and is accessed via LOAD_NAME/STORE_NAME instead. */
    std::unordered_map<std::string, int> localSlotMap_;
    /** Names that the enclosing function defines as locals/parameters, *regardless* of
     *  whether they live in CO_OPTIMIZED slots or on the frame.  Nested-scope compilers
     *  consult this set (in addition to localSlotMap_) when deciding whether a name is
     *  one of the enclosing function's variables, which they then read through a cell
     *  (LOAD_DEREF / STORE_DEREF) — needed when the outer is forceMapped (its
     *  localSlotMap_ is empty) or the variable is a cell variable (not in
     *  localSlotMap_ either). */
    std::unordered_set<std::string> definedLocals_;
    bool isGenerator_ = false;
    bool isClassBody_ = false;
    bool isFunctionScope_ = false;
    bool isAsyncFunction_ = false;  // PC2: tracking for yield-from check (PEP 525)
    bool forceMapped_ = false;
    /** Mangled names of the function's parameters (positional, keyword-only,
     *  *args, **kwargs): bound at entry, so a write group may read them. Empty
     *  outside a `def` body. */
    std::unordered_set<std::string> boundParams_;
    /** Attribute write groups. When statements[i] starts a run of two or more
     *  `p.name = value` statements on the same parameter `p`, where every value
     *  after the first can neither run code nor raise, emits the run with
     *  OP_STORE_ATTR_GROUP / OP_STORE_ATTR_GROUP_END and answers the number of
     *  statements consumed; otherwise emits nothing and answers 0. */
    size_t tryCompileAttrGroup(SuiteNode* n, size_t i);
    /** `p.name = value` with a single target, `p` a parameter held in a fast
     *  slot: the receiver's mangled name and the attribute's mangled name. */
    bool attrGroupStatement(ASTNode* stmt, std::string& receiver, std::string& attr) const;
    /** A value a run may compute after its first statement: a constant, a
     *  negated numeric constant, a parameter (its slot is appended to
     *  `paramSlots`; the engine checks it is bound), and list, tuple and
     *  dict displays of such values (dict keys constants). */
    bool attrGroupInertValue(ASTNode* e, std::vector<int>& paramSlots) const;
    bool attrGroupParamSlot(const std::string& rawName, int& slot) const;
    /** Captured docstring of the function body, if its first statement is a
     *  bare string literal. compileFunctionDef reads this after compiling
     *  the body to stamp `co_doc` on the resulting code object so
     *  `fn.__doc__` resolves correctly. Empty string ⇒ no docstring. */
    std::string capturedDocstring_;
    /** The name of the immediately enclosing class, propagated to method compilers
     *  so that zero-argument super() can be rewritten to super(ClassName, self). */
    std::string currentClassName_;
    /** Apply CPython's PEP 8 private-name mangling to `raw` when this
     *  compiler is inside a class body (or a method nested in one).
     *  Names of the form `__name` (two-or-more leading underscores, NOT
     *  ending in two underscores) become `_<ClassName>__name`, with
     *  leading underscores of ClassName stripped.  All other names
     *  (including `__dunder__` names) are returned unchanged. */
    std::string mangleIdentifier(const std::string& raw) const;
    /** PI: the dotted qualified-name prefix for nested definitions
     *  ("Outer", "Outer.Inner", etc.).  compileClassDef uses this to
     *  emit __qualname__ on class objects. */
    std::string qualnamePrefix_;
    /** PEP 695: set just before compileFunctionDef compiles an annotation
     *  scope, which is then left on the stack instead of stored. */
    bool leaveDefOnStack_ = false;
    /** True in the body compiler of a PEP 695 annotation scope sitting
     *  directly in a class body: the def it wraps is a method and keeps the
     *  class name for zero-arg super() and private-name mangling. */
    bool classAnnotationScope_ = false;
    /** True in a method (a def or lambda directly in a class body) and in
     *  every scope nested in one: a free `__class__` there is the implicit
     *  class cell, read with LOAD_DEREF from the class namespace, where
     *  BUILD_CLASS stores the class it creates. */
    bool classCellScope_ = false;
    // ---- Closures: cells, free variables, recursive nested defs -----------
    //
    // A variable a nested scope uses lives in a cell (PythonEnvironment::
    // newCell).  Each function-like scope lays out its fast slots as
    //     [co_varnames ...][co_freevars ...][operand stack ...]
    // A cell variable (a local or parameter some nested scope captures)
    // keeps its cell in its own co_varnames slot; a free variable keeps the
    // cell taken from the function's closure in the slot after co_varnames.
    // LOAD_DEREF / STORE_DEREF / DELETE_DEREF take that slot; LOAD_CLOSURE
    // pushes the cell itself to build a nested function's closure tuple.
    // A class body has no co_varnames: its free variables come first and its
    // `__class__` cell (for the methods' zero-argument super()) after them.

    /** Slot of every name this scope reads through a cell: its own cell
     *  variables and its free variables. */
    std::unordered_map<std::string, int> derefSlot_;
    /** The names among derefSlot_ that are this scope's own cell variables. */
    std::unordered_set<std::string> cellVars_;
    /** A recursive nested def's own name: the function binds it to itself in
     *  its free-variable slot at entry instead of closing over the defining
     *  scope's cell, which would close a cycle (cell -> function -> closure
     *  -> cell) that protoCore never collects.  Read with LOAD_FAST, or with
     *  LOAD_DEREF when a scope nested in the function captures the name too
     *  (selfRefCell_: the entry then makes a cell holding the function). */
    std::string selfRefName_;
    int selfRefSlot_ = -1;
    bool selfRefCell_ = false;
    /** Names a nested scope asked LOAD_CLOSURE for that this scope holds in
     *  no cell; the scope that compiles this one recompiles it with them. */
    std::unordered_set<std::string> missingClosure_;
    /** Every name a nested scope asked LOAD_CLOSURE for. */
    std::unordered_set<std::string> requestedClosure_;
    bool missingSelfCell_ = false;
    /** Function scope: its first parameter (the receiver zero-argument
     *  super() passes), or empty. */
    std::string firstParam_;
    /** Class body: the slot of its `__class__` cell, and whether a method
     *  (or a scope nested in one) closed over it. */
    int classCellSlot_ = -1;
    bool classCellUsed_ = false;
    /** Class body: names it binds (read with LOAD_NAME, not LOAD_CLASSDEREF),
     *  and names it declares nonlocal. */
    std::unordered_set<std::string> classBound_;
    std::unordered_set<std::string> classNonlocal_;
    /** Function scope: its body and parameter names, consulted when a def
     *  in it may bind its own name (recursiveDefSelfBinds). */
    ASTNode* scopeBody_ = nullptr;
    std::unordered_set<std::string> scopeParams_;
    /** co_argcount (positional parameters) and co_posonlyargcount, read by
     *  the argument binding and by inspect. */
    const proto::ProtoObject* stampArgCounts(const proto::ProtoObject* codeObj, int nPositional, int nPosOnly);
    /** Push the cell holding `name` (LOAD_CLOSURE) for a closure tuple. */
    void emitLoadClosure(const std::string& name);
    /** Push the cells of a nested scope whose co_freevars are `freevars`,
     *  leaving out `selfRef` (a recursive def's own name), and answer the
     *  OP_BUILD_FUNCTION argument bits that count them. */
    int emitClosureCells(const std::vector<std::string>& freevars, const std::string& selfRef);
    /** True when `def <n->name>` in this function scope may bind its own
     *  name to itself at entry: the def is the only binding of that name
     *  anywhere in the scope, it is undecorated, not generic and not inside
     *  a loop, so inside the function the name always denotes the function. */
    template <class DefNode> bool recursiveDefSelfBinds(DefNode* n) const;
    /** Configure `body` (the compiler of a function-like scope whose fast
     *  locals are `varnames`) to read `cellVars` and `freeVars` through
     *  cells, `selfRef` (if not empty) through its free-variable slot. */
    static void configureClosureSlots(Compiler& body, const std::vector<std::string>& varnames,
                                      const std::unordered_set<std::string>& cellVars,
                                      const std::vector<std::string>& freeVars,
                                      const std::string& selfRef, bool selfRefCell);
    /** Stamp the closure layout on a code object: co_freevars, co_cellvars,
     *  co_cellslots (slots made into cells at entry), co_selfref (index in
     *  co_freevars, or absent) and co_nlocalsplus (slots before the operand
     *  stack). */
    const proto::ProtoObject* stampClosureLayout(const proto::ProtoObject* codeObj,
                                                 const std::vector<std::string>& varnames,
                                                 const std::unordered_set<std::string>& cellVars,
                                                 const std::vector<std::int32_t>& extraCellSlots,
                                                 const std::vector<std::string>& extraCellNames,
                                                 const std::vector<std::string>& freeVars,
                                                 const std::string& selfRef, bool selfRefCell);

    int bytecodeOffset() const;
    /** Record a jump arg slot to be patched later with target (bytecode list index). */
    void addPatch(int argSlotIndex, int targetBytecodeIndex);
    void applyPatches();
    std::vector<std::pair<int, int>> patches_;
    
    /** TryFinally: a SETUP_FINALLY block; leaving it pops the block and runs
     *  cleanupNode (the finally body), or only pops it when cleanupNode is
     *  null (the block that routes the body's exceptions to the except
     *  clauses). With: a with statement. ExceptHandler: an except clause, or
     *  a finally clause run for an exception, while the exception is active
     *  (pushed by the engine when it caught it); leaving it pops the
     *  clause's cleanup block and the active exception (POP_EXCEPT). */
    enum class BlockType { TryFinally, With, ExceptHandler };
    struct BlockEnv {
        BlockType type;
        ASTNode* cleanupNode; // ASTNode for finally body. Null for With and ExceptHandler.
        // Set true while unwindBlocks is mid-compile for this entry's
        // cleanup. A break/continue/return inside that cleanup body must
        // not re-enter the same finally — that would recurse forever in
        // the compiler. unwindBlocks consults this flag and skips entries
        // that are already being unwound.
        bool unwinding = false;
    };
    std::vector<BlockEnv> blockEnvStack_;
    bool unwindBlocks(bool isLoopExit, bool hasValueOnStack = false);

    // Counter incremented while unwindBlocks is recursively compiling a
    // finally body for a `return` (i.e. with the pending return value
    // sitting on the operand stack).  If a break/continue inside that
    // finally redirects control to a loop, the pending value must be
    // discarded before the JUMP — otherwise FOR_ITER on the next loop
    // iteration sees the return value instead of the loop iterator.
    // This tracks the nesting so nested return-unwinds (try/finally
    // inside another try/finally) compose correctly.  See
    // test_grammar.test_control_flow_in_finally Hard1/Hard4 cases.
    int returnUnwindDepth_ = 0;
    
    struct LoopInfo {
        int start;
        std::vector<int> breakPatches;
        size_t blockDepth;
        bool hasIterator;
    };
    std::vector<LoopInfo> loopStack_;
    
    // Line number tracking
    int firstLine_ = -1;
    int currentLine_ = 0;
    int lastPC_ = 0;
    int lastLine_ = 0;
    std::vector<unsigned char> lnotabVec_;
    void setLineNumber(int line);

    // Stack depth tracking — updated by emit() for each opcode emitted.
    // currentStack_: depth after the last emitted instruction.
    // maxStack_:     highest depth seen; used as co_stacksize for slot allocation.
    int currentStack_ = 0;
    int maxStack_     = 0;

public:
    int getMaxStack() const { return maxStack_; }

private:
};

/** Build a code object (ProtoObject with co_consts, co_names, co_code) from compiler output.
 * Optional: co_varnames (slot-ordered names), co_nparams, co_automatic_count for fast local slots. */
const proto::ProtoObject* makeCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoTuple* constants,
    const proto::ProtoTuple* names,
    const proto::ProtoTuple* bytecode,
    const proto::ProtoString* filename = nullptr,
    const proto::ProtoTuple* varnames = nullptr,
    int nparams = 0,
    int kwonlyargcount = 0,
    int automatic_count = 0,
    int flags = 0,
    bool isGenerator = false,
    const proto::ProtoString* co_name = nullptr,
    int firstlineno = 0,
    const proto::ProtoTuple* lnotab = nullptr);

/** Run a code object with the given frame. Returns execution result.
 *  `closure` (a class body's) holds the cells for its free variables, in
 *  co_freevars order; its co_cellslots get fresh empty cells. */
const proto::ProtoObject* runCodeObject(proto::ProtoContext* ctx,
    const proto::ProtoObject* codeObj,
    proto::ProtoObject*& frame,
    const proto::ProtoList* closure = nullptr);

} // namespace protoPython

#endif
