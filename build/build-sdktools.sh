#!/usr/bin/env bash
# Build the tools of the SDK (PRIVATE\SDKTOOLS) that build for ARM, in the order of
# its DIRS, then its OPTIONAL_DIRS and the tools no DIRS names, into
# sdcard/root/SDKTOOLS (\SDKTOOLS of the system disk), their DLLs beside them. The
# tools already in System32 (ini, ech, adduser, shutdown, winmsd, perfmon, imagehlp)
# are built by build-user.sh. Each step runs when its inputs changed (build/stamp.sh).
#
# Usage:  build/build-sdktools.sh
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
P=/work/PRIVATE S=SDKTOOLS
export DEST=SDKTOOLS
# lib COMPONENT [PROGRAM...]: its library (in its directory, LOCAL=1), then its programs
lib() { local c=$1; shift; LOCAL=1 ROOT=$P cstep lib $P $S/$c -- $K/make-lib.sh $S/$c; exe $c "$@"; }
dll() { local c=$1; shift; INSTALL=1 ROOT=$P cstep dll $P $S/$c link -- $K/make-dll.sh $S/$c; exe $c "$@"; }
# exe COMPONENT PROGRAM...: a step for each program of the component
exe() {
    local c=$1 p; shift
    for p; do INSTALL=1 ROOT=$P step exe/$S/$c/$p $(perl $R/build/inputs.pl $R $P $S/$c link) -- $K/make-exe.sh $S/$c $p; done
}
# The commands of SLM: copies of execslm.exe, which runs slm.exe with its name (SLM\MAKEFILE.INC).
slmcmds() {
    local d=$R/sdcard/root/$S n
    for n in addfile addproj catsrc defect delfile delproj enlist in log out scomp ssync status; do
        cp $d/EXECSLM.EXE $d/$(echo $n | tr a-z A-Z).EXE
    done
}

# DIRS
exe ALIAS alias; exe CAT cat; lib CHECKREL checkrel; exe CHMODE chmode; exe CMDEVENT cmdevent; exe CRASHDMP crashdmp
exe CP cp; exe DELNODE delnode; exe DIFF diff; exe DSKDUMP dskdump; exe DISKMON diskmon; exe DU du; exe ECHOTIME echotime
exe EXP exp; exe FCOM fcom; exe GETDBG getdbg; exe HEAD head
exe IMAGEHLP bind binplace checkfix dbgdump imagechk rebase smashlck splitsym undname verfix editsym stripcv
exe INFILTER infilter; exe KERNPROF kernprof; lib LOC loc; exe LS ls; exe MUNGE munge; exe MV mv; exe PARCOMP parcomp parsync
exe PERFMTR perfmtr top pmon drivers poolmon; exe PSTAT pstat pcall; exe PROFILE profile; lib RATHOLE rathole
exe RESCAN rescan setpriv remove; lib REGINI regini regdmp regback; exe RM rm; lib STARTPGM startpgm; exe STRINGS strings
exe SUCK suck; exe SYNC sync; exe TAIL tail; exe TC tc; exe TIMER timer ntimer; exe TOUCH touch; exe TRANS trans
exe UNDEL undel; exe UPD upd; exe UPDRN updrn; lib VDMPERF vdmperf; exe WALK walk; exe WC wc; exe WHERE where
lib WPERF wperf; exe DSKBENCH dskbench; exe YNC ync; exe BUILD build
lib RESTOOLS/UNICODE; lib RESTOOLS/RLTOOLS/COMMON; exe RESTOOLS/RLTOOLS/RLMAN rlman
for c in RLQUIKED RLADMIN RLEDIT; do lib RESTOOLS/RLTOOLS/$c $(echo $c | tr A-Z a-z); done
lib RESTOOLS/RESONEXE resonexe; lib RESTOOLS/TCT display
exe GENSRV gensrv; lib HE he; lib LIST list; lib UNITS units; exe MAPMSG mapmsg; lib QGREP qgrep; lib PPR ppr; lib RCPP rcpp
exe HD hd; exe UIXPORT uixport; dll PSAPI; exe VADUMP vadump; exe HEXTRACT hextract; exe HSPLIT hsplit; exe WCSHDR wcshdr
lib PVIEW pview; exe TEE tee; exe WS ws; exe ZWAPI zwapi; lib WINOBJ winobj; lib SECEDIT/APP SECEDIT; dll SECEDIT/DLL
lib DDESPY ddespy; exe REGTOGRP regtogrp; exe REMOTE remote
exe CNTRTEXT/LODCTR lodctr; exe CNTRTEXT/UNLODCTR unlodctr; exe CNTRTEXT/CTRLIST ctrlist
exe LISTMUNG listmung; exe TZ tz; dll TOPDESK/DLL; lib TOPDESK/APP topdesk; exe DISKPERF diskperf; lib BIN2C bin2c
exe NETJOIN netjoin; exe MTTF/MAIN mttf; exe MTTF/SETUP getmttf; exe MTTF/VWR mttfvwr; exe DUMPGRP dumpgrp fix31grp
lib TLIST tlist kill empty; exe MAKEANI makeani; dll RESMON resmon objmon heapwalk; lib COMPACT compact
exe REGDIFF regdiff; exe CACLS cacls; exe BUILDNUM buildnum; exe AUTOWRAP AutoWrap

