#!/bin/bash
# protoPython BEFORE (cd43628b on protoCore cf52f9f5) vs AFTER (1633557c on
# protoCore 65760e4a), Release, clean worktrees, each protoPython build
# carrying its sibling protoCore (add_subdirectory ../protoCore).
set -u
export QUIET_CORES=${QUIET_CORES:-4.0}
WS=/home/gamarino/Documentos/proyectos
M=$WS/.agent_scratch/meas_1022; OUT=$M/py_out; mkdir -p $OUT/libs
Q="python3 $WS/protoCore/docs/reports/data/2026-10-12-mark-stack-int-compare/quiet.py"
CPT=$HOME/.local/bin/python3.14t; CP=/usr/local/bin/python3.14
T() { echo $M/$1/protoPython/build_release; }
fresh() { local d=$OUT/libs/$2; mkdir -p $d; cp -L $(T $1)/protoCore/libprotoCore.so.4 $d/libprotoCore.so.4; export LD_LIBRARY_PATH=$d
          echo "== $2" >> $OUT/ldd.txt; LD_LIBRARY_PATH=$(T $1)/src/library:$d ldd $(T $1)/src/runtime/protopy $(T $1)/test/compiler/run_module | grep -iE "proto|^/" >> $OUT/ldd.txt; }
# 1. verification
fresh before verify_before; fresh after verify_after; unset LD_LIBRARY_PATH
python3 $M/py_verify.py $OUT > $OUT/verify.txt 2>&1
# 2. 4-way harness, three rounds, order alternated
for r in 1 2 3; do
  order="after before"; [ $r = 2 ] && order="before after"
  for v in $order; do
    $Q > $OUT/quiet_${v}_r$r.txt 2>&1; fresh $v ${v}_r$r; echo "$(date -u +%FT%TZ) 4way $v r$r"
    t=$(T $v)
    ( cd $M/$v/protoPython && PROTOPY_BIN=$t/src/runtime/protopy PROTOPYC_BIN=$t/src/compiler/protopyc \
        RUN_MODULE_BIN=$t/test/compiler/run_module LD_LIBRARY_PATH_BENCH=$t/src/library \
        CPYTHON_BIN=$CP CPYTHONT_BIN=$CPT \
        python3 benchmarks/run_4way_interleaved.py -o $OUT/py_${v}_r$r.md > $OUT/py_${v}_r$r.log 2>&1 )
  done
done
# 3. protopyc compile time (wall, 3 reps, interleaved), then compiled probes
$Q > $OUT/quiet_compile.txt 2>&1
fresh before cmp_before; LB=$LD_LIBRARY_PATH; fresh after cmp_after; LA=$LD_LIBRARY_PATH; unset LD_LIBRARY_PATH
echo "$(date -u +%FT%TZ) compile"
SRCS="$M/after/protoPython/benchmarks/int_sum_loop.py $M/after/protoPython/benchmarks/attr_lookup.py $M/after/protoPython/benchmarks/call_recursion.py $M/after/protoPython/benchmarks/pyperf/bench_richards_lite.py $M/py_probes/raise_across5.py $M/py_probes/keyerror.py"
for rep in 1 2 3; do
  for s in $SRCS; do
    for v in before after; do
      L=$LB; [ $v = after ] && L=$LA
      wd=$OUT/compile/$v/$(basename $s .py); mkdir -p $wd; cp $s $wd/; rm -f $wd/module.so
      t0=$(date +%s.%N); ( cd $wd && LD_LIBRARY_PATH=$(T $v)/src/library:$L $(T $v)/src/compiler/protopyc $(basename $s) --build-so > build.log 2>&1 ); rc=$?; t1=$(date +%s.%N)
      echo "$(basename $s .py) $v rep$rep rc=$rc ms=$(echo "($t1-$t0)*1000" | bc) so_bytes=$(stat -c %s $wd/module.so 2>/dev/null || echo 0) cpp_lines=$(cat $wd/*.cpp 2>/dev/null | wc -l)" >> $OUT/compile.txt
    done
  done
done
# 4. probes: protopy / protopyc BEFORE and AFTER, CPython 3.14t; perf stat -r 3
$Q > $OUT/quiet_probes.txt 2>&1; echo "$(date -u +%FT%TZ) probes"
for f in $M/py_probes/*.py; do
  n=$(basename $f .py)
  for v in before after; do
    L=$LB; [ $v = after ] && L=$LA
    wd=$OUT/probe_so/$v/$n; mkdir -p $wd; cp $f $wd/
    ( cd $wd && LD_LIBRARY_PATH=$(T $v)/src/library:$L $(T $v)/src/compiler/protopyc $n.py --build-so > build.log 2>&1 )
  done
  for k in protopy_before protopy_after protopyc_before protopyc_after cpythont; do
    v=${k#*_}; L=$LB; [ $v = after ] && L=$LA
    case $k in
      protopy_*) c="env LD_LIBRARY_PATH=$(T $v)/src/library:$L $(T $v)/src/runtime/protopy $f";;
      protopyc_*) [ -f $OUT/probe_so/$v/$n/module.so ] || { echo "$n $k NO_SO" >> $OUT/probes.txt; continue; }
                  c="env LD_LIBRARY_PATH=$(T $v)/src/library:$L $(T $v)/test/compiler/run_module $OUT/probe_so/$v/$n/module.so";;
      cpythont) c="$CPT $f";;
    esac
    chk=$($c 2>&1 | grep CHECK | tail -1)
    o=$(perf stat -r 3 -x, -e cycles,task-clock $c 2>&1 >/dev/null | grep -E 'cycles|task-clock' | cut -d, -f1 | paste -sd' ')
    echo "$n $k $o | $chk" >> $OUT/probes.txt
  done
done
echo DONE
