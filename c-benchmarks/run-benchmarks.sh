#!/bin/bash
# Run run-benchmarks.sh [start_test_num]
#

tempc=c-benchmarks/__temp.c
temp=c-benchmarks/__temp.out
temp2=c-benchmarks/__temp2.out
temp3=c-benchmarks/__temp3.out
errf=c-benchmarks/__temp.err

if test x`echo -n` != "x-n";then NECHO="echo -n"; else NECHO=printf; fi

rm -f $temp3

percent () {
    val=`awk "BEGIN {if ($2==0) print \"Inf\"; else printf \"%.2f\n\", $1/$2;}"`
    echo "$val"x
    if test x$c2m != x; then
	echo $val >>$temp3
    fi
}

skip () {
    l=$1
    n=$2
    while test $l -le $n; do $NECHO " "; l=`expr $l + 1`; done
}

print_time() {
    title="$1"
    secs=$2
    c2m=$3
    if test "x$NECHO" = x; then
	echo $title:
	echo "   " $secs
    else
	n=$title:
	$NECHO "$n"
	skip ${#n} 40
	$NECHO "$secs"
	skip ${#secs} 10
        echo " " `percent $base_time $secs $c2m`
    fi
}

run () {
  title=$1
  preparation=$2
  program=$3
  expect_out=$4
  inputf=$5
  flag=$6
  c2m=$7
  ok=
  if test x"$preparation" != x; then
    $preparation 2>$errf
    if test $? != 0; then echo "$2": FAIL; cat $errf; return 1; fi
  fi
  if test x$inputf = x; then inputf=/dev/null;fi
  if (time -p $program < $inputf) >$temp 2>$temp2; then ok=y;fi
  if test x$ok = x;then echo $program: FAILED; return 1; fi
  if test x$expect_out != x && ! cmp $expect_out $temp; then
    echo Unexpected output:
    diff -up $expect_out $temp
    return 1
  fi
  secs=`egrep 'user[ 	]*[0-9]' $temp2 | sed s/.*user// | sed s/\\t//`
  if test x$flag != x;then base_time=$secs;fi
  print_time "$title" $secs $c2m
}

runbench () {
  bench=$1
  arg=$2
  base_time=0.01
  inputf=
  if test -f $bench.expect; then expect_out=$bench.expect; else expect_out=; fi
  first=first
  cat >$tempc <<EOF
  #include <stdio.h>
  int main (void) {printf ("hi\n"); return 0;}
EOF
  if gcc $tempc >/dev/null 2>&1; then
      run "gcc -O2" "gcc -std=c99 -O2 -Ic-benchmarks -I. $bench.c -lm" "./a.out $arg" "$expect_out" "$inputf" "$first"
      first=
  fi
  if clang $tempc >/dev/null 2>&1; then
      run "clang -O2" "clang -O2 -Ic-benchmarks -I. $bench.c -lm" "./a.out $arg" "$expect_out" "$inputf" "$first"
      first=
  fi
#  run "gcc -O0" "gcc -std=c99 -O0 -Ic-benchmarks -I. $bench.c -lm" "./a.out $arg" "$expect_out" "$inputf"
  run "c2m -eg" "" "./c2m -Ic-benchmarks -I. $bench.c -eg $arg" "$expect_out" "$inputf" "$first" 1
}

start_bench_num=$1
if test x$start_bench_num = x; then
  start_bench_num=0
fi
bench_num=0
for bench in array binary-trees except funnkuch-reduce hash hash2 heapsort lists matrix method-call mandelbrot nbody sieve spectral-norm strcat  # ackermann fib random 
do
    if test $bench_num -ge $start_bench_num; then
	b=c-benchmarks/$bench
	if test -f $b.arg; then arg=`cat $b.arg`; else arg=; fi
	echo "+++++ $bench_num:$bench $arg +++++"
	runbench $b $arg
    fi
    bench_num=`expr $bench_num + 1`
done

s="C2M Average:"
$NECHO $s
skip ${#s} 53
awk '{f = f + $1} END {printf "%0.2fx\n", f / NR;}' < $temp3

s="C2M Geomean:"
$NECHO $s
skip ${#s} 53
awk 'BEGIN {f = 1.0} {f = f * $1} END {printf "%0.2fx\n", f ^  (1.0/NR);}' < $temp3

rm -f $tempc $temp $temp2 $temp3 $errf
