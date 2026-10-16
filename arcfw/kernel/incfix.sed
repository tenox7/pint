# NT #include names are case-insensitive and may carry Windows directory parts;
# the build host is case-sensitive and every header is farmed flat + lower-case
# into an -I directory, the sys\ headers into its sys directory. Lower-case the
# name and drop any directory part but sys\.
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*["<])[sS][yY][sS][\\/]([^">\\/]+[">])/\1sys\/\L\2/
/^[[:space:]]*#[[:space:]]*include[[:space:]]*["<]sys\//!{
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*")([^"]*[\\/])?([^"\\/]+)"/\1\L\3"/
s/^([[:space:]]*#[[:space:]]*include[[:space:]]*<)([^>]*[\\/])?([^>\\/]+)>/\1\L\3>/
}
