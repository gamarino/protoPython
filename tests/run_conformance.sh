#!/bin/bash

# Configuration
PROTOPY="/home/gamarino/Documentos/proyectos/protoPython/build/src/runtime/protopy"
LOG="/home/gamarino/Documentos/proyectos/protoPython/conformance_status.log"
BASE_DIR="/home/gamarino/Documentos/proyectos/protoPython"
TIMEOUT="300" # 5 minutes per test

# Test Groups
ESSENTIAL=(
    "test/cpython/test_grammar.py"
    "test/cpython/test_types.py"
    "test/cpython/test_descr.py"
    "test/cpython/test_generators.py"
    "test/cpython/test_asyncgen.py"
    "test/cpython/test_base64.py"
)

IMPORTANT=(
    "tests/test_os.py"
)

NECESSARY=(
    "tests/test_decorator.py"
)

echo "--- Conformance Suite Started: $(date) ---" > "$LOG"
echo "Approval Policy: Autonomous" >> "$LOG"
echo "Timeout per test: ${TIMEOUT}s" >> "$LOG"
echo "" >> "$LOG"

run_tests() {
    local group_name=$1
    shift
    local tests=("$@")
    
    echo "## Running $group_name tests..." | tee -a "$LOG"
    for test_file in "${tests[@]}"; do
        if [ ! -f "$BASE_DIR/$test_file" ]; then
            echo "[SKIP] $test_file (Not Found)" | tee -a "$LOG"
            continue
        fi
        
        echo -n "[RUN] $test_file ... " | tee -a "$LOG"
        
        START_TIME=$(date +%s)
        PROTO_ENV_DIAG=1 timeout "$TIMEOUT" "$PROTOPY" "$BASE_DIR/$test_file" > /tmp/test_out.log 2>&1
        RET=$?
        END_TIME=$(date +%s)
        DURATION=$((END_TIME - START_TIME))
        
        if [ $RET -eq 0 ]; then
            echo "PASS (${DURATION}s)" | tee -a "$LOG"
        elif [ $RET -eq 124 ]; then
            echo "TIMEOUT" | tee -a "$LOG"
        else
            echo "FAIL (exit $RET, ${DURATION}s)" | tee -a "$LOG"
            # Optional: append snippet of failure to log
            # tail -n 5 /tmp/test_out.log >> "$LOG"
        fi
    done
    echo "" >> "$LOG"
}

run_tests "Essential" "${ESSENTIAL[@]}"
run_tests "Important" "${IMPORTANT[@]}"
run_tests "Necessary" "${NECESSARY[@]}"

echo "--- Conformance Suite Finished: $(date) ---" >> "$LOG"
