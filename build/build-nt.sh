#!/usr/bin/env bash
# Build the components of NT 4.0 for ARM, the drivers, the user mode, the programs of the
# setup card and the apps of PINT, in the order they were first built (research/build-order.txt),
# each after what it links and includes. Each step runs when its inputs changed
# (build/stamp.sh), in order; the members of a family (dlls, exes), which take nothing of
# each other, as jobs (stamp.sh), J at once. The DLLs, drivers and programs go where NT setup
# puts them (INSTALL=0: left in obj; DEST: another directory of the disk). build-update.sh
# runs it, after the tools, the C run-time and the kernel.
#
# Usage:  build/build-nt.sh
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
N=/work/private/ntos P=/work/private W=/work/private/windows D=/work/RASPI/drivers A=/work/RASPI/apps
# lib ROOT COMPONENT...: libraries (make-lib.sh); implib ROOT COMPONENT...: the import libraries of
# DLLs alone (make-dll.sh IMPLIB=1); dll, drv ROOT COMPONENT...: DLLs (make-dll.sh), drivers
# (make-driver.sh); exe ROOT COMPONENT PROGRAM: a program (make-exe.sh)
lib()    { local r=$1 c; shift; for c; do ROOT=$r cstep lib $r $c -- $B/make-lib.sh $c; done; }
implib() { local r=$1 c; shift; for c; do IMPLIB=1 ROOT=$r cstep implib $r $c -- $B/make-dll.sh $c; done; }
dll()    { local r=$1 c; shift; for c; do INSTALL=${INSTALL:-1} ROOT=$r cstep dll $r $c link -- $B/make-dll.sh $c; done; }
drv()    { local r=$1 c; shift; for c; do INSTALL=${INSTALL:-1} ROOT=$r cstep drv $r $c link -- $B/make-driver.sh $c; done; }
exe() {
    INSTALL=${INSTALL:-1} ROOT=$1 step exe/$(echo $1/$2/$3 | sed -E 's,^/work/(private/)?,,') $(perl $R/build/inputs.pl $R $1 $2 exe) \
        -- $B/make-exe.sh $2 $3
}
# a family: dlls ROOT COMPONENT..., exes ROOT COMPONENT:PROGRAM...
dlls() { local r=$1 c; shift; for c; do CJ=4 job dll/${r#/work/}/$c dll $r $c; done; barrier; }
exes() { local r=$1 c; shift; for c; do CJ=4 job exe/${r#/work/}/$c exe $r ${c%%:*} ${c#*:}; done; barrier; }

# ---- the import libraries the tools step makes when missing and that of gdi32, which every image step
#      takes (inputs.pl), the lists of user32 (pass 0 of ntuser\inc)
implib $P windows/base/client/daytona
lib $N w32/ntuser/inc/daytona
implib $P ntos/w32/ntuser/client/daytona windows/base/advapi/daytona ntos/w32/ntgdi/client/daytona
implib $N dll/daytona

# ---- the drivers of the boot disk
drv $N fastfat dd/scsiport dd/class2 dd/disk
drv $D bcmemmc

# ---- the SCSI class drivers of every system (layout.inx; text setup copies them): CD-ROM, floppy, CD audio,
#      scanner and tape (on the class library of dd), crash dump; no printer (no printing)
lib $N dd/class
drv $N dd/cdrom dd/sfloppy dd/cdaudio dd/scsiscan/class dd/scsitape/newclass dd/diskdump

# ---- native: ntdll (its LINKLIBS take ole32.lib), smss, csrss
lib $N rtl/user
lib $P urtl sm/client
implib $P ole32/dll/daytona
dll $N dll/daytona
exe $P sm/server smss
dll $P csr/server
exe $P csr/server csrss
dll $P windows/base/server

# ---- kernel32, user32, gdi32, winsrv
lib $P ntos/w32/ntcon/client windows/winnls
dll $W base/client/daytona
lib $P ntos/w32/umode/daytona ntos/w32/ntuser/server/daytona ntos/w32/ntcon/server
implib $P windows/spooler/spoolss/client/km
dll $P ntos/w32/winsrv/daytona
lib $P ntos/w32/ntuser/rtl/client/daytona
dll $P ntos/w32/ntuser/client/daytona
lib $P ntos/w32/ntgdi/math
dll $P ntos/w32/ntgdi/client/daytona

# ---- win32k.sys and the display
lib $P ntos/w32/ntgdi/fondrv/tt/scaler ntos/w32/ntgdi/fondrv/tt/ttfd/daytona ntos/w32/ntgdi/fondrv/bmfd ntos/w32/ntgdi/fondrv/vtfd \
    ntos/w32/ntuser/rtl/kernel/daytona ntos/w32/ntuser/kernel/daytona ntos/w32/ntgdi/halftone/ht ntos/w32/ntgdi/gre/daytona
drv $N w32/kmode/daytona video/port
drv $D bcmvideo
drv $N w32/ntgdi/displays/framebuf

# ---- the RPC run-time, the client libraries of the LSA, the registry and the services; rpcrt4, advapi32
lib $P rpc/ndrlib rpc/ndrmem rpc/ndr20 rpcutil rpc/runtime/rtifs rpc/runtime/mtrt lsa/common lsa/uclient lsa/crypt/engine \
    lsa/crypt/dll lsa/client/user eventlog/elfclnt windows/base/accctrl/src windows/screg/sc/lib windows/screg/sc/client \
    windows/screg/winreg/local windows/screg/winreg/perflib windows/screg/winreg/client windows/screg/winreg/lib
dll $P rpc/runtime/rtdll windows/base/advapi/daytona

# ---- the client libraries of the network, samlib
lib $P net/svcdlls/srvsvc/client net/svcdlls/srvsvc/lib net/svcdlls/wkssvc/client net/svcdlls/browser/client/daytona \
    net/svcdlls/browser/common/daytona net/svcdlls/dfs/client net/svcdlls/logonsrv/client/daytona net/svcdlls/repl/common \
    net/svcdlls/repl/client net/rpcxlate/rxcommon net/rpcxlate/rxapi net/netbios net/svcctrl/client net/svcdlls/msgsvc/client \
    net/svcdlls/at/client net/svcdlls/rpl/client net/access net/netlib/daytona
implib $P newsam/client net/rap net/api/daytona
dll $P newsam/client

# ---- logon, lsass, the services (the import libraries first: these DLLs import each other)
lib $P windows/screg/winreg/server windows/screg/sc/svcslib nlsecutl
implib $P windows/mpr/daytona windows/gina/userenv lsa/server/dll newsam/server windows/pnp/umpnpmgr windows/shell/shelldll/base
lib $P windows/shell/shelldll/unicode types/uuid windows/shell/common/lib/winnt
dll $P windows/shell/common/comctl32/winnt windows/shell/shelldll/base net/api/daytona windows/mpr/daytona windows/gina/msgina \
    windows/gina/userenv
exe $P windows/gina/winlogon winlogon
exe $P windows/gina/userinit userinit
dll $P newsam/server lsa/msv1_0 lsa/msprivs
exe $P lsa/server/exe lsass
dll $P eventlog/server/daytona eventlog/netevent lsa/server/dll
exe $P windows/screg/sc/server/daytona services
lib $P windows/pnp/lib
dll $P windows/pnp/umpnpmgr net/rap

# ---- the US keyboard layout (pass 0 of us_kbd: KBDTOOL makes the component kbd\tmp\kbdus), the file
#      system and input drivers, the RPC transports of named pipes
lib $N w32/ntuser/kbd/us_kbd
X=$ST/lib_ntos_w32_ntuser_kbd_us_kbd dll $N w32/ntuser/kbd/tmp/kbdus
drv $N npfs mailslot dd/null dd/beep
dll $P rpc/runtime/trans/win32/svrnp rpc/runtime/trans/win32/clntnp
drv $N dd/kbdclass dd/mouclass
drv $D bcmusb

# ---- the console programs of the first boot
exes $P sdktools/adduser:adduser sdktools/shutdown:shutdown sdktools/sleep:sleep
lib $P sdktools/ztools/src
exes $P sdktools/ini:ini sdktools/ech:ech windows/cmd/cmd:cmd

# ---- Explorer and the shell DLLs, setupapi (setupapi and cfgmgr32 import each other)
lib $P windows/shell/cabinet/base
DEST=WINNT exe $P windows/shell/cabinet/base explorer
lib $W shell/lz/libs
dll $W shell/lz/lzexpand
lib $W diamond/chuck/fdi.nt diamond/mszip/mdi.nt diamond/qstub
dll $W shell/version
exe $W shell/rundll32 rundll32
dll $W shell/comdlg media/winmm
exe $W shell/applets/systray systray
lib $W shell/welcome
DEST=WINNT exe $W shell/welcome welcome
lib $W diamond/quantum/qdi.nt
implib $W setup/setupapi
dll $W pnp/cfgmgr32 setup/setupapi shell/control/mmsys
lib $W shell/userpri prsinf
dll $W shell/control/t1instal
lib $W netdde/src/toolkit
dll $W setup/syssetup

# ---- the Control Panel, the screen savers
lib $W shell/ctrlfldr
exe $W shell/ctrlfldr control
dll $W shell/cpls/main shell/cpls/utc shell/cpls/desk shell/cpls/intl shell/cpls/ports shell/cpls/access shell/cpls/appwiz \
    shell/control/console shell/control/joy
lib $W shell/control/devapps/setup shell/control/devapps/pcmcia/pcminfo shell/control/devapps/pcmcia/shell \
    shell/control/devapps/scsi/shell shell/control/devapps/ctape/shell shell/control/devapps/ctape/tapedev \
    shell/control/devapps/llist shell/control/scrnsave/common/scrnsave shell/control/scrnsave/common/scrnsavw
exes $W shell/control/scrnsave/bezier:ssbezier shell/control/scrnsave/default:scrnsave shell/control/scrnsave/marquee:ssmarque \
    shell/control/scrnsave/mystify:ssmyst shell/control/scrnsave/stars:ssstars shell/control/scrnsave/logon:logon
dll $W shell/control/ups shell/control/devapps/cpl

# ---- Task Manager (taskmgr imports vdmdbg), Program Manager, File Manager, Registry Editor, the
#      accessories, the games, About Windows NT (winver)
lib $W shell/taskman
exe $W shell/taskman taskman
dll $P mvdm/vdmdbg
exe $W shell/sysmon taskmgr
lib $W shell/progman port1632
exe $W shell/progman progman
exe $W shell/winfile winfile
DEST=WINNT exe $W shell/regedit regedit
lib $W shell/accesory/notepad shell/accesory/clock shell/accesory/spechars ep/winmine ep/freecell shell/runonce shell/winver
dll $W ep/carddll rover/filesync/linkinfo
exes $W shell/accesory/notepad:notepad shell/accesory/calc:calc shell/accesory/clock:clock shell/accesory/spechars:charmap \
    shell/games/sol:sol ep/winmine:winmine ep/freecell:freecell shell/runonce:runonce shell/grpconv:grpconv shell/winver:winver

# ---- OLE, RpcSs, Winsock 2, the NTLM security provider
O=ole32
lib $P dcomidl $O/common/daytona $O/common/cruntime/daytona $O/com/inc/daytona $O/com/coll/daytona $O/com/debug/daytona \
    $O/com/moniker2/daytona $O/com/util/daytona $O/com/dcomrem/daytona $O/com/dde/client/daytona $O/com/dde/server/daytona \
    $O/com/objact/daytona $O/com/rot/daytona $O/com/class/daytona $O/com/accctrl/daytona $O/com/wx86grpa/daytona \
    $O/ilib/daytona $O/ole232/inc/daytona $O/ole232/advise/daytona $O/ole232/base/daytona $O/ole232/cache/daytona \
    $O/ole232/clipbrd/daytona $O/ole232/debug/daytona $O/ole232/drag/daytona $O/ole232/inplace/daytona \
    $O/ole232/stdimpl/daytona $O/ole232/ole1/daytona $O/ole232/util/daytona $O/stg/async/docfile/daytona \
    $O/stg/docfile/daytona $O/stg/exp/daytona $O/stg/msf/daytona $O/stg/simp/daytona $O/stg/props/daytona \
    $O/oleprx32/proxy/daytona $O/dcomss/olescm/daytona $O/dcomss/objex/daytona
dll $P rpc/locator/client
implib $P sdktools/imagehlp
lib $P rpc/runtime/epmap
dll $P $O/dll/daytona
lib $P net/sockets/libuemul net/sockets/sockreg net/sockets/sockutil
dll $P net/sockets/winsock2/ws2help/daytona
lib $P net/sockets/winsock2/dll/common
dll $P net/sockets/winsock2/dll/winsock2 net/sockets/winsock2/wsp/msafd net/sockets/winsock2/wsock32/mswsock \
    net/sockets/winsock2/wsock32/dll
lib $P net/svcdlls/ntlmssp/package net/svcdlls/ntlmssp/common/export
dll $P net/svcdlls/ntlmssp/client/export
exe $P $O/dcomss/wrapper/daytona rpcss

# ---- the network drivers
drv $N ndis/ndis40/mp
drv $D bcmeth
drv $N tdi/wrapper
lib $N tdi/tcpip/ip/mp
drv $N tdi/tcpip/tcp/mp afd/daytona
lib $N nbt/nbt/mp
drv $N nbt/nt/mp mup/daytona
lib $N bowser/daytona
drv $P lsa/client/ssp
lib $N smbtrsup
drv $N rdr/daytona srv/daytonam netbios

# ---- the network services and their tools, the NET command
dll $P net/sockets/wshtcpip net/sockets/wshnetbs net/sockets/winsock2/wsock32/rnrprov
exe $P net/sockets/winsock2/setup ws2setup
dll $P rpc/runtime/trans/win32/clntcomn rpc/runtime/trans/win32/svrcomn net/svcdlls/ntlmssp/server/export
# (the pass 0 of dhcpcsvc.dll and its import library first, as build.exe made them before the
# libraries: the dhcpmsg.h of client\inc, which dhcpcli2.lib takes)
implib $P net/sockets/tcpcmd/dhcp/client/newnt/daytona
lib $P net/sockets/tcpcmd/dhcp/lib net/sockets/tcpcmd/dhcp/client/dhcp
dll $P net/sockets/tcpcmd/dhcp/client/newnt/daytona
lib $P net/svcdlls/lls/common
dll $P net/svcdlls/lls/ntlsapi
lib $P net/dosprint
implib $P net/svcdlls/browser/server/daytona
dll $P net/xactsrv net/svcdlls/srvsvc/server/daytona net/svcdlls/wkssvc/server/daytona net/svcdlls/browser/server/daytona \
    net/svcdlls/msgsvc/server net/svcdlls/alrsvc net/svcdlls/logonsrv/server net/messages net/netcmd/help
lib $P net/netcmd/common net/netcmd/map32 net/netcmd/netuse net/netcmd/netcmd
exes $P net/netcmd/netuse:net net/netcmd/netcmd:net1
dll $P ntos/dd/nlsmsg net/sockets/winstrm
lib $P net/snmp/agent/authapi
dll $P net/snmp/agent/common sdktools/perfctrs
lib $P net/svcdlls/repl/server
exe $P net/svcimgs/lmrepl lmrepl
lib $P rpc/locator/newservr/data
exes $P rpc/locator/newservr/server:locator sdktools/netjoin:netjoin windows/screg/sc/sc:sc

# ---- NET UI: the class libraries, combined (misc\bin, applib\bin, lmobj\bin\daytona), the NETUI DLLs
#      (export lists made by uixport of their libraries), acledit, ntlanman, ntlanui, mprui
NU=net/ui/common/src
lib $P $NU/string/pch $NU/string/string $NU/collect/pch $NU/collect/collect $NU/misc/fmx $NU/misc/buffer $NU/misc/misc \
    $NU/misc/trace $NU/misc/wintime $NU/misc/system $NU/misc/fsenum $NU/misc/heap $NU/misc/base $NU/misc/errmap $NU/blt/pch \
    $NU/blt/blt $NU/blt/bltcc $NU/applib/pch $NU/applib/applib $NU/mnet32/pch $NU/mnet32/mnet32 $NU/mnet32/netslow \
    $NU/lmobj/pch $NU/lmobj/lmoenum $NU/lmobj/lmobj $NU/lmobj/log/daytona $NU/profile/profile $NU/reg/reg $NU/misc/bin \
    $NU/applib/bin $NU/lmobj/bin/daytona
dll $P $NU/dll3/dll0 $NU/dll3/dll1/daytona $NU/dll3/dll2
lib $P net/ui/acledit/acledit net/ui/acledit/xlate net/ui/shell/shell/daytona net/ui/shell/util net/ui/shell/enum/daytona \
    net/ui/shellui/share net/ui/shellui/file net/ui/shellui/misc net/ui/shellui/xlate/daytona net/ui/mpr/xlate
dll $P net/ui/acledit/bin net/ui/shell/bin/daytona net/ui/shellui/bin/daytona net/ui/mpr/mpr

# ---- the parts of the Network control panel, the Server Manager control panel, the user profile extension
lib $P net/ui/ncpa1.1/sp net/ui/ncpa1.1/classes/src
dll $P net/ui/ncpa1.1/netcfg/daytona net/ui/ncpa1.1/ipx net/ui/ncpa1.1/tcpip
lib $P net/ui/admin/server/xlate net/ui/admin/server/server
dll $P net/ui/admin/server/cpl net/ui/profext

# ---- the NET UI admin tools (adminapp: ported/net/ui/admin/common/src/adminapp)
AD=net/ui/admin
lib $P $AD/common/src/adminapp/adminapp $AD/common/src/adminapp/setfocus $AD/common/src/adminapp/colwidth \
    $AD/common/src/adminapp/bin $AD/eventvwr/xlate $AD/eventvwr/eventvwr $AD/user/xlate $AD/user/user $AD/user/miniuser \
    $AD/rplmgr/xlate $AD/rplmgr/lmobj $AD/rplmgr/rplmgr net/svcdlls/logonsrv/monitor $AD/nlmon/xlate $AD/browmon/xlate
exes $P $AD/eventvwr/eventvwr:eventvwr $AD/user/user:usrmgr $AD/user/miniuser:musrmgr $AD/server/bin:srvmgr \
    $AD/rplmgr/rplmgr:rplmgr net/svcdlls/logonsrv/monitor:nlmon $AD/nlmon/nlmon:dommon $AD/browmon/browmon:browmon

# ---- the parts of NetDDE (but its service: the tree has no des.c), the column list box
lib $P windows/netdde/incs windows/netdde/src/ntddecmn
dll $P windows/netdde/src/nddenb32
lib $P windows/netdde/src/ndeapi/server
dll $P windows/netdde/src/ndeapi/client
lib $P windows/netdde/src/ddeshare windows/netdde/src/nddeagnt sdktools/wintools/lib
exes $P windows/netdde/src/ddeshare:ddeshare windows/netdde/src/nddeagnt:nddeagnt
dll $P sdktools/wintools/clb

# ---- the file system utility DLLs, the NT utility library, the console utilities
dll $P utils/ulib/src utils/ureg/src utils/ifsutil/src utils/ufat/src utils/untfs/src utils/cufat/src utils/fmifs/src
lib $P utils/ntlib/src utils/chcp utils/keyb utils/format utils/mode utils/more utils/replace utils/restore/src
exes $P utils/chcp:chcp utils/keyb:keyb utils/format:format utils/mode:mode utils/more:more utils/replace:replace \
    utils/restore/src:restore utils/attrib:attrib utils/chkdsk:chkdsk utils/chkntfs:chkntfs utils/comp:comp \
    utils/convert:convert utils/diskcomp:diskcomp utils/diskcopy:diskcopy utils/doskey:doskey utils/fc:fc utils/find:find \
    utils/findstr:findstr utils/help:help utils/label:label utils/print:print utils/recover:recover utils/sort:sort \
    utils/subst:subst utils/tree:tree utils/compact:compact utils/xcopy:xcopy utils/autochk/daytona:autochk \
    utils/autoconv/daytona:autoconv

# ---- the cabinet libraries of RDISK, Registry Editor, Disk Administrator, Chat
lib $P windows/diamond/mszip/mci.nt windows/diamond/quantum/qci.nt windows/diamond/chuck/fci.nt utils/rdisk utils/regedit/src \
    utils/windisk/debug utils/windisk/util utils/windisk/controls ntos/diskreg windows/shell/accesory/winchat
exes $P utils/rdisk:rdisk utils/regedit/src:regedt32 utils/windisk/src:windisk windows/shell/accesory/winchat:winchat

# ---- the Network control panel (it takes the classes netcfg.dll exports), its setup DLL, System
dll $P net/ui/ncpa1.1/ncpa1.1/daytona net/ui/ncpa1.1/netsetup/daytona windows/shell/cpls/system

# ---- Windows Help 4 (winhstb: the System32\WINHLP32.EXE of the setup card and, as layout.inx puts it, of the disk)
dll $P windows/win4help/ftsrch
DEST=WINNT exe $P windows/win4help/winhelp winhlp32
INSTALL=0 exe $P windows/win4help/winhstb winhstb
step winhlp32 $R/obj/WINHSTB.EXE -- cp $R/obj/WINHSTB.EXE $R/sdcard/root/WINNT/System32/WINHLP32.EXE

# ---- Performance Monitor, Windows NT Diagnostics, ClipBook, the input locale indicator, the font viewer,
#      the Schedule service and AT, small tools; imagehlp, rebase and imagecfg (sdcard/rebase.sh), Dr. Watson
lib $P sdktools/perfmon windows/shell/accesory/clipbook/common windows/shell/accesory/clipbook/datasrv \
    windows/shell/accesory/clipbook/clipview windows/shell/fontfldr/fontview net/svcdlls/at/server windows/shell/lz/dos/expand
dll $P sdktools/cntrtext/common
exes $P sdktools/perfmon:perfmon sdktools/wintools/winmsd:winmsd windows/shell/accesory/clipbook/datasrv:clipsrv \
    windows/shell/accesory/clipbook/clipview:clipbrd windows/shell/internat/exe:internat windows/shell/fontfldr/fontview:fontview \
    net/svcimgs/at:atsvc net/svcdlls/at/atcmd:at sdktools/cacls:cacls sdktools/diskperf:diskperf \
    sdktools/cntrtext/lodctr:lodctr sdktools/cntrtext/unlodctr:unlodctr windows/shell/lz/dos/expand:expand \
    windows/gina/savedump:savedump
dll $P sdktools/imagehlp
INSTALL=0 exe $P sdktools/imagehlp rebase
INSTALL=0 exe $P sdktools/imagehlp imagecfg
lib $P sdktools/crashlib
exe $P sdktools/drwatson drwtsn32

# ---- the client of the spooler, winspool.drv (gdi32, comdlg32 and the programs import it); the system has
#      no spooler and no printer drivers
dll $P windows/spooler/spoolss/client/km

# ---- multimedia
MM=windows/media
dll $P $MM/mmdrv $MM/mciwave $MM/mciseq $MM/mcicda $MM/synth $MM/midimap $MM/msacm/msacm $MM/msacm/msacmmap $MM/msacm/imaadpcm \
    $MM/msacm/msadpcm $MM/msacm/gsm610 $MM/msacm/g711
lib $P $MM/avi/drawdib $MM/avi/compman $MM/avi/mciwnd
dll $P $MM/avi/video $MM/avi/msvidc $MM/avi/msrle
implib $P $MM/avi/avifile $MM/avi/avicap.io
dll $P $MM/avi/mciavi32 $MM/dsound
exes $P $MM/mixerapp:sndvol32 $MM/mplayer2:mplay32 $MM/cdplayer:cdplayer

# ---- TAPI, Unimodem, HyperTerminal
dll $P tapi/dev/client
exe $P tapi/dev/server tapisrv
dll $P tapi/dev/cpl.new tapi/dev/perfdll tapi/dev/sp/kmddsp
DEST=WINNT exe $P tapi/dev/apps/dialer dialer
lib $P unimodem/common
dll $P unimodem/serialui unimodem/modemui unimodem/tapisp unimodem/cpl
exe $P unimodem/lights lights
drv $N dd/modem ndis/ndistapi
HT=windows/shell/accesory/hypertrm
lib $P $HT/cncttapi $HT/comwsock $HT/comstd $HT/emu $HT/xfer $HT/ext2
dll $P $HT/ext $HT/tdll
implib $P $HT/htrn_jis
exe $P $HT/term hypertrm

# ---- DirectDraw
dll $P ntos/w32/ntgdi/dciman
lib $P ntos/w32/ntgdi/direct/misc ntos/w32/ntgdi/direct/blitlib ntos/w32/ntgdi/direct/ddhel
dll $P ntos/w32/ntgdi/direct/ddraw
exe $P ntos/w32/ntgdi/direct/ddhelp ddhelp

# ---- small system files
dll $P ntos/seaudit/msaudite ntos/seaudit/msobjs windows/screg/winreg/prflbmsg
exes $P windows/screg/sc/bootok:bootok windows/screg/sc/bootvrfy:bootvrfy windows/gina/mpnotify:mpnotify
dll $P ntos/w32/ntgdi/mf3216 ntos/w32/ntuser/imm/daytona windows/setup/moricons windows/shell/internat/dll windows/setup/legacy/dll
drv $N fsrec dd/ntft

# ---- DirectPlay
lib $P $MM/dplay/misc
dll $P $MM/dplay/dplay $MM/dplay/dpwsock $MM/dplay/serial

# ---- the keyboard layouts (pass 0 of all_kbds: KBDTOOL makes the components kbd\tmp\kbdXX, so a layout
#      takes the stamp of that step); kbdbene and kbdurs, which layout.inf has not, stay in obj
lib $N w32/ntuser/kbd/all_kbds
KB=w32/ntuser/kbd/tmp
X=$ST/lib_ntos_w32_ntuser_kbd_all_kbds dlls $N $KB/{kbdal,kbdbe,kbdblr,kbdbr,kbdbu,kbdca,kbdcr,kbdcz,kbdcz1,kbdda,kbddv,kbdes,kbdest,kbdfc,kbdfi} \
    $KB/{kbdfr,kbdgkl,kbdgr,kbdgr1,kbdhe,kbdhe220,kbdhe319,kbdhela2,kbdhela3,kbdhu,kbdhu1,kbdic,kbdir,kbdit,kbdit142,kbdla} \
    $KB/{kbdlt,kbdlv,kbdlv1,kbdne,kbdno,kbdpl,kbdpl1,kbdpo,kbdro,kbdru,kbdru1,kbdsf,kbdsg,kbdsl,kbdsl1,kbdsp,kbdsw,kbdtuf} \
    $KB/{kbdtuq,kbduk,kbdur,kbdusl,kbdusr,kbdusx,kbdycc,kbdycl}
X=$ST/lib_ntos_w32_ntuser_kbd_all_kbds INSTALL=0 dlls $N $KB/kbdbene $KB/kbdurs

# ---- file systems, disk, network protocols
lib $N lfs/mp
drv $N cntfs/mp cdfs dd/diskperf tdi/nbf tdi/isn/ipx/mp tdi/isn/spx/mp tdi/isn/nb/mp tdi/isn/rip/mp dlc ndis/loop
dll $N tdi/isn/sockhelp
dll $P net/dlc/dlcapi
lib $P net/svcdlls/nwsap/client
implib $P net/svcdlls/nwsap/server
dll $P net/svcdlls/nwsap/server
exe $N tdi/isn/ipxroute ipxroute

# ---- the shell and accessories
dll $P windows/shell/fontfldr/fontext/src windows/shell/diskcopy
lib $P windows/shell/migrate
exe $P windows/shell/migrate shmgrate
dll $P windows/shell/accesory/access/skdll
exes $P windows/shell/accesory/access/skeys:skeys net/svcdlls/upssvc:ups

# ---- the POSIX subsystem
lib $P posix/rtl
dll $P posix/client
lib $P posix/programs/bsdlib
exes $P posix/psxss:psxss posix/programs/psxses:posix posix/programs/pax:pax
INSTALL=0 exe $P posix/programs/hello psxhello

# ---- OpenGL (opengl32 and glu32 import each other: the import library of glu32 first)
GL=windows/opengl
lib $P $GL/glu/nurbs/core $GL/glu/nurbs/clients $GL/glu/nurbs/nt $GL/glu/libtess $GL/glu/libutil $GL/dlist $GL/server/generic \
    $GL/server/pixel $GL/server/soft $GL/server/wgl
implib $P $GL/glu/glu32
dll $P $GL/client $GL/glu/glu32 $GL/mcd/client
drv $P $GL/mcd/server
dll $P $GL/gls/lib
lib $P $GL/toolkits/libaux $GL/scrsave/common/km $GL/scrsave/maze/km $GL/scrsave/pipes/km
exes $P $GL/scrsave/3dfo/km:ss3dfo $GL/scrsave/flwbox/km:ssflwbox $GL/scrsave/maze/km:ssmaze $GL/scrsave/pipes/km:sspipes \
    $GL/scrsave/text3d/km:sstext3d

# ---- more Workstation files; MIBCC (it links snmp.lib, of snmpapi.dll) and MIB.BIN
dll $P sdktools/vctools/ntmfc/ctl3d $MM/avi/avicap.io
lib $P net/portuas
exes $P net/portuas:portuas windows/setup/legacy/exe:setup
dll $P windows/shell/pifmgr rpc/runtime/trans/win32/clntvns rpc/runtime/trans/win32/svrvns windows/shell/accesory/kbdswtch/dll \
    net/sockets/tcpsvcs/simptcp
exe $P net/sockets/tcpsvcs/tcpsvcs tcpsvcs
dll $P windows/shell/fontfldr/ole2map net/sockets/tcpcmd/dhcp/server/client windows/rover/filesync/core windows/rover/filesync/syncui
lib $P windows/rover/filesync/syncapp net/snmp/agent/mibcc
exe $P windows/rover/filesync/syncapp syncapp
dll $P net/snmp/agent/mgmtapi net/snmp/lmmib2
exes $P net/snmp/agent/proxy:snmp net/snmp/agent/snmptrap:snmptrap
step mibcc $R/obj/um/snmp.lib -- $R/tools/make-tools.sh mibcc
X=$R/obj/host/MIBCC.EXE INSTALL=1 lib $P net/snmp/mibs
dll $P $MM/avi/avifile windows/shell/shcompui
exes $P windows/setup/oempre/lfn:autolfn windows/setup/oempre/sprestrt:sprestrt
dll $P windows/base/advapi/wintrust
exe $P windows/base/advapi/digsig/spinit spinit

# ---- AppleTalk and the MacFile control panel (sfmsvc, of the Server, stays in obj: sfmapi takes its validate.obj)
dll $P net/sfm/atalk/messages
drv $P net/sfm/atalk/driver
dll $P net/sfm/atalk/sockets net/sfm/atalk/atkctrs net/ui/ncpa1.1/sfm
dll $P net/sfm/setup/util
INSTALL=0 exe $P net/sfm/afp/service/server sfmsvc
dll $P net/sfm/afp/service/client
lib $P net/sfm/afp/ui/xlate
dll $P net/sfm/afp/ui/afpmgr

# ---- OLE Automation 2.1, the OLE dialogs, OLE 1, Sound Recorder, mciole32
lib $P oleauto/src/dispatch/win32
dll $P oleauto/src/typelib
implib $P oleauto/src/typelib
dll $P ole2ui32/daytona windows/ole/client windows/ole/server $O/olecnv32/daytona
lib $P $MM/sndrec32/o2base
exe $P $MM/sndrec32/soundrec sndrec32
dll $P $MM/mciole

# ---- Quick View (System32\viewers)
QV=windows/shell/quikview
lib $P $QV/viewer/fa $QV/viewer/fi $QV/viewer/lo $QV/viewer/pg $QV/viewer/chunk $QV/viewer/viewer
DEST=WINNT/System32/viewers dll $P $QV/viewer/ut $QV/viewer/sccview
DEST=WINNT/System32/viewers dlls $P $QV/viewer/{debmp,dehex,demet,dess,dewp} \
    $QV/stream/work/{asc8,bmp,drw,exe2,gif,mp,msw,pp,rtf,tiff,w6,wks,wmf,word,work,wp5,wp6,wpf,xl5}
DEST=WINNT/System32/viewers exe $P $QV/qvstub quikview

# ---- shell extensions; the TCP/IP tools of PINT (icmp, ipconfig, hostname: of PINT 3.5; ping, tracert,
#      nbtstat, netstat: of XP SP1; arp, route: of NT 3.5) and its LMHOSTS service
dll $P windows/shell/docprop windows/shell/shole
lib $P windows/shell/lmui/debug
dll $P windows/shell/lmui/ntshrui windows/shell/lmui/netobjs windows/shell/security/rshx
dll $A/net icmp
lib $A/net tcpcmd/nls tcpcmd/common2 tcpcmd/tcpinfo
exes $A/net tcpcmd/ping:ping tcpcmd/tracert:tracert tcpcmd/nbtstat:nbtstat tcpcmd/netstat:netstat tcpcmd/arp:arp \
    tcpcmd/route:route ipconfig:ipconfig hostname:hostname
dll $A/net lmhsvc

# ---- MFC 4.2, RichEdit, WordPad, Paint, dcomcnfg, the Plus! tab, Write and Paintbrush, Object Packager
MFC=sdktools/vctools/ntmfc
lib $P $MFC/src40/uuid2 $MFC/src42/mfcs42u $MFC/src42/mfcs42
dll $P $MFC/src42/mfc42u $MFC/src42/mfc42 windows/richedit/src windows/richedit/src/riched32
lib $P $O/oleui/daytona windows/shell/accesory/packager
dll $P windows/shell/cpls/plus
exes $P windows/shell/accesory/wordpad:wordpad windows/shell/accesory/mspaint:mspaint $O/oleui/daytona:dcomcnfg \
    windows/shell/accesory/write:write windows/shell/accesory/pbrush:pbrush windows/shell/accesory/packager:packager

# ---- the type library of OLE Automation: mktyplib, run on the Wine of the build with ole32 and oleaut32 (the
#      stamps of their steps: the rebase rewrites the DLLs)
INSTALL=0 exe $P oleauto/src/mktyplib mktyplib
INSTALL=1 step stdole $R/obj/MKTYPLIB.EXE $ST/dll_ole32_dll_daytona $ST/dll_oleauto_src_typelib -- $B/make-stdole.sh

# ---- RAS: the drivers, the user mode
drv $N tdi/acd
lib $P net/ras/src/nt/ndis/coherent net/ras/src/nt/ndis/compress
drv $P net/ras/src/nt/ndis/asyncmac
drv $N ndis/ndiswan/export
drv $P net/ras/src/nt/ndis/ip net/ras/src/nt/ndis/filter
RS=net/ras/src
lib $P $RS/common/src/eventlog/lib
dll $P $RS/common/src/eventlog/msgdll $RS/rasfile
lib $P $RS/ui/common/{debug,nouiutil,pbk,uiutil,tapiutil,rnk,phonenum} $RS/rasman/wrapper
dll $P $RS/rasman/rasmxs32 $RS/rasman/serial32 $RS/rasman/rasman32/dll
exe $P $RS/rasman/rasman32/exe rasman
dll $P $RS/rasman/tapi32 $RS/rasman/rasman32/rasauto $RS/rasman/rasman32/rasadhlp $RS/ui/admin/api/api
lib $P $RS/ppp/common $RS/newsrv/common/utils $RS/newsrv/common/debug
dll $P $RS/ppp/{rasiphlp,engine,nbfcp,raschap,rasipcp,rasipxcp,raspap,rascbcp,rasccp}
lib $P $RS/auth/common/{debug,netbios,rasasync,frames,emuldbg}
dll $P $RS/auth/server $RS/auth/client
dll $P $RS/newsrv/{nbfcpdll,netbios,gtwyprxy,adminapi}
exe $P $RS/newsrv/suprprxy rassprxy
lib $P $RS/ui/extapi/xlate
dll $P $RS/ui/rasscrpt $RS/ui/extapi/src
lib $P $RS/ui/rasdial/xlate $RS/ui/rasdial/src
exe $P $RS/ui/rasdial/src rasdial
lib $P $RS/ui/setup/pch $RS/ui/setup/xlate
dll $P $RS/ui/setup/src $RS/ui/setup/digi/isdn $RS/ui/perf $RS/ui/rasdlg
exes $P $RS/ui/rasmon:rasmon $RS/ui/rasphone:rasphone
dll $P $RS/ui/rascpl $RS/ui/rasshell
exes $P $RS/rasman/rasman32/rasautou:rasautou $RS/newsrv/suprv:rassrv
lib $P $RS/ui/admin/rasadmin/pch $RS/ui/admin/rasadmin/xlate
exe $P $RS/ui/admin/rasadmin/rasadmin rasadmin

# ---- the programs of the setup card: text setup (usetup.exe, setupdd.sys, spddlang.sys), SETUPLDR;
#      usetup first, as the dirs of textmode: its pass 0 makes the msg.h of textmode\inc setupdd.sys takes
INSTALL=0 exe $W/setup textmode/user usetup
IMPLIB=1 ROOT=$W/setup/textmode cstep drvimplib $W/setup/textmode spddlang/sbcs -- $B/make-driver.sh spddlang/sbcs
INSTALL=0 drv $W/setup textmode/kernel
INSTALL=0 drv $W/setup/textmode spddlang/sbcs
lib $N boot/setup
step setupldr $R/obj/setupldr.lib $R/obj/boot.lib $R/obj/bldrrtl.lib $R/obj/libcntpr.lib -- $B/make-setupldr.sh

# ---- the apps of PINT: the games (\GAMES), TenoxVNC, Internet Explorer 1.5 and ntfetch (\APPS), the test
#      programs (System32: hello, smptest the processors, nettest raw TCP)
DEST=GAMES exes $A games/2048:2048 games/breakout:breakout games/bubbles:bubbles games/lander:lander games/missile:misl \
    games/snake:ntsnake games/tankz:tankz games/ntrigue:ntrigue games/riscque:riscque games/sokoban:sokoban \
    games/riscoban:riscoban games/wintown:wintown
DEST=APPS exe $A tenoxvnc/win32 tenoxvnc
lib $A ie/shared ie/win32 ie/basic ie/bearssl ie/make
DEST=APPS exe $A ie/make iexplore
DEST=APPS exe $A ntfetch ntfetch
exes $A hello:hello smptest:smptest nettest:nettest