# OPTIONAL_DIRS: of VCTOOLS the code view dump, the message compiler, the resource compiler, the
# help contents converter and nmake (PMAKE)
exe VCTOOLS/CVDUMP cvdump; exe VCTOOLS/MC mc; dll VCTOOLS/RCDLL; exe VCTOOLS/RC rc; exe VCTOOLS/CONTENTS/HDXCVT hdxcvt
lib VCTOOLS/PMAKE/SRC nmake
exe PCH pch; exe PDC pdc; exe NTSLM ntslm; lib MASM masm386; lib DISKEDIT diskedit; lib DLGEDIT dlgedit
lib DOCTOR doctor c2rtf; exe DOSDEV dosdev; lib DSKIMAGE dskimage; lib FONTEDIT fontedit; dll GUTILS; lib IMAGEDIT imagedit
lib MIP2COFF mip2coff; exe OBJDIR objdir; exe READLINE readline; exe SETLINK setlink; exe SOLIDPP solidpp
lib SLMNEW/SLM slm sadmin slmck execslm; step slmcmds $R/sdcard/root/$S/EXECSLM.EXE -- slmcmds
lib SLMNEW/UTIL merge unmerge slmdiff cookie; exe SLMNEW/SLMED/DAYTONA slmed; exe SLMNEW/FIXDIFF fixdiff
exe SLEEP sleep; dll SPY/DLL; lib SPY/APP spy; exe RANDOM random; exe SECLIST seclist; exe SIMBAD simbad
exe TAKEOWN takeown; exe RCDUMP rcdump; lib VI vi; exe NEWINVTP winvtp; lib WZMAIL/DH.NEW/LIB; lib WZMAIL/MAIL1.0 wzmail
dll FASTIMER; lib WAP/DUMP apf32dmp; lib WAP/CONVERT apf32cvt; for c in FERNEL BASE ADVAPI CRTDLL; do dll WAP/$c; done
exe WINDIFF/WINDIFF windiff; lib WINDIFF/CLIENT; exe WINDIFF/SERVER sumserve; lib ZOOMIN zoomin; lib COMPDIR compdir
lib PVIEWER pviewer; exe ROSHARE roshare; exe A2COFF a2coff; lib CLEARMEM clearmem; lib EDITREG editreg; lib XEROX xerox
exe TILE tile

# the tools no DIRS names
lib CNTFS cntfs; exe DETAB detab; exe DIS32 dis32; exe DISKGEOM diskgeom; lib FILEVER filever; exe FLEX flex; exe GETBIN getbin
exe LOGOFF logoff; exe MODSEL modesel; lib MPTEST mptest; exe MRGSCOMP mrgscomp mrgdiff; exe MHLSRV/CLIENT mhlcli
exe MHLSRV/SERVER mhlsrv; exe NEWER newer; exe NTFSDUMP ntfsdump; lib OWNTREE owntree; exe RSHELL/NTCLIENT rcmd
lib RSHELL/SERVER rcmdsrv; lib SFS sfs-gate sfs-page; lib SYMREF/SRDAEM srdaem; exe SYMREF/SYMREF symref; lib TIMTP timtp
lib LZTEST lztest; exe VMTESTS poolkill vmperf32 vmtest vmstress tfork pageit color; exe XMAS xmas; exe YACC yacc
lib ZD zdsim
# the templates of the scanners flex and the parsers yacc write (flex -Sc:\sdktools\skeleton.flx, yacc
# finds yypars.c on the INCLUDE path)
T="$R/../PRIVATE/SDKTOOLS/FLEX/SKELETON.FLX $R/../PRIVATE/SDKTOOLS/YACC/YYPARS.C"
step templates $T -- install -m 644 $T $R/sdcard/root/$S/
