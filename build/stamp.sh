# The steps of the build (build-update.sh), sourced by the build scripts. A step runs
# when one of its inputs is newer than its stamp (obj/stamp/NAME, touched when it
# succeeds): the NT tree never changes, so the inputs are the files of the port and
# what the steps before made. Every step also takes the headers and scripts all use
# (GLOBAL) and what the tool, hive (REGINI), ksarm and MIDL steps made (GEN); those
# steps take only what the steps before them made. FORCE=1 runs every step.
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
K=$R/arcfw/kernel
ST=$R/obj/stamp
mkdir -p $ST
GLOBAL="$K/inc $R/arcfw/inc $(ls $K/*.sh | grep -v /make-) $K/*.sed $K/*.awk $R/tools/ntdiff.pl $R/tools/host.sh"
HOSTGEN="$R/obj/host $R/obj/gen/farm/.stamp $R/obj/gen/error.h"
GEN="$HOSTGEN $R/obj/gen/ksarm.h $R/obj/gen/midl"
CRT="$R/arcfw/crt $R/arcfw/ported/crt32 $R/arcfw/ported/crt32nt $R/arcfw/ported/fp32"

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
    local n=$1/$(echo $2/$3 | sed -E 's,^/work/(PRIVATE/)?,,') a=("$2" "$3"); shift 3
    [ "$1" = link ] && { a+=(link); shift; }
    step "$n" $(perl $R/build/inputs.pl $R "${a[@]}") "$@"
}
