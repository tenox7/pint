# MSVC takes \ in #include paths and finds headers case-insensitively; the mirror
# of the tree that gen-midl.sh makes is lower case.
/^[[:space:]]*#[[:space:]]*include/{
s/\\/\//g
s/[<"][^>"]*[>"]/\L&/
}
