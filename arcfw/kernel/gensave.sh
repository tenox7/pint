#!/bin/bash
# gensave.sh: keep the generated files but sources this build made (/tmp/f/gen, nttarget.sh and
# mcinc.sh: the NTTARGETFILE0 recipes and message headers of a component) in obj/gen/tree,
# which mkfarm.sh gives to the builds that follow, as NT's build left them in its tree for
# the components that include them.
[ -f /tmp/f/.genbase ] || exit 0
(cd /tmp/f/gen 2>/dev/null && find . -type f -newer /tmp/f/.genbase ! -iname "*.c" ! -iname "*.cxx" ! -iname "*.asm") | while read -r f; do
    install -D -m 644 "/tmp/f/gen/$f" "/work/RASPI/obj/gen/tree/$f"
done
exit 0
