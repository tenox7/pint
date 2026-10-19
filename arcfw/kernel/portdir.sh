#!/bin/bash
# portdir.sh DIR: the directory of the port's versions of the files of an NT directory
# (/work/PRIVATE/<path>, lower case): apps/<path> for the NT trees of applications the port
# keeps with its own (apps/sdktools for SDKTOOLS), else arcfw/ported/<path>.
APPS="sdktools"
p=$(echo ${1#/work/PRIVATE/} | tr A-Z a-z)
case " $APPS " in *" ${p%%/*} "*) echo /work/RASPI/apps/$p ;; *) echo /work/RASPI/arcfw/ported/$p ;; esac
