#!/usr/bin/perl
# setupfiles.pl LAYOUT.INF TXTSETUP.SIF [NAME=FILE...]: the setup source of ARM, laid out as the
# CD of 1996 holds that of a CPU in \<cpu>: each file of layout.inf ([SourceDisksFiles] and
# [SourceDisksFiles.arm]) where the build made it or the NT tree has it, and the programs of text
# setup the CD adds: SETUPLDR, USETUP.EXE, and SYSTEM32\SMSS.EXE (USETUP.EXE again) and NTDLL.DLL,
# which the kernel runs in text setup (its system root is the directory of SETUPLDR). NAME=FILE
# gives a file made for the disk. Prints one SOURCE DESTINATION line per file; the files nothing
# has go to standard error, and the run fails when text setup copies one of them.
#
# A built file is taken where it is installed in sdcard/root (the target directory of layout.inf,
# then its name anywhere there), else from obj; a data file of the tree by its name, or from the
# list below where the tree has different files of one name (each the file of the NT 4.0 CD).
use strict; use warnings;

my ($layout, $sif, @made) = @ARGV;
my $R = '/work';
my @tree = ('/nt/private', '/nt/public');
my $arm = 'arm';

my %special = (
    'system'       => "$R/obj/gen/hives/SETUP/SYSTEM",
    'software'     => "$R/obj/gen/hives/SETUP/SOFTWARE",
    'default'      => "$R/obj/gen/hives/SETUP/DEFAULT",
    'userdiff'     => "$R/obj/gen/hives/SETUP/USERDIFF",
    'setupreg.hiv' => "$R/obj/gen/hives/SETUPREG.HIV",
);
my %pick = map { my ($n, $p) = split / /; ($n, "/nt/$p") } (
    'adp.bin private/ntos/ndis/digi/images/adp.bin',
    'idp_xfs.bin private/ntos/ndis/digi/images/idp_xfs.bin',
    'backgrnd.gif private/inet/ohnt/setupnt/backgrnd.gif',
    'home.htm private/inet/ohnt/setupnt/home.htm',
    'space.gif private/inet/ohnt/setupnt/space.gif',
    'iexplore.cnt private/inet/ohnt/setupnt/iexplore.cnt',
    'iexplore.hlp private/inet/ohnt/setupnt/iexplore.hlp',
    'imgsamp.frm private/wangview/norway/ntfiles/imgsamp.frm',
    'wangimg.cnt private/wangview/norway/help/winnt/wangimg.cnt',
    'cross.cur private/windows/shell/control/cursors/anis/cross.cur',
    'hibeam.cur private/windows/shell/control/cursors/anis/hibeam.cur',
    'ding.wav private/windows/media/sounds/ding.wav',
    'midimap.cfg private/windows/media/midimap/midimap.cfg',
    'net.hlp private/net/netcmd/net.hlp',
    'readme.wri private/windows/setup/readme/readme.wri',
    'setup.txt private/windows/setup/readme/setup.txt',
    'system.ini private/mvdm/bin86/system.ini',
    'system.mdb public/sdk/lib/system.mdb',
    'win.ini private/windows/win.ini',
    'lucon.ttf private/ntos/w32/ntgdi/fonts/lucon.ttf',
);
# and the fonts of both ntgdi\fonts and winnls\fontsup\system: those of ntgdi\fonts
my @prefer = ('/nt/private/ntos/w32/ntgdi/fonts/');
for (@made) { my ($n, $f) = split /=/, $_, 2; $special{lc $n} = $f; }

sub sections {
    my ($f) = @_;
    open(my $h, '<:raw', $f) or die "$f: $!\n"; local $/; my $d = <$h>; close $h;
    $d =~ s/[\r\x1a]//g;
    my (%s, $cur);
    for (split /\n/, $d) {
        if (/^\s*\[([^\]]+)\]/) { $cur = lc $1; next; }
        next if !defined $cur || /^\s*(;|$)/;
        push @{$s{$cur}}, $_;
    }
    return \%s;
}
my $ls = sections($layout);
my %dirs = map { /^\s*(\d+)\s*=\s*"?([^"]*)"?\s*$/ ? ($1, $2) : () } @{sections($sif)->{winntdirectories}};

