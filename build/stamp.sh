# The steps of the build (build-update.sh), sourced by the build scripts. A step runs
# when one of its inputs is newer than its stamp (obj/stamp/NAME, touched when it
# succeeds): the NT tree never changes, so the inputs are the files of the port and
# what the steps before made. Every step also takes the headers and scripts all use
# (GLOBAL) and what the tool, hive (REGINI), ksarm and MIDL steps made (GEN); those
# steps take only what the steps before them made. FORCE=1 runs every step.
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
K=$R/kernel B=$R/build
ST=$R/obj/stamp
mkdir -p $ST
GLOBAL="$K/inc $R/arcfw/inc $(ls $B/*.sh | grep -vE "/(make-|build|stamp|run|portable)[^/]*$") $B/*.sed $B/*.awk $R/tools/ntdiff.pl $R/tools/host.sh"
HOSTGEN="$R/obj/host $R/obj/gen/farm/.stamp $R/obj/gen/error.h"
GEN="$HOSTGEN $R/obj/gen/ksarm.h $R/obj/gen/midl"
CRT="$R/crt $R/ported/crt32 $R/ported/crt32nt $R/ported/fp32"

# step NAME INPUT... -- COMMAND...: the command (its script an input too) when the
# step is out of date
step() {
    local n=$1 i=() s; shift
    while [ "$1" != -- ]; do i+=("$1"); shift; done; shift
    [ -f "$1" ] && i+=("$1")
    s=$ST/$(echo $n | tr / _)
    [ -z "${FORCE:-}" ] && [ -f $s ] && [ -z "$(find $GLOBAL $GEN "${i[@]}" -newer $s -print -quit 2>/dev/null)" ] && return 0
    echo ">> $n"
    "$@" && touch $s
}

# cstep KIND ROOT COMPONENT [link] -- COMMAND...: the step of a component (inputs.pl),
# KIND/<its directory>
cstep() {
    local n=$1/$(echo $2/$3 | sed -E 's,^/work/(private/)?,,') a=("$2" "$3"); shift 3
    [ "$1" = link ] && { a+=(link); shift; }
    step "$n" $(perl $R/build/inputs.pl $R "${a[@]}") "$@"
}

# Jobs: with J > 1 (default 8; J=1 in order), job NAME COMMAND... runs COMMAND, a step or a
# function of steps that run in order, as one of at most J jobs at once. Its output is
# printed whole when it ends, and no job starts after one failed. barrier waits for every
# job and stops the build if one failed. With J=1 a job runs in place.
J=${J:-8}
JD=$R/obj/jobs
[ "$J" -le 1 ] || { mkdir -p $JD; rm -rf $JD/print $JD/failed; }
job() {
    local n=$1 l; shift
    [ "$J" -gt 1 ] || { "$@"; return; }
    while [ $(jobs -rp | wc -l) -ge $J ]; do sleep 0.2; done
    [ ! -f $JD/failed ] || return 0
    l=$JD/$(echo $n | tr / _).log
    (
        set +e
        "$@" > $l 2>&1
        s=$?
        until mkdir $JD/print 2>/dev/null; do sleep 0.1; done
        cat $l
        rmdir $JD/print
        [ $s = 0 ] || echo $n >> $JD/failed
        exit $s
    ) &
}
barrier() {
    [ "$J" -gt 1 ] || return 0
    wait
    [ -f $JD/failed ] || return 0
    echo "!! failed: $(tr '\n' ' ' < $JD/failed)"
    exit 1
}
