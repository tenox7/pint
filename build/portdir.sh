#!/bin/bash
# portdir.sh DIR: the directory of the port's versions of the files of an NT directory
# (/work/private/<path>, lower case): crt/<path> for the C run-time of Visual C++
# (sdktools\vctools\crt), apps/<path> for the NT trees of applications the port keeps
# with its own (apps/sdktools for SDKTOOLS), else ported/<path>; ported/public/<path> for
# one of /work/public (the MFC 4.2 headers).
APPS="sdktools"
case $1 in /work/public/*) echo /work/RASPI/ported/public/$(echo ${1#/work/public/} | tr A-Z a-z); exit ;; esac
p=$(echo ${1#/work/private/} | tr A-Z a-z)
case $p in sdktools/vctools/crt/*) echo /work/RASPI/crt/${p#sdktools/vctools/crt/}; exit ;; esac
case " $APPS " in *" ${p%%/*} "*) echo /work/RASPI/apps/$p ;; *) echo /work/RASPI/ported/$p ;; esac
