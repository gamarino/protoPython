export PROTO_ENV_DIAG=1
export PROTO_PC_TRACE=1
./build/test/library/test_execution_engine --gtest_filter=ExecutionEngineTest.StoreSubscr 2>&1 | grep "DEBUG OP_STORE_SUBSCR"