# the built files: those installed in sdcard/root by path and by name, and those of obj
my (%inst, %instname, %obj);
sub walk {
    my ($dir, $rel) = @_;
    opendir(my $h, $dir) or return;
    for my $e (readdir $h) {
        next if $e eq '.' || $e eq '..';
        my $p = "$dir/$e";
        my $r = $rel eq '' ? lc $e : "$rel/" . lc $e;
        if (-d $p) { walk($p, $r); next; }
        $inst{$r} = $p; push @{$instname{lc $e}}, $p;
    }
}
walk("$R/sdcard/root/WINNT", 'winnt');
walk("$R/sdcard/root/os", 'os');
# the kernel of a multiprocessor machine ([SpecialFiles] of txtsetup.sif) is the one built
$special{'ntkrnlmp.exe'} //= $inst{'winnt/system32/ntoskrnl.exe'};
opendir(my $oh, "$R/obj") or die "$R/obj: $!\n";
for (readdir $oh) { $obj{lc $_} = "$R/obj/$_" if -f "$R/obj/$_"; }
my $gen = "$R/obj/gen/tree/windows/setup/inf/win4/inf/daytona/wks";
opendir(my $gh, $gen) or die "$gen: $!\n";
for (readdir $gh) { $obj{lc $_} //= "$gen/$_" if -f "$gen/$_"; }

# the tree, by name
my %t;
for my $top (@tree) {
    open(my $f, '-|', 'find', $top, '-type', 'f') or die;
    while (<$f>) { chomp; my $n = lc $_; $n =~ s/.*\///; push @{$t{$n}}, $_; }
}
sub same {
    my @d = map { local $/; open(my $h, '<:raw', $_) or die "$_: $!\n"; my $x = <$h>; $x } @_;
    for (@d[1 .. $#d]) { return 0 if $_ ne $d[0]; }
    return 1;
}

my (%e, @order);
for my $s ('sourcedisksfiles', "sourcedisksfiles.$arm") {
    for (@{$ls->{$s} || []}) {
        my ($k, $v) = /^\s*([^=\s]+)\s*=\s*(.*?)\s*$/ or next;
        my @f = map { s/^\s+|\s+$//gr } split /,/, $v, -1;
        push @order, lc $k unless $e{lc $k};
        $e{lc $k} = \@f;
    }
}

my ($fail, %out) = (0);
sub put { my ($s, $d) = @_; print "$s $d\n"; $out{lc $d} = 1; }
for my $n (@order) {
    my @f = @{$e{$n}};
    my ($sub, $dir, $tm, $target) = ($f[1] // '', $f[7] // '', $f[9] // '', lc($f[10] // '') || $n);
    my $src = $special{$n} // $pick{$n};
    if (!defined $src && $dir ne '' && defined $dirs{$dir}) {
        my $p = $dirs{$dir} eq '\\' ? "winnt/$target" : 'winnt/' . lc($dirs{$dir} =~ tr/\\/\//r) . "/$target";
        $src = $inst{$p};
    }
    if (!defined $src && $instname{$n}) {
        my @p = @{$instname{$n}};
        if (@p == 1 || same(@p)) { $src = $p[0]; } else { print STDERR "ambiguous $n: @p\n"; $fail = 1; next; }
    }
    $src //= $obj{$n};
    if (!defined $src && $t{$n}) {
        my @p = @{$t{$n}};
        for my $d (@prefer) { my @q = grep { index($_, $d) == 0 } @p; @p = @q if @q == 1; }
        if (@p == 1 || same(@p)) { $src = $p[0]; } else { print STDERR "ambiguous $n: @p\n"; $fail = 1; next; }
    }
    if (!defined $src) {
        my $copied = $tm ne '' && $tm ne '3';
        print STDERR "missing $n" . ($copied ? " (text setup copies it)" : '') . "\n";
        $fail = 1 if $copied;
        next;
    }
    put($src, $sub eq '' ? "$arm/$n" : "$arm/" . lc($sub =~ s/^\\//r =~ tr/\\/\//r) . "/$n");
}

# the programs the CD adds, and its file list of winnt
my $ntdll = $inst{'winnt/system32/ntdll.dll'};
for ([$obj{'setupldr'}, "$arm/setupldr"], [$obj{'usetup.exe'}, "$arm/usetup.exe"],
     [$obj{'usetup.exe'}, "$arm/system32/smss.exe"], [$ntdll, "$arm/system32/ntdll.dll"],
     [$obj{'dosnet.inf'}, "$arm/dosnet.inf"]) {
    my ($s, $d) = @$_;
    if (!defined $s) { print STDERR "missing $d\n"; $fail = 1; next; }
    put($s, $d) unless $out{$d};
}
exit $fail;
