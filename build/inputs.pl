#!/usr/bin/perl
# inputs.pl RASPI ROOT COMPONENT [link|exe]: the inputs of the build step of a component (stamp.sh). One of the port
# (ROOT under /work/RASPI) is its directory and what it shares in the directories of the port its SOURCES INCLUDES
# names: their files and the directories in them that are no component (apps/games: arch.h, the levels of
# Sokoban). One of the NT tree is the port's versions of its sources and of its
# parent's (ported, or apps for the applications: portdir.sh) and its ARM sources (kernel/<component>.arm
# or the sources.arm of its port directory, and the files it lists). With link (a DLL, a driver) or exe (a
# program), also the libraries it links, as the linker finds them: those of its SOURCES (of a DLL or driver the
# LINKLIBS and TARGETLIBS, make-dll.sh and make-driver.sh; of a program its UMLIBS, LINKLIBS and the TARGETLIBS of
# a PROGRAM, make-exe.sh) and the system and C run-time ones, of obj (kernel mode) or obj/um.
use strict; use warnings;
my ($r, $root, $comp, $link) = @ARGV;
sub host { return $_[0] =~ s{^/work/RASPI/}{$r/}r =~ s{^/work/}{$r/../}r; }
sub port { my $p = shift; my ($t) = split m{/}, $p; return grep({ $_ eq ($t // "") } qw(sdktools)) ? "$r/apps/$p" : "$r/ported/$p"; }
my $dir = host("$root/$comp");
if ($root =~ m{^/work/RASPI/}) {
    print "$dir\n";
    my ($s) = grep { -f } map { "$dir/$_" } qw(sources);
    my ($h, $t) = (undef, "");
    $t = do { local $/; <$h> } =~ s/\r//gr =~ s/\\\n/ /gr if $s && open($h, '<', $s);
    for my $i (map { split /;/ } $t =~ /^\s*INCLUDES\s*=(.*)/mg) {
        $i =~ s/^\s+|\s+$//g;
        next if $i eq "" || $i =~ /\$\(/;
        my $p = $dir;
        for my $c (grep { length && $_ ne "." } split m{[\\/]}, $i) {
            if ($c eq "..") { $p =~ s{/[^/]+$}{}; next; }
            opendir(my $d, $p) or last;
            my ($n) = grep { lc eq lc $c } readdir $d;
            $p = defined $n ? "$p/$n" : "";
            last unless defined $n;
        }
        next unless -d $p && $p ne $dir;
        print "$_\n" for grep { -f || (-d && !-f "$_/sources") } glob "$p/*";
    }
} else {
    my $rel = lc "$root/$comp" =~ s{^/work/private/}{}r;
    print port($rel), "\n";
    (my $parent = $rel) =~ s{/[^/]+$}{};
    print "$_\n" for grep { -f } glob port($parent) . "/*";
    my $arm = "$r/kernel/" . lc($comp =~ tr{/}{_}r) . ".arm";
    $arm = port($rel) . "/sources.arm" unless -f $arm;
    (my $ad = $arm) =~ s{/[^/]+$}{};
    if (open(my $a, '<', $arm)) {
        print "$arm\n";
        while (<$a>) { next if /^#/; my ($f) = split; print m{^/} ? host($f) : "$ad/$f", "\n" if $f; }
    }
    # the exports and system service stubs of ntdll (make-dll.sh, gensrv.sh)
    print "$r/kernel/dll/armdef.src\n$r/kernel/ke/services.stb\n$r/kernel/ke/table.stb\n" if $comp eq "dll/daytona";
}
exit 0 unless $link;
# (the port's version of a SOURCES first, which srcmacro.sh reads in place of the tree's)
my $pd = $root =~ m{^/work/RASPI/} ? "" : port(lc "$root/$comp" =~ s{^/work/private/}{}r);
my ($s) = grep { -f } map { ($pd ? "$pd/$_" : ()), "$dir/$_" } qw(up/sources sources);
exit 0 unless $s;
open(my $h, '<', $s) or exit 0;
my $t = do { local $/; <$h> } =~ s/\r//gr =~ s/\\\n/ /gr;
my $km = $t =~ /^\s*TARGETTYPE\s*=\s*(DRIVER|EXPORT_DRIVER|MINIPORT|HAL)\b/mi;
my $o = $km ? "$r/obj" : "$r/obj/um";
my @lib = map { "$o/$_.lib" } $km ? qw(ntoskrnl hal libcntpr) : qw(libc libcmt crtdll kernel32 user32 gdi32 advapi32 ntdll);
my $m = $link ne "exe" ? "LINKLIBS|TARGETLIBS" : $t =~ /^\s*TARGETTYPE\s*=\s*PROGRAM\b/mi ? "UMLIBS|LINKLIBS|TARGETLIBS" : "UMLIBS|LINKLIBS";
for (split /\n/, $t) {
    next unless /^\s*(?:$m)\s*=(.*)/;
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
