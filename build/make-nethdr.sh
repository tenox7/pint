#!/usr/bin/env bash
# The headers of the network: ndis.h, which NTOS\MAKEFIL0 makes in its header pass
# from NDISMAIN.H, NDISMAC.H and NDISMINI.H (with the port's versions of them,
# ported/ntos/inc), and the sockets headers of PRIVATE\INC\SOCKETS and
# SYS\SNET (ported/inc), which their includes name with their directories (the
# farm has them flat, as incfix.sed names them). They go to obj/gen/farm/priv and pinc
# with the headers of mkgen.sh, which mkfarm.sh adds to every farm; mkgen.sh starting
# over runs this again (its stamp is an input of every step).
#
# Usage:  cd RASPI/build && ./make-nethdr.sh
set -uo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT35="$(cd ../.. && pwd)"

docker run --rm -v "$NT35/msvc":/opt/msvc -v "$NT35":/work -w /work/RASPI/build --entrypoint bash tenox7/msvc-arm32 -c '
set -u
. ./msvc.sh
F=/tmp/f W=/work G=/work/RASPI/obj/gen/farm
HOST="bash $W/RASPI/tools/host.sh"
bash mkfarm.sh $F || exit 1
P=$W/RASPI/ported/ntos/inc
for h in $P/*.h; do
  tr -d "\032\r" < $h | sed -E -f $B/riscgate.sed -f $B/msvcgate.sed -f $B/incfix.sed | sed -f $B/armport.sed -f $B/msc2.sed > $F/priv/$(basename $h)
done
cd $F
eval "$(sed -n "/^mkdir -p \$F\/nmk$/,/^nmk windows /p" $B/mkgen.sh | sed "\$d")"
nmk ntos "inc\\ndis.h" priv
[ -s priv/ndis.h ] || { echo "make-nethdr: no ndis.h"; exit 1; }
mkdir -p $G/pinc
cp priv/ndis.h $(for h in $P/*.h; do echo priv/$(basename $h); done) $G/priv/
for d in SOCKETS SOCKETS/NETINET SOCKETS/ARPA SYS/SNET; do
  for h in $W/PRIVATE/INC/$d/*.[Hh]; do
    b=$(basename $h | tr A-Z a-z); p=$W/RASPI/ported/inc/$(echo $d | tr A-Z a-z)/$b
    [ -f $p ] && h=$p
    tr -d "\032\r" < $h | sed -E -f $B/riscgate.sed -f $B/msvcgate.sed -f $B/incfix.sed | sed -f $B/armport.sed -f $B/msc2.sed > $G/pinc/$b
  done
done
echo "   wrote the network headers"
'
