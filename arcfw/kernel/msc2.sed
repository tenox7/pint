# The C and C++ Microsoft C 2.0 accepted and the compiler of today does not, as
# the build copies the sources and headers.

# A member function named without a call (gre/pdevobj.cxx): the 1994 compiler
# warned of the missing argument list and called nothing.
s/^\([[:space:]]*\)bDisableHalftone;/\1\&PDEVOBJ::bDisableHalftone;/

# A function of C linkage without a return type, int (gre/textobj.cxx).
s/^extern "C" \([A-Za-z_][A-Za-z_0-9]*\)(/extern "C" int \1(/

# The iostreams: the address of a protected member of a virtual base by its
# qualified name (the address of the member, without parentheses a pointer to
# member), and a temporary of a class whose constructor is protected (here a
# default ios, assigned to reset the ios of a stream: a class derived from ios
# makes it).
s/(&istream::bp!=&ostream::bp)/(\&(istream::bp)!=\&(ostream::bp))/
s/this->ios::operator=(ios());/{ class _ios_default : public ios { public: _ios_default() { } } _Default; this->ios::operator=(_Default); }/

# A parameter of a prototype named by an undeclared type (wowshlp.h, LPSZ): the
# 1994 compiler took it as a name; the string it is, LPSTR.
s/(LPSZ, WORD)/(LPSTR, WORD)/

# An int assigned to an enumeration (midl20\codegen\pickle.cxx): the 1994 compiler
# converted it.
s/pTEInfo->Flags = HasImplicitHandle() ? TYPE_ENCODE_WITH_IMPL_HANDLE : 0;/pTEInfo->Flags = (TYPE_ENCODE_FLAGS)(HasImplicitHandle() ? TYPE_ENCODE_WITH_IMPL_HANDLE : 0);/

# A string literal followed by a macro of a string (midl20\codegen): C++ of today reads
# the name as the suffix of a user-defined literal.
s/"\(FC_FIELD_PAD_MACRO\|LENGTH_VAR_NAME\|NOTIFY_SUFFIX\|PSTUB_MESSAGE_PAR_NAME\|STUB_MESSAGE_VAR_NAME\|VLIST_VAR_NAME\|XMIT_AS_ROUTINE_TABLE_TYPE\)/" \1/g

# A member function named without a call as a condition (regedit/regdata.cxx, the
# share disposition of a resource): the 1994 compiler took its address, never zero.
s/if( \([A-Za-z]*\)->\(IsResourceShare[A-Za-z]*\) )/if( \&IO_DESCRIPTOR::\2 )/
