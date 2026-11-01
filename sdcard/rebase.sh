#!/usr/bin/env bash
# Rebase the DLLs and programs of the system disk (root/WINNT, then the SDK tools of
# root/SDKTOOLS, as the lab's mstools and idw) as the build lab rebased those
# of a build (public\tools\ntrebase.cmd, for a regular build; a QFE build kept the addresses
# of a release, its baseaddr.txt, which ARM has not had): imagecfg -r marks the processes of
# the system for the aggressive trimming of their working sets, then rebase packs the DLLs
# down from 0x78000000 (first the known DLLs and the network DLLs, each a group), the programs
# up from 0x01000000, and csrss with its server DLLs down from 0x60000000, the keyboard layouts
# and the display drivers each overlaid below them. The lists are the lab's (public\tools\*.reb);
# their WOW (ntvdm.reb) and Wx86 images are not built, and the system has no printing (no
# spooler, no printer.reb).
# imagecfg and rebase (sdktools\imagehlp, make-exe.sh) run on the Wine of the build image with
# the imagehlp.dll of the port: that of Wine does not rebase. Log: obj/rebase.log (and the
# output, obj/rebase.out).
#
# Usage:  cd RASPI/sdcard && ./rebase.sh
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NT40="$(cd ../.. && pwd)"
for f in ../obj/REBASE.EXE ../obj/IMAGECFG.EXE root/WINNT/System32/IMAGEHLP.DLL; do
  [[ -f "$f" ]] || { echo "missing $f - build it first"; exit 1; }
done

echo ">> rebasing the images of root/WINNT and root/SDKTOOLS"
docker run --rm -v "$NT40/../nt35/msvc":/opt/msvc -v "$NT40/../nt35":/nt35:ro -v "$NT40/../XPSP1":/XPSP1:ro -v "$NT40":/work -v "$NT40/private":/work/private:ro -v "$NT40/public":/work/public:ro -w /work/RASPI/sdcard --entrypoint bash tenox7/msvc-arm32 -c '
set -u
O=/work/RASPI/obj
W=/work/RASPI/sdcard/root/WINNT
S=$W/System32
L=/work/public/tools
# the SDK tools, after the system: a pattern only when the folder has such files
K=/work/RASPI/sdcard/root/SDKTOOLS; KD=; KE=
ls $K/*.dll >/dev/null 2>&1 && KD="$K/*.dll"
ls $K/*.exe >/dev/null 2>&1 && KE="$K/*.exe"
# the tools, with the imagehlp.dll of the port beside them (tools/host.sh: NTROOT)
mkdir -p /tmp/rb/RASPI/obj/host
cp $O/REBASE.EXE $O/IMAGECFG.EXE $S/IMAGEHLP.DLL /tmp/rb/RASPI/obj/host/
export WINEDLLOVERRIDES=imagehlp=n
rm -f $O/rebase.log /tmp/out.txt
# (a pattern of the arguments, quoted, is for the tool to expand: setargv)
run() { NTROOT=/tmp/rb bash /work/RASPI/tools/host.sh "$@" >> /tmp/out.txt 2>&1; }
run imagecfg -r $(ls -d $S/* $W/* | grep -iE "/System32/(smss|csrss|lsass|services|winlogon|rpcss)\.exe$|/WINNT/explorer\.exe$")
F="-v -l $O/rebase.log"
run rebase $F -d -b 0x78000000 -R $S -N $L/csrss.reb -N $L/kbd.reb -N $L/video.reb -N $L/ntvdm.reb -N $L/wx86.reb -N $L/wx86thnk.reb -N $L/never.reb \
    -G $L/known.reb -G $L/net.reb "$S/*.acm" "$S/*.dll" "$S/*.cpl" "$S/*.drv" ${KD:+"$KD"}
run rebase $F -b 0x01000000 -R $S -N $L/csrss.reb -N $L/kbd.reb -N $L/video.reb -N $L/ntvdm.reb -N $L/ntoskrnl.reb -N $L/bldtools.reb "$W/*.exe" "$S/*.exe" \
    ${KE:+"$KE"}
run rebase $F -d -b 0x60000000 -R $S -G $L/csrss.reb -O $L/kbd.reb -O $L/video.reb
tr -d "\r" < /tmp/out.txt > $O/rebase.out
tr -d "\r" < $O/rebase.log > /tmp/log.txt && cp /tmp/log.txt $O/rebase.log
if grep -E "\*\*\*|fopen|preceed|Invalid switch|^usage|unable to|Unhandled" $O/rebase.out; then
  echo "!! rebase failed (obj/rebase.out)"; exit 1
fi
echo "   $(grep -c " rebased to " $O/rebase.log) images placed, $(grep -c "Unable to rebase" $O/rebase.log) without relocations kept, $(grep -c "Could Not Find" $O/rebase.out) of the lists not built"
grep "^REBASE: Range" $O/rebase.out | sed "s/^REBASE: /   /"
'
