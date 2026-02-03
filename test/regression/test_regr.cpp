#include <gtest/gtest.h>
#include <protoPython/PythonEnvironment.h>
#include <string>

class RegressionTest : public ::testing::Test {
protected:
    protoPython::PythonEnvironment env{STDLIB_PATH, {REGR_PATH}};
};

TEST_F(RegressionTest, LoadRunner) {
    // In our current implementation, resolving a module name that maps to a file
    // will execute the file content (if we had an executor, for now it just creates symbols).
    // Let's verify we can load our runner placeholder.
    const proto::ProtoObject* runner = env.resolve("regrtest_runner");
    EXPECT_NE(runner, nullptr);
    EXPECT_NE(runner, PROTO_NONE);
}
