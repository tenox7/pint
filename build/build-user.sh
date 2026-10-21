#!/usr/bin/env bash
# Build the NT user mode in two passes over its components in link order, as build.exe
# does: the compile pass makes each library and the import library of each DLL (from its
# .def, as makefile.def did), the link pass each DLL and program, the images into
# sdcard/root/WINNT/System32. The C run-time, and the import library of kernel32 it
# imports, come first. Each step runs when its inputs changed (build/stamp.sh); the steps of
# a pass are jobs (stamp.sh), J at once. drain waits in the compile pass for the steps before
# it, where one takes what they made: a library made of libraries, the header the message
# file of another component makes.
#
# Usage:  build/build-user.sh
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/stamp.sh"
N=/work/PRIVATE/NTOS P=/work/PRIVATE W=/work/PRIVATE/WINDOWS
# lib ROOT COMPONENT, dll ROOT COMPONENT [libs], exe ROOT COMPONENT PROGRAM: the steps of a pass; libs
# for a DLL whose export list is made of its libraries (uixport), whose import library they make
lib() { [ $PASS = link ] || job lib/${1#/work/}/$2 libstep $1 $2; }
dll() { if [ $PASS = compile ]; then job implib/${1#/work/}/$2 implibstep $1 $2 ${3:+link}; else job dll/${1#/work/}/$2 dllstep $1 $2; fi; }
exe() { [ $PASS = compile ] || job exe/${1#/work/}/$2/$3 exestep $1 $2 $3; }
drain() { [ $PASS = link ] || barrier; }
libstep() { ROOT=$1 cstep lib $1 $2 -- $B/make-lib.sh $2; }
implibstep() { IMPLIB=1 ROOT=$1 cstep implib $1 $2 ${3:-} -- $B/make-dll.sh $2; }
dllstep() { INSTALL=1 ROOT=$1 cstep dll $1 $2 link -- $B/make-dll.sh $2; }
exestep() { INSTALL=1 ROOT=$1 cstep exe $1 $2 link -- $B/make-exe.sh $2 $3; }
# pair ROOT COMPONENT:PROGRAM: the library of a program and the program
pair() { lib $1 ${2%%:*}; exe $1 ${2%%:*} ${2#*:}; }
umcrt() ( cd $R/crt && UM=1 ./make-crt.sh && ./make-crt32.sh ST && ./make-crt32.sh MT && ./make-crt32.sh DLL && INSTALL=1 ./make-crtdll.sh )

[ -f $R/obj/um/kernel32.lib ] || IMPLIB=1 ROOT=$W $B/make-dll.sh BASE/CLIENT/DAYTONA
step crt-um $CRT $R/obj/um/kernel32.lib -- umcrt

user() {
# native: ntdll, the session manager, the client-server runtime
lib $N RTL/USER; lib $P URTL; lib $P SM/CLIENT; lib $P SM/SERVER
dll $N DLL/DAYTONA; exe $P SM/SERVER smss
dll $P CSR/SERVER; exe $P CSR/SERVER csrss

# base, rpc, advapi32 (its crypt library, sys003.lib, is made of that of the crypt engine)
for c in BASE/RTL WINNLS WINCON/CLIENT; do lib $W $c; done
dll $W BASE/CLIENT/DAYTONA; dll $W BASE/SERVER
for c in RPC/NDRLIB RPC/NDRMEM RPC/NDR20 RPCUTIL; do lib $P $c; done
dll $P RPC/RUNTIME/MTRT; dll $P RPC/RUNTIME/TRANS/WIN32/CLNTNP; dll $P RPC/RUNTIME/TRANS/WIN32/SVRNP
for c in LSA/COMMON LSA/UCLIENT LSA/CRYPT/ENGINE; do lib $P $c; done; drain
for c in LSA/CRYPT/DLL EVENTLOG/ELFCLNT WINDOWS/SCREG/SC/LIB WINDOWS/SCREG/SC/CLIENT WINDOWS/SCREG/WINREG/LOCAL \
         WINDOWS/SCREG/WINREG/PERFLIB WINDOWS/SCREG/WINREG/LIB WINDOWS/SCREG/WINREG/CLIENT; do lib $P $c; done
dll $W BASE/ADVAPI

# user, gdi, the spooler client, the windows server and the display driver
dll $W USER/CLIENT; lib $W GDI/MATH; dll $W GDI/CLIENT
lib $W PRSINF; dll $W SPOOLER/SPOOLSS/CLIENT
for c in WINCON/SERVER/DAYTONA USER/SERVER GDI/GRE GDI/FONDRV/BMFD GDI/FONDRV/TT/SCALER GDI/FONDRV/TT/TTFD \
         GDI/FONDRV/VTFD GDI/HALFTONE/HT; do lib $W $c; done
dll $W WINSRV/DAYTONA; dll $W GDI/DISPLAYS/FRAMEBUF

# networking: the client libraries of the services, netapi32
for c in NETLIB SVCDLLS/SRVSVC/CLIENT SVCDLLS/SRVSVC/LIB SVCDLLS/WKSSVC/CLIENT SVCDLLS/BROWSER/CLIENT SVCDLLS/BROWSER/COMMON \
         SVCDLLS/LOGONSRV/CLIENT SVCDLLS/REPL/COMMON SVCDLLS/REPL/CLIENT RPCXLATE/RXCOMMON RPCXLATE/RXAPI NETBIOS SVCCTRL/CLIENT \
         SVCDLLS/MSGSVC/CLIENT SVCDLLS/AT/CLIENT SVCDLLS/RPL/CLIENT ACCESS; do lib $P NET/$c; done
dll $P NEWSAM/CLIENT; dll $P NET/RAP; dll $P NET/API

# logon
for c in LSA/CLIENT/USER WINDOWS/SCREG/WINREG/SERVER WINDOWS/USER/WINLOGON/DAYTONA; do lib $P $c; done
exe $W USER/WINLOGON/DAYTONA winlogon

# the local security authority (lsass: lsasrv, samsrv, msv1_0, msprivs), the service controller, the event log
# and the message files of its events (netevent, iologmsg and the network messages, netmsg)
for c in NLSECUTL WINDOWS/SCREG/SC/SVCSLIB WINDOWS/SCREG/SC/SERVER/DAYTONA; do lib $P $c; done
dll $P LSA/SERVER; exe $P LSA/SERVER lsass
dll $P NEWSAM/SERVER; dll $P LSA/MSV1_0; dll $P LSA/MSPRIVS
exe $W SCREG/SC/SERVER/DAYTONA services
dll $P EVENTLOG/SERVER; dll $P EVENTLOG/NETEVENT; dll $N DD/NLSMSG; dll $P NET/MESSAGES

# the keyboard layout, the shell (userinit, progman, control panel) and the tools of
# the first boot of a developer install (cmd, ini, ech, adduser, shutdown, sleep)
dll $W USER/KBDLYOUT/US
lib $W SHELL/USERPRI; lib $W PORT1632; dll $W SHELL/LIBRARY; dll $W MPR
lib $W USER/USERINIT; exe $W USER/USERINIT userinit
lib $W CMD; exe $W CMD cmd
lib $P SDKTOOLS/ZTOOLS/SRC
for t in INI:ini ECH:ech ADDUSER:adduser SHUTDOWN:shutdown SLEEP:sleep; do exe $P SDKTOOLS/${t%%:*} ${t#*:}; done
lib $W DIAMOND/MSZIP/MDI.NT; lib $W DIAMOND/CHUCK/FDI.NT; lib $W SHELL/LZ/LIBS; dll $W SHELL/LZ/LZEXPAND
dll $W SHELL/VERSION; dll $W SHELL/COMDLG; dll $W SHELL/CONTROL/MAIN
exe $W SHELL/CONTROL/CPANEL control
lib $W SHELL/PROGMAN; exe $W SHELL/PROGMAN progman
# the network DDE agent winlogon starts with userinit, the network DDE service and DDE Share
lib $W NETDDE/SRC/TOOLKIT; dll $W NETDDE/SRC/NDEAPI/CLIENT; lib $W NETDDE/SRC/NDDEAGNT; exe $W NETDDE/SRC/NDDEAGNT nddeagnt
lib $W NETDDE/SRC/NTDDECMN; dll $W NETDDE/SRC/NDDENB32; lib $W NETDDE/SRC/NDEAPI/SERVER
for a in NETDDE:netdde DDESHARE:ddeshare; do lib $W NETDDE/SRC/${a%%:*}; exe $W NETDDE/SRC/${a%%:*} ${a#*:}; done

# the common controls and the cards, then the accessories, the games, File Manager, the task list and winver
dll $W SHELL/MSCTLS; dll $W EP/CARDDLL; dll $W EP/ABOUTWEP
for a in SHELL/ACCESORY/NOTEPAD:notepad SHELL/ACCESORY/CALC:calc SHELL/ACCESORY/CLOCK:clock SHELL/ACCESORY/SPECHARS:charmap \
         SHELL/GAMES/SOL:sol EP/WINMINE:winmine EP/FREECELL:freecell SHELL/TASKMAN:taskman SHELL/WINFILE:winfile \
         SHELL/WINVER:winver SHELL/GAMES/REVERSI:reversi EP/CRUEL:cruel EP/GOLF:golf EP/PEGGED:pegged EP/SNAKE:snake \
         EP/TICTAC:tictac EP/OLDTP:taipei; do pair $W $a; done

# WinHelp and its full-text search
for c in MVAPI FTENGINE FTUI; do dll $W WINHELP/$c; done
exe $W WINHELP/WINHELP winhlp32

# OLE 1 and the accessories that use it, ClipBook, Terminal, Recorder, Calendar and the accessibility tools
dll $W OLE/CLIENT; dll $W OLE/SERVER
exe $W SHELL/ACCESORY/CARDFILE cardfile; exe $W SHELL/ACCESORY/ACCESS/SKEYS skeys
lib $W SHELL/ACCESORY/COMMON; lib $W SHELL/ACCESORY/CLIPBOOK/COMMON
dll $W SHELL/ACCESORY/ACCESS/SKDLL; dll $W SHELL/ACCESORY/RECORDER/REC_DLL
for a in PAINTBRS:pbrush CLIPBOOK/DATASRV:clipsrv CLIPBOOK/CLIPVIEW:clipbrd PACKAGER:packgr32 TERMINAL:terminal \
         CALENDAR:calendar ACCESS/ACCESS:access RECORDER/REC_EXE:recorder; do pair $W SHELL/ACCESORY/$a; done

# multimedia: winmm and its drivers (MCI, the MME driver of the kernel sound drivers, the
# synthesizer), the audio compression manager and its codecs, video for Windows and the players
lib $W MEDIA/MMCNTRLS; dll $W MEDIA/WINMM
for c in MMDRV MCIWAVE MCISEQ MCICDA SYNTH MSACM/MSACM MSACM/MSACMMAP MSACM/IMAADPCM MSACM/MSADPCM MSACM/GSM610 MCIOLE; do
    dll $W MEDIA/$c
done
for c in DRAWDIB COMPMAN MCIWND; do lib $W MEDIA/AVI/$c; done
for c in VIDEO MSVIDC MSRLE MCIAVI32; do dll $W MEDIA/AVI/$c; done
for a in SNDVOL:sndvol32 CDPLAYER:cdplayer MPLAYER2:mplay32; do exe $W MEDIA/${a%%:*} ${a#*:}; done

# Windows NT Diagnostics: winmsd, its 3D controls (winmsd.dll) and column list box (clb.dll); the
# header the library makes, wintools.h, before winmsd compiles
lib $P SDKTOOLS/WINTOOLS/LIB; drain; dll $P SDKTOOLS/WINTOOLS/CTL3D/DLL; dll $P SDKTOOLS/WINTOOLS/CLB
lib $P SDKTOOLS/WINTOOLS/WINMSD; exe $P SDKTOOLS/WINTOOLS/WINMSD winmsd

# the utilities: their libraries (ulib, the registry and file system libraries and fmifs; ntlib,
# their sources for the native programs), the command line programs, autochk and autoconv,
# Repair Disk and Disk Administrator, and Performance Monitor; the message file of ulib makes
# their header ULIB\INC\rtmsg.h
for c in ULIB/SRC UREG/SRC IFSUTIL/SRC UFAT/SRC UHPFS/SRC UNTFS/SRC CUFAT/SRC CUHPFS FMIFS/SRC; do dll $P UTILS/$c; done; drain
lib $P UTILS/NTLIB/SRC; lib $N DISKREG; lib $W DIAMOND/MSZIP/MCI.NT; lib $W DIAMOND/CHUCK/FCI.NT
for a in ACLCONV:aclconv CHCP:chcp FINDSTR:findstr KEYB:keyb FORMAT:format MEP/SRC:mep MODE:mode MORE:more \
         RDISK:rdisk REPLACE:replace RESTORE/SRC:restore XCOPY:xcopy AUTOCONV:autoconv FDISK:windisk; do pair $P UTILS/$a; done
for a in ATTRIB:attrib CHKDSK:chkdsk COMP:comp CONVERT:convert DISKCOMP:diskcomp DISKCOPY:diskcopy DOSKEY:doskey \
         FC:fc FIND:find HELP:help LABEL:label PRINT:print RECOVER:recover SORT:sort SUBST:subst TREE:tree \
         AUTOCHK/DAYTONA:autochk; do exe $P UTILS/${a%%:*} ${a#*:}; done
lib $P SDKTOOLS/PERFMON; exe $P SDKTOOLS/PERFMON perfmon

# networking: Windows Sockets (wsock32, its NetBIOS and TCP/IP helpers, the STREAMS library winstrm),
# the ICMP echo API and ping, the LmHosts service (the DNS name resolution of NetBIOS over TCP/IP),
# the DHCP client (its message file makes the header of its library, CLIENT\INC\dhcpmsg.h), the LAN
# Manager services (server, workstation, browser, messenger, alerter, netlogon), the transaction
# server and the net command
for c in SOCKREG LIBUEMUL SOCKUTIL; do lib $P NET/SOCKETS/$c; done
for c in WINSOCK WSHNETBS WINSTRM; do dll $P NET/SOCKETS/$c; done
X=/work/RASPI/apps/net
dll $X wshtcpip; dll $X icmp; dll $X lmhsvc; for p in ping ipconfig hostname; do exe $X $p $p; done
# the RPC transports over TCP/IP, UDP and NetBIOS, and the NT LM security support provider
# (security.dll, and its service, ntlmssps.dll)
for c in CLNTTCP SVRTCP CLNTNB SVRNB DGUDPC DGUDPS; do dll $P RPC/RUNTIME/TRANS/WIN32/$c; done
lib $P NET/SVCDLLS/NTLMSSP/COMMON; dll $P NET/SVCDLLS/NTLMSSP/CLIENT; dll $P NET/SVCDLLS/NTLMSSP/SERVER
D=NET/SOCKETS/TCPCMD/DHCP
lib $P $D/LIB; dll $P $D/CLIENT/NEWNT; drain; lib $P $D/CLIENT/DHCP
for c in SRVSVC/SERVER WKSSVC/SERVER/DAYTONA BROWSER/SERVER MSGSVC/SERVER ALRSVC LOGONSRV/SERVER; do dll $P NET/SVCDLLS/$c; done
lib $P NET/DOSPRINT; dll $P NET/XACTSRV
lib $P NET/NETCMD/COMMON; lib $P NET/NETCMD/MAP32
lib $P NET/NETCMD/NETUSE; exe $P NET/NETCMD/NETUSE net
lib $P NET/NETCMD/NETCMD; exe $P NET/NETCMD/NETCMD net1; dll $P NET/NETCMD/HELP

# the network user interface: the class libraries of NET UI (strings, collections, BLT, the LAN
# Manager objects; uimisc, uiapplib and uilmobj are made of them) in the NETUI DLLs, which export
# what their libraries define (uixport); the security editor (acledit), the LAN Manager network
# provider (ntlanman) with its sharing dialogs (ntlanui) and the network dialogs of MPR (mprui);
# Event Viewer and User Manager (uiadmin is made of their libraries). Then Registry Editor and
# Chat, which take the network provider.
C=NET/UI/COMMON/SRC A=NET/UI/ADMIN
for c in STRING/PCH STRING/STRING COLLECT/PCH COLLECT/COLLECT MISC/FMX MISC/BUFFER MISC/MISC MISC/TRACE MISC/WINTIME \
         MISC/SYSTEM MISC/FSENUM MISC/HEAP MISC/BASE MISC/ERRMAP BLT/PCH BLT/BLT BLT/BLTCC APPLIB/PCH APPLIB/APPLIB \
         MNET32/PCH MNET32/MNET32 MNET32/NETSLOW LMOBJ/PCH LMOBJ/LMOENUM LMOBJ/LMOBJ LMOBJ/LOG PROFILE/PROFILE REG/REG; do
    lib $P $C/$c
done; drain
for c in MISC/BIN APPLIB/BIN LMOBJ/BIN; do lib $P $C/$c; done; drain
for c in DLL0 DLL1 DLL2; do dll $P $C/DLL3/$c libs; done
for c in ACLEDIT/ACLEDIT ACLEDIT/XLATE SHELL/SHELL SHELL/UTIL SHELL/ENUM SHELLUI/SHARE SHELLUI/FILE SHELLUI/MISC SHELLUI/XLATE \
         MPR/XLATE; do lib $P NET/UI/$c; done
for c in ACLEDIT/BIN SHELL/BIN SHELLUI/BIN MPR/MPR; do dll $P NET/UI/$c; done
for c in COMMON/SRC/ADMINAPP/SETFOCUS COMMON/SRC/ADMINAPP/ADMINAPP COMMON/SRC/ADMINAPP/COLWIDTH EVENTVWR/XLATE USER/XLATE \
         USER/USER USER/MINIUSER; do lib $P $A/$c; done; drain; lib $P $A/COMMON/SRC/ADMINAPP/BIN
exe $P $A/EVENTVWR/EVENTVWR eventvwr; exe $P $A/USER/USER usrmgr; exe $P $A/USER/MINIUSER musrmgr
lib $P UTILS/REGEDIT/SRC; exe $P UTILS/REGEDIT/SRC regedt32
lib $W SHELL/ACCESORY/WINCHAT; exe $W SHELL/ACCESORY/WINCHAT winchat

# Server Manager and its Control Panel applets (Server, Services, Devices), User Profile Editor,
# the other Control Panel applets (cursors; display, with the INF parser prsinf; the multimedia
# applet with sound, drivers and the MIDI mapper; hardware profiles; the Type 1 font installer),
# the screen savers (logon.scr is the one of the logon desktop), PIF Editor, Backup and the icons
# of Program Manager (moricons)
for c in XLATE SERVER; do lib $P $A/SERVER/$c; done
exe $P $A/SERVER/BIN srvmgr; dll $P $A/SERVER/CPL
lib $W SHELL/UPEDIT; exe $W SHELL/UPEDIT upedit
for c in CURSORS VIDEO MULTIMED SOUND DRIVERS MIDI PROFILE T1INSTAL; do dll $W SHELL/CONTROL/$c; done
lib $W SHELL/CONTROL/SCRNSAVE/COMMON
for a in BEZIER:ssbezier DEFAULT:scrnsave MARQUEE:ssmarque MYSTIFY:ssmyst STARS:ssstars LOGON:logon; do
    exe $W SHELL/CONTROL/SCRNSAVE/${a%%:*} ${a#*:}
done
lib $P MVDM/VDMUTILS/PIFEDIT; exe $P MVDM/VDMUTILS/PIFEDIT pifedit
lib $P UTILS/NTBACKUP/SRC; exe $P UTILS/NTBACKUP/SRC ntbackup
dll $W SETUP/MORICONS

# Setup: the image help library (the checksums setup verifies, the stack walk of the debuggers),
# the setup DLL the INF scripts call and the INF interpreter, setup.exe
dll $P SDKTOOLS/IMAGEHLP; dll $W SETUP/SETUPDLL
lib $W SETUP/SRC; exe $W SETUP/SRC setup

# netcard detection for the network setup: the library over the detection driver (netdtect.dll)
# and the detection of the netcards Microsoft ships (msncdet.dll, with the Raspberry Pi Ethernet)
dll $N NDIS/DETECT/DLL; dll $P NET/DETECT/DLL

# the Network control panel (ncpa.cpl: the bindings of its Small Prolog rules, the dialogs that run
# the network INFs through setup.exe; its message file, xlate\elfmsg.mc, makes the header elfmsg.h)
# and the TCP/IP configuration it and the INFs call (tcpcfg.dll)
lib $P NET/UI/NCPA/PCH; lib $P NET/UI/NCPA/XLATE; drain
for c in SP C NCPA; do lib $P NET/UI/NCPA/$c; done; dll $P NET/UI/NCPA/BIN
for c in XLATE SRC; do lib $P NET/UI/NCPA/TCPIP/$c; done; dll $P NET/UI/NCPA/TCPIP/BIN

# the TCP/IP services the network control panel installs, hosted by tcpsvcs.exe: the FTP server (its
# administration API, performance counters, Control Panel applet and Server Manager extension) and
# the simple TCP/IP services
D=NET/SOCKETS/TCPCMD/FTPSVC
dll $P $D/CLIENT; dll $P $D/SERVER; dll $P $D/FTPCTRS; dll $P NET/UI/FTPMGR/CPL; dll $P NET/UI/FTPMGR/FTPSMX
dll $P NET/SOCKETS/TCPSVCS/SIMPTCP; exe $P NET/SOCKETS/TCPSVCS/TCPSVCS tcpsvcs

exe /work/RASPI/apps hello hello
}

PASS=compile; user; barrier
PASS=link; user; barrier
