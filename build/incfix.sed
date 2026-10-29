# NT #include names are case-insensitive and may carry Windows directory parts;
# the build host is case-sensitive and every header is farmed flat + lower-case
# into an -I directory, the sys\ headers into its sys directory. Lower-case the
# name and drop any directory part but sys\ - and keep a farmed header named by its path
# (incdirs.sh: a header of another directory by the name of one beside the file).
/^[[:space:]]*#[[:space:]]*include[[:space:]]*"\/tmp\/f\//b
# A header of windows\inc16 is named by its path (private\windows\inc\shell2.h: "..\inc16\shell.h",
# of the same name as the one that includes it or one beside the source): its farm.
/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<]([.][.][\\/])+[iI][nN][cC]16[\\/]/{
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*)["<]([.][.][\\/])+[iI][nN][cC]16[\\/]([^">\\/]+)[">]/\1"\/tmp\/f\/inc16\/\L\3"/
b
}
# A POSIX header named by its directory (the POSIX run-time, the session manager:
# <posix\sys\types.h>, not posix\types.h), which the farm keeps there (mkfarm.sh).
/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<][pP][oO][sS][iI][xX][\\/]/{
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*["<])[pP][oO][sS][iI][xX][\\/][sS][yY][sS][\\/]([^">\\/]+[">])/\1posix\/sys\/\L\2/
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*["<])[pP][oO][sS][iI][xX][\\/]([^">\\/]+[">])/\1posix\/\L\2/
b
}
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*["<])[sS][yY][sS][\\/]([^">\\/]+[">])/\1sys\/\L\2/
/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<]sys\//!{
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*")([^"]*[\\/])?([^"\\/]+)"/\1\L\3"/
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*<)([^>]*[\\/])?([^>\\/]+)>/\1\L\3>/
}
