#!/usr/bin/env bash
# Build the NT user mode in link order, as the NT build walks the tree: the C
# run-time, then each library, DLL and program, the images into
# sdcard/root/WINNT/System32. The import lists of the DLLs that import each
# other come first, from their .def files.
#
# Usage:  build/build-user.sh
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../arcfw/kernel"
N=/work/PRIVATE/NTOS P=/work/PRIVATE W=/work/PRIVATE/WINDOWS
lib() { ROOT=$1 ./make-lib.sh $2; }
dll() { INSTALL=1 ROOT=$1 ./make-dll.sh $2; }
exe() { INSTALL=1 ROOT=$1 ./make-exe.sh $2 $3; }
implib() { [ -f ../../obj/um/$3.lib ] || IMPLIB=1 ROOT=$1 ./make-dll.sh $2; }

implib $W BASE/CLIENT/DAYTONA kernel32
(cd ../crt && UM=1 ./make-crt.sh && ./make-crt32.sh ST && ./make-crt32.sh MT && ./make-crt32.sh DLL && INSTALL=1 ./make-crtdll.sh)

implib $W USER/CLIENT user32; implib $W GDI/CLIENT gdi32; implib $W SPOOLER/SPOOLSS/CLIENT winspool
implib $W WINSRV/DAYTONA winsrv; implib $P NET/API netapi32; implib $P NET/RAP netrap; implib $P NEWSAM/CLIENT samlib
implib $W BASE/ADVAPI advapi32

# native: ntdll, the session manager, the client-server runtime
lib $N RTL/USER; lib $P URTL; lib $P SM/CLIENT; lib $P SM/SERVER
dll $N DLL/DAYTONA; exe $P SM/SERVER smss
dll $P CSR/SERVER; exe $P CSR/SERVER csrss

# base, rpc, advapi32
for c in BASE/RTL WINNLS WINCON/CLIENT; do lib $W $c; done
dll $W BASE/CLIENT/DAYTONA; dll $W BASE/SERVER
for c in RPC/NDRLIB RPC/NDRMEM RPC/NDR20 RPCUTIL; do lib $P $c; done
dll $P RPC/RUNTIME/MTRT; dll $P RPC/RUNTIME/TRANS/WIN32/CLNTNP; dll $P RPC/RUNTIME/TRANS/WIN32/SVRNP
for c in LSA/COMMON LSA/UCLIENT LSA/CRYPT/ENGINE LSA/CRYPT/DLL EVENTLOG/ELFCLNT WINDOWS/SCREG/SC/LIB WINDOWS/SCREG/SC/CLIENT \
         WINDOWS/SCREG/WINREG/LOCAL WINDOWS/SCREG/WINREG/PERFLIB WINDOWS/SCREG/WINREG/LIB WINDOWS/SCREG/WINREG/CLIENT; do lib $P $c; done
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
implib $P LSA/SERVER lsasrv; implib $P NEWSAM/SERVER samsrv
for c in NLSECUTL WINDOWS/SCREG/SC/SVCSLIB WINDOWS/SCREG/SC/SERVER/DAYTONA; do lib $P $c; done
dll $P LSA/SERVER; exe $P LSA/SERVER lsass
dll $P NEWSAM/SERVER; dll $P LSA/MSV1_0; dll $P LSA/MSPRIVS
exe $W SCREG/SC/SERVER/DAYTONA services
dll $P EVENTLOG/SERVER

# the keyboard layout, the shell (userinit, progman, control panel) and the tools of
# the first boot of a developer install (cmd, ini, ech, adduser, shutdown)
dll $W USER/KBDLYOUT/US
lib $W SHELL/USERPRI; lib $W PORT1632; dll $W SHELL/LIBRARY; dll $W MPR
lib $W USER/USERINIT; exe $W USER/USERINIT userinit
lib $W CMD; exe $W CMD cmd
lib $P SDKTOOLS/ZTOOLS/SRC
for t in INI:ini ECH:ech ADDUSER:adduser SHUTDOWN:shutdown; do exe $P SDKTOOLS/${t%%:*} ${t#*:}; done
lib $W DIAMOND/MSZIP/MDI.NT; lib $W DIAMOND/CHUCK/FDI.NT; lib $W SHELL/LZ/LIBS; dll $W SHELL/LZ/LZEXPAND
dll $W SHELL/VERSION; dll $W SHELL/COMDLG; dll $W SHELL/CONTROL/T1INSTAL; dll $W SHELL/CONTROL/MAIN
exe $W SHELL/CONTROL/CPANEL control
lib $W SHELL/PROGMAN; exe $W SHELL/PROGMAN progman
# the network DDE agent winlogon starts with userinit
lib $W NETDDE/SRC/TOOLKIT; dll $W NETDDE/SRC/NDEAPI/CLIENT; lib $W NETDDE/SRC/NDDEAGNT; exe $W NETDDE/SRC/NDDEAGNT nddeagnt

exe /work/RASPI/arcfw/apps hello hello
