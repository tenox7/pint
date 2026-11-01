#!/usr/bin/env bash
# Build the tools of the SDK (private\sdktools) that build for ARM, in the order of its dirs
# (DIRS, then OPTIONAL_DIRS, then the tools no dirs file names), into sdcard/root/SDKTOOLS
# (\SDKTOOLS of the system disk), their DLLs beside them, as PINT 3.5 did; sdcard/rebase.sh
# rebases them after the system, as the lab rebased its mstools and idw. The tools NT setup
# puts in System32 (adduser, cacls, diskperf, ech, ini, lodctr, netjoin, perfmon, shutdown, sleep,
# unlodctr, winmsd, drwtsn32, imagehlp.dll) are built by build-nt.sh. Not built: the tools of the
# processors NT 4.0 ran on, with no ARM part (apimon, apiprofs, callmon, cap, dumpexam, givit, heaper,
# instaler, kdexts, mandel, memt, mpheap, ntsd, perftool\stat, pperf, vmmreg32, wap\crtdll, wst),
# JetAdmin (printing), sprof (16-bit WOW programs), binplace (its -c signs with rsa32.lib, which has
# no source), the tools left from older NT: damage (HPFS),
# napdump (the NAP profiling build of ntdll), uipers and winmeter (the LUID and the object counts of
# NT 3.1), ntfsdump (the untfs of NT 3.5), wap\user and wap\gdi (the user and the gdi32 exports of
# NT 3.5), symref (svcctrl.lib, which NT 4.0 does not build), and monitors (its monitors.h is not in
# the tree), cvdump (mspdb41.dll, of the build of Visual C++).
# Not yet: pstat and wcall (the gensrv -stubs of nttarget.sh), nmake (the copy /v of nmk.awk), timtp
# (a PROGRAM with assembler sources and other UMAPPL programs, make-exe.sh), sadmin, slmck, execslm
# and the commands of SLM (the resources of the UMAPPL programs of a LIBRARY, make-exe.sh) and the
# driver, viewer, event and control DLLs of mstest (NTCPPFLAGS), changes of the build that rebuild
# everything. Each step
# runs when its inputs changed (build/stamp.sh). The libraries and DLLs build first, then the
# programs, which link them; each component is a job (stamp.sh), J at once.
#
# Usage:  build/build-sdktools.sh
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
P=/work/private S=sdktools
export DEST=SDKTOOLS
# lib COMPONENT [PROGRAM...]: its library, and its programs;
# dll COMPONENT [PROGRAM...]: its DLL and its programs; exe COMPONENT PROGRAM...: its
# programs, a step for each. PHASE libs builds the libraries and DLLs, PHASE exes the programs.
# (Steps sdk/...: build-nt.sh builds some of these programs for System32 or obj, under exe/...)
lib() { local c=$1; shift; if [ $PHASE = libs ]; then job lib/$S/$c libstep $c; else exe $c "$@"; fi; }
dll() { local c=$1; shift; if [ $PHASE = libs ]; then job dll/$S/$c dllstep $c; else exe $c "$@"; fi; }
exe() { local c=$1; shift; [ $PHASE = exes ] && [ $# -gt 0 ] || return 0; job sdk/$S/$c exesteps $c "$@"; }
libstep() { ROOT=$P cstep lib $P $S/$1 -- $B/make-lib.sh $S/$1; }
dllstep() { INSTALL=1 ROOT=$P cstep dll $P $S/$1 link -- $B/make-dll.sh $S/$1; }
# (a job of several programs fails when one of them failed, not only the last: stamp.sh)
exesteps() {
    local c=$1 p rc=0; shift
    for p; do INSTALL=1 ROOT=$P step sdk/$S/$c/$p $(perl $R/build/inputs.pl $R $P $S/$c exe) -- $B/make-exe.sh $S/$c $p || rc=1; done
    return $rc
}
# The commands of SLM: copies of execslm.exe, which runs slm.exe with its name (slm\makefile.inc).
slmcmds() {
    local d=$R/sdcard/root/SDKTOOLS n
    for n in addfile addproj catsrc defect delfile delproj enlist in log out scomp ssync status; do
        cp $d/EXECSLM.EXE $d/$(echo $n | tr a-z A-Z).EXE
    done
}

tools() {
# DIRS
exe alias alias; exe autowrap AutoWrap; exe bin2c bin2c; exe build build; exe buildnum buildnum; exe cat cat; exe cdp cdp
exe checkrel checkrel; exe chksum chksum chkverfy; exe chmode chmode; exe cmdevent cmdevent
exe cntrtext/ctrlist ctrlist; lib cntrtext/initodat initodat; lib cntrtext/showperf showperf
exe cp cp; exe curridw curridw; exe dbmon dbmon; exe dbwin32 dbwin32; lib ddespy ddespy; exe delnode delnode; exe diff diff
exe diskmon diskmon; exe dmacheck DMACHECK; exe drivemap drivemap; exe dskbench dskbench; exe dskdump dskdump; exe du du
exe dumpchk dumpchk; exe dumpclip dumpclip; exe dumpflop dumpflop; exe dumpgrp dumpgrp fix31grp
exe dumpref dumpref; exe echotime echotime; lib ethstat ethstat; exe exp exp; exe extag extag; exe fcom fcom; exe filever filever
exe ftc ftc; exe gensrv gensrv; exe getbin getbin; exe getdbg getdbg; exe gflags gflags; exe guidlib guidlib; exe hd hd; lib he he
exe head head; exe hextract hextract; exe hsplit hsplit
exe imagehlp bind certify checkfix dbgdump editsym imagecfg imagechk impchk rebase smashlck splitsym stripcv undname \
    upddbg verfix
exe infilter infilter; exe kbdtool kbdtool; exe kernprof kernprof; exe kernrate kernrate; exe kerntwk KernTwk; exe links links
exe list list; exe listmung listmung; exe loc loc; exe ls ls; exe makeani makeani; exe mapmsg mapmsg
exe memsnap memsnap sortlog poolsnap; exe mrc mrc; exe mttf/main mttf; exe mttf/setup getmttf; exe mttf/vwr mttfvwr
exe munge munge; exe mv mv; exe netperm netperm; exe nvram nvram; exe order order; exe paranoia paranoia
exe parcomp parcomp parsync; exe passprop passprop; exe pcmcmd pcmcmd; dll pdh/dll
exe perfmtr perfmtr top pmon drivers poolmon lookmon; exe pfmon pfmon; exe ppr ppr; exe pretty pretty; exe profile profile
dll psapi; exe pview pview; exe qgrep qgrep; exe rathole rathole; lib rcpp rcpp; exe regdiff regdiff
lib regini regini regdmp regdir regback regfind; exe regtogrp regtogrp; exe remote remote; exe rescan rescan setpriv
lib resmon dh heapmon resmon objmon dhcmp
# (barrier: the readers and writers of rlt32 link io's import library and rw\common)
lib restools/unicode; dll restools/rlt32/io; lib restools/rlt32/rw/common; barrier
for c in win32 win16 res32 inf mac; do dll restools/rlt32/rw/$c; done; exe restools/rlt32/bingen bingen
lib restools/rltools/common; exe restools/rltools/rlman rlman
for c in rlquiked rladmin rledit; do lib restools/rltools/$c $c; done
lib restools/resonexe resonexe; lib restools/tct display
exe rm rm; exe sclist sclist; exe secedit/app SECEDIT; dll secedit/dll; exe srvname srvname; exe startpgm startpgm
exe strings strings; exe structo structo; exe suck suck; exe switcher switcher; exe sync sync; exe tail tail; exe tc tc
exe tee tee; exe timer timer; lib tlist tlist kill empty emptyq; dll topdesk/dll; exe topdesk/app topdesk
exe topdesk/mrshadow mrshadow; exe touch touch; exe trans trans; exe tz tz; exe uixport uixport; exe undel undel; exe units units
exe upd upd; exe updrn updrn; exe vadump vadump
# of vctools the message compiler, the resource compiler and its DLL and the help contents converter
exe vctools/mc mc; dll vctools/rcdll; exe vctools/rc rc; exe vctools/contents/hdxcvt hdxcvt
exe vdesk vdesk; exe vdmperf vdmperf; exe walk walk; exe wc wc; exe wcache wcache; exe wcshdr wcshdr; exe where where
exe winobj winobj; exe wintools/winmsdp winmsdp; lib wperf wperf; exe ws ws; exe ws2hdr ws2hdr; exe yapt yapt; exe ync ync
lib z/extens/exthdr; barrier; dll z/extens/zextens; exe z/src z; exe zwapi zwapi

# OPTIONAL_DIRS
exe pch pch; exe a2coff a2coff; exe bintrack bintrack; lib bugboard htmboard bugboard; exe chkalive ChkAlive
exe clearmem clearmem; exe compdir compdir; exe dlgedit dlgedit; lib doctor doctor c2rtf; exe dosdev dosdev
exe dphhogs dphhogs; exe dskimage dskimage; lib editreg editreg; lib fontedit fontedit; dll gutils
exe imagedit imagedit; exe masm masm386; lib mip2coff mip2coff
for c in ntedit/src ntdlgs/dll ntscrn ntidle ntabout; do dll mstest/$c; done
exe mstest/ntdlgs/app testdlgs; exe mstest/ntsnap testscrn
exe newinvtp winvtp; exe ntslm ntslm; exe objdir objdir; exe pdc pdc; lib pviewer pviewer; exe random random
exe rcdump rcdump; exe readline readline; exe roshare roshare; exe seclist seclist; exe setlink setlink; exe simbad simbad
lib slmnew/slm slm; lib slmnew/util merge unmerge slmdiff cookie; exe slmnew/fixdiff fixdiff
exe slmnew/slmed/daytona slmed; exe slmnew/fixlog logfix; exe slmnew/fixstat slmfix
exe solidpp solidpp; dll spy/dll; lib spy/app spy; exe takeown takeown; exe tile tile; lib tlibs/nb3lib/src
exe vi vi
dll fastimer; barrier; lib wap/dump apf32dmp; lib wap/convert apf32cvt; for c in fernel base advapi; do dll wap/$c; done
exe windiff/windiff windiff; lib windiff/client; exe windiff/server sumserve; lib xerox xerox
lib zoomin zoomin

# the tools no dirs file names (membnch of killer: mptest has a copy of it)
lib cpustres CpuStres; lib creatfil creatfil; exe dd4 dd4; exe detab detab; exe dis32 dis32
exe diskgeom diskgeom; lib exctrlst ExCtrLst; exe flex flex; exe heaper/badapp badapp
exe kbdview kbdview; exe killer mtbnch iobnch membnch; lib leakyapp LeakyApp; exe lnkstres lnkstres; exe logoff logoff
lib lztest lztest; exe mepsetup mepsetup; exe mhlsrv/client mhlcli; exe mhlsrv/server mhlsrv; exe modsel modesel
lib mptest mptest; exe mrgscomp mrgscomp mrgdiff; exe nbsim nbsim; exe netshare netshare
exe newer newer; exe olebnch oledoc; exe owntree owntree; lib perflog/pdlcnfig pdlcnfig
lib perflog/pdlsvc pdlsvc; dll perftool/timer; lib perftool/wait wait; lib physdisk physdisk
exe qfetool qfetool; exe rshell/ntclient rcmd; lib rshell/server rcmdsrv; lib rslm rslm
exe rwtest rwtest; lib secmgr secmgr; lib setedit setedit; lib sfs sfs-gate sfs-page; lib shellfix shellfix
lib srvnsp srvnsp; exe swappath swappath
exe vmtests poolkill vmperf32 vmtest vmstress tfork pageit color vmread
exe wsacl wsacl; lib wzmail/dh.new/lib; lib wzmail/mail1.0 wzmail; exe xmas xmas; exe yacc yacc; lib zd zdsim
}

PHASE=libs; tools; barrier
PHASE=exes; tools; barrier
# (not yet, with execslm: step slmcmds $R/sdcard/root/SDKTOOLS/EXECSLM.EXE -- slmcmds)
# the templates of the scanners flex and the parsers yacc write (flex -Sc:\sdktools\skeleton.flx, yacc
# finds yypars.c on the INCLUDE path)
T="$R/../private/sdktools/flex/skeleton.flx $R/../private/sdktools/yacc/yypars.c"
step templates $T -- install -m 644 $T $R/sdcard/root/SDKTOOLS/
