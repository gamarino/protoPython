#!/bin/bash
# protoPython benchmarks/run_4way_interleaved.py (CPython 3.14 and 3.14t) on
# protoCore 2.14.8, main 4f8ab5bb, Release; three rounds, each behind the
# quiet gate and on a fresh copy of the library.
set -u
WS=/home/gamarino/Documentos/proyectos
OUT=$WS/.agent_scratch/copy/py_suite; mkdir -p $OUT/libs
Q="python3 $WS/protoCore/docs/reports/data/2026-10-12-mark-stack-int-compare/quiet.py"
LIB=$WS/.agent_scratch/installers-oct2026/stage2148/usr/lib/libprotoCore.so.3
t=$WS/protoPython/build_2148
for r in 1 2 3; do
  $Q > $OUT/quiet_r$r.txt 2>&1
  d=$OUT/libs/r$r; mkdir -p $d; cp -L $LIB $d/; export LD_LIBRARY_PATH=$d
  echo "$(date -u +%FT%TZ) py r$r"
  ( cd $WS/protoPython && PROTOPY_BIN=$t/src/runtime/protopy \
      PROTOPYC_BIN=$t/src/compiler/protopyc RUN_MODULE_BIN=$t/test/compiler/run_module \
      LD_LIBRARY_PATH_BENCH=$t/src/library \
      CPYTHON_BIN=/usr/local/bin/python3.14 CPYTHONT_BIN=$HOME/.local/bin/python3.14t \
      python3 benchmarks/run_4way_interleaved.py -o $OUT/py_r$r.md > $OUT/py_r$r.log 2>&1 )
done
echo DONE
