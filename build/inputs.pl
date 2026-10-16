#!/usr/bin/perl
# inputs.pl RASPI ROOT COMPONENT [link]: the inputs of the build step of a component (stamp.sh). One of the port
# (ROOT under /work/RASPI) is its directory; one of the NT tree is the port's versions of its sources and of its
# parent's (arcfw/ported) and its ARM sources (arcfw/kernel/<component>.arm and the files it lists). With link, also
# the libraries it links, as the linker finds them: those of its SOURCES and the system and C run-time ones, of obj
# (kernel mode) or obj/um.
use strict; use warnings;
my ($r, $root, $comp, $link) = @ARGV;
sub host { return $_[0] =~ s{^/work/RASPI/}{$r/}r =~ s{^/work/}{$r/../}r; }
my $dir = host("$root/$comp");
if ($root =~ m{^/work/RASPI/}) {
    print "$dir\n";
} else {
    my $rel = lc "$root/$comp" =~ s{^/work/PRIVATE/}{}r;
    print "$r/arcfw/ported/$rel\n";
    (my $parent = $rel) =~ s{/[^/]+$}{};
    print "$_\n" for grep { -f } glob "$r/arcfw/ported/$parent/*";
    my $arm = "$r/arcfw/kernel/" . lc($comp =~ tr{/}{_}r) . ".arm";
    if (open(my $a, '<', $arm)) {
        print "$arm\n";
        while (<$a>) { next if /^#/; my ($f) = split; print m{^/} ? host($f) : "$r/arcfw/kernel/$f", "\n" if $f; }
    }
    # the exports and system service stubs of ntdll (make-dll.sh, gensrv.sh)
    print "$r/arcfw/kernel/dll/armdef.src\n$r/arcfw/kernel/ke/services.stb\n$r/arcfw/kernel/ke/table.stb\n" if $comp eq "DLL/DAYTONA";
}
exit 0 unless $link;
my ($s) = grep { -f } map { "$dir/$_" } qw(UP/SOURCES SOURCES sources);
open(my $h, '<', $s) or exit 0;
my $t = do { local $/; <$h> } =~ s/\r//gr =~ s/\\\n/ /gr;
my $km = $t =~ /^\s*TARGETTYPE\s*=\s*(DRIVER|EXPORT_DRIVER|MINIPORT|HAL)\b/mi;
my $o = $km ? "$r/obj" : "$r/obj/um";
my @lib = map { "$o/$_.lib" } $km ? qw(ntoskrnl hal libcntpr) : qw(libc libcmt crtdll kernel32 user32 gdi32 advapi32 ntdll);
for (split /\n/, $t) {
    next unless /^\s*(?:UMLIBS|TARGETLIBS|LINKLIBS)\s*=(.*)/;
    for my $l (map { tr{\\}{/}r } grep { /\.lib$/i } split " ", $1) {
        my @d = dirlib($l);
        push @lib, @d ? @d : "$o/" . lc($l =~ s{.*/}{}r);
    }
}
print "$_\n" for @lib;

# a library named by its directory (..\x\obj\*\name.lib, $(BASEDIR)\private\...\obj\*\name.lib): the one
# make-lib.sh made for that directory, obj[/um]/dir/<directory>/<name>, if it did (libpath.sh)
sub dirlib {
    my ($d, $b) = $_[0] =~ m{^(.*?)/?obj/\*/([^/]+)$}i or return;
    my $p = "$root/$comp";
    $p = "/work" if $d =~ s{^\$\((?:BASEDIR|_NTROOT|_NTBINDIR)\)}{};
    for my $c (grep { length && $_ ne "." } split m{/}, $d) {
        if ($c eq "..") { $p =~ s{/[^/]+$}{}; next; }
        opendir(my $h, host($p)) or return;
        my ($n) = grep { lc eq lc $c } readdir $h;
        return unless defined $n;
        $p .= "/$n";
    }
    $p =~ s{^/work/}{};
    return (grep { -f } map { "$r/obj$_/dir/$p/" . lc $b } ("/um", ""))[0] // ();
}
