#!/usr/bin/perl
# riscdef.pl I386DEF [-x STRING]... [-r FROM TO]... > DEF: the module definition file of a DLL of
# C++ exports for a RISC machine (ARM) from the one of the Intel machine, as the RISC .def files
# of the class library have their names (ntmfc\src\mips against ntmfc\src\i386): each decorated
# name with the calling convention of its functions, and of the pointers to functions and to
# member functions among its types, __cdecl, the one convention of a RISC machine: __thiscall (E),
# __stdcall (G), __fastcall (I) and __pascal (C) are __cdecl (A), their __export forms (F, H,
# J, D) __cdecl __export (B). The ordinals stay. An export whose decorated name holds a STRING
# of -x is left out: a function of the Intel machine alone; a FROM of -r in a name is TO.
# A template argument of the address of an object of C names (the IID of the COM templates of
# MFC, $1IID_IMoniker) is, as the compiler of today decorates it, its C++ symbol (a const GUID:
# $1?IID_IMoniker@@3U_GUID@@B).
use strict; use warnings;

my ($def, @skip, @ren);
while (@ARGV) {
    my $a = shift;
    if ($a eq '-x') { push @skip, shift; }
    elsif ($a eq '-r') { push @ren, [shift, shift]; }
    else { $def = $a; }
}
die "usage: riscdef.pl I386DEF [-x STRING]... [-r FROM TO]...\n" unless defined $def;

# The decorated name is read from $s at $p and copied to $o, its calling conventions changed.
my ($s, $p, $o);
sub peek { return substr($s, $p, 1); }
sub take { my $c = substr($s, $p, 1); die "ends early\n" if $c eq ''; $p++; $o .= $c; return $c; }
sub cc {
    my $c = substr($s, $p, 1);
    die "no calling convention at $p\n" unless $c =~ /^[A-J]$/;
    $p++; $o .= $c =~ /[ACEGI]/ ? 'A' : 'B';
}
# a number: 0-9 (1 to 10), else hexadecimal digits A-P up to @; negative after ?
sub number {
    my $c = take();
    $c = take() if $c eq '?';
    return ord($c) - ord('0') + 1 if $c =~ /[0-9]/;
    my $n = 0;
    while ($c ne '@') { die "bad number at $p\n" unless $c =~ /[A-P]/; $n = $n * 16 + ord($c) - ord('A'); $c = take(); }
    return $n;
}
sub ident { 1 while take() ne '@'; }
# a qualified name: its fragments, then @
sub qname { fragment() while peek() ne '@'; take(); }
sub fragment {
    my $c = peek();
    if ($c =~ /[0-9]/) { take(); return; }                      # a name back reference
    if (substr($s, $p, 2) eq '?$') { take(); take(); template(); return; }
    die "nested name at $p\n" if $c eq '?';
    ident();
}
# a template name and its arguments, up to @
sub template {
    ident();
    targ() while peek() ne '@';
    take();
}
sub targ {
    if (peek() ne '$') { type(); return; }
    take(); my $k = take();
    if ($k eq '0') { number(); return; }                        # an integer
    if ($k eq '1') {                                            # an address: a C++ symbol, or a
        if (peek() eq '?') { symbol(); return; }               # C name up to the @ of the list
        my $n = ''; $n .= substr($s, $p++, 1) while peek() ne '@';
        die "address of $n at $p\n" unless $n =~ /^(IID|CLSID)_/;
        $o .= "?$n\@\@3U_GUID\@\@B";
        return;
    }
    die "template argument \$$k at $p\n";
}
sub type {
    my $c = take();
    return if $c =~ /[CDEFGHIJKMNOX0-9]/;                       # simple, void, a back reference
    if ($c eq '_') { take(); return; }                           # __int64, bool and the like
    if ($c =~ /[TUV]/) { qname(); return; }                      # union, struct, class
    if ($c eq 'W') { take(); qname(); return; }                  # enum
    if ($c =~ /[ABPQRS]/) { pointee(); return; }                 # reference, pointer
    if ($c eq 'Y') { my $n = number(); number() for 1 .. $n; type(); return; }   # array
    if ($c eq '?') { take(); type(); return; }                   # cv-qualified
    die "type $c at $p\n";
}
sub pointee {
    my $c = peek();
    if ($c eq '6') { take(); cc(); function(); return; }          # function
    if ($c eq '8') { take(); qname(); take(); cc(); function(); return; }   # member function
    take();                                                      # its cv
    type();
}
# return type (@: none), arguments (X: void; ending in @, or Z: ...) and throw (Z)
sub function {
    if (peek() eq '@') { take(); }
    elsif (peek() eq '?') { take(); take(); type(); }
    else { type(); }
    if (peek() eq 'X') { take(); }
    else {
        while (1) { my $c = peek(); if ($c eq '@' || $c eq 'Z') { take(); last; } type(); }
    }
    die "no throw at $p\n" unless take() eq 'Z';
}
sub symbol {
    take();                                                      # ?
    if (substr($s, $p, 2) eq '?$') { take(); take(); template(); }
    elsif (peek() eq '?') {                                      # an operator, constructor and so on
        take(); my $c = take(); if ($c eq '_') { $c = take(); take() if $c eq '_'; }
    } else { ident(); }
    qname();
    my $c = take();
    if ($c =~ /[0-4]/) { type(); take(); return; }               # data, its storage class
    if ($c =~ /[YZ]/) { cc(); function(); return; }              # global function
    if ($c =~ /[ABEFIJMNQRUV]/) { take(); cc(); function(); return; }   # member function, this cv
    if ($c =~ /[CDKLST]/) { cc(); function(); return; }          # static member function
    if ($c =~ /[GHOPWX]/) { number(); take(); cc(); function(); return; }   # adjustor thunk
    die "kind $c at $p\n";
}
sub risc {
    ($s, $p, $o) = ($_[0], 0, '');
    return $s unless $s =~ /^\?/;
    symbol();
    die "left over at $p\n" if $p != length $s;
    return $o;
}

open(my $h, '<', $def) or die "riscdef: $def: $!\n";
my $bad = 0;
while (my $l = <$h>) {
    $l =~ s/[\r\x1a]//g;
    # (an export: indented, with or without its ordinal)
    if ($l =~ /^(\s+)([^\s;]+)(.*)$/s) {
        my ($a, $n, $b) = ($1, $2, $3);
        next if grep { index($n, $_) >= 0 } @skip;
        $n =~ s/\Q$_->[0]\E/$_->[1]/g for @ren;
        my $r = eval { join '=', map { risc($_) } split /=/, $n };
        if (!defined $r) { print STDERR "riscdef: $n: $@"; $bad++; next; }
        $l = "$a$r$b";
    }
    print $l;
}
exit($bad ? 1 : 0);
