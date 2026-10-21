#!/usr/bin/perl
# grp.pl GROUP DIR COLUMNS NAME=PROGRAM...: add items for the programs DIR/PROGRAM.EXE to a
# Program Manager group file (PMCC, the group files of NT's developer setup that REGISTRY.INI
# ProgramGroups names), each with the icon Program Manager's New Program Item takes: the 32x32
# one of 16 colours of the program's first icon (else its first 32x32 one; a program without an
# icon gets Program Manager's). The items are laid out again, COLUMNS a row, 75 pixels apart as
# Program Manager arranges them, and the group window grows by the rows and columns added.
# Item definitions and icons start on 4 bytes: Program Manager reads them in place.
use strict; use warnings;
my ($grp, $dir, $cols, @add) = @ARGV;
die "usage: grp.pl GROUP DIR COLUMNS NAME=PROGRAM...\n" unless @add;
my $d = slurp($grp);
die "$grp: not a group file\n" unless substr($d, 0, 4) eq 'PMCC';
my $cb = unpack 'v', substr($d, 6, 2);
my @rc = unpack 'l4', substr($d, 8, 16);
my ($show, $pname, $cx, $cy, $fmt, $res, $n) = unpack 'v7', substr($d, 32, 14);
my @slot = unpack "v$n", substr($d, 46, 2 * $n);

# the items: [x, y, name, command, icon path, idIcon, wIconVer, cbIconRes, indexIcon, dummy2, dummy3, icon]
my @item;
for my $i (0 .. $#slot) {
    my $o = $slot[$i] or next;
    my ($x, $y) = unpack 'l2', substr($d, $o, 8);
    my ($id, $ver, $cbi, $idx, $d2, $pi, $d3, $pn, $pc, $pp) = unpack 'v10', substr($d, $o + 8, 20);
    $item[$i] = [$x, $y, str($pn), str($pc), str($pp), $id, $ver, $cbi, $idx, $d2, $d3, substr($d, $pi, $cbi)];
}
my @old = grep { defined $item[$_] } 0 .. $#item;
my $cols0 = keys %{{ map { $item[$_][0] => 1 } @old }} || 1;
my $rows0 = keys %{{ map { $item[$_][1] => 1 } @old }} || 1;
for my $a (@add) {
    my ($name, $prog) = split /=/, $a, 2;
    my $file = (grep { lc $_ eq lc "$prog.exe" } map { s{.*/}{}r } glob "$dir/*")[0] or die "grp.pl: no $dir/$prog.exe\n";
    my ($id, $icon) = icon("$dir/$file");
    my $i = 0; $i++ while defined $item[$i];
    $item[$i] = [0, 0, $name, lc "$prog.exe", '', $id // 0, 3, length($icon // ''), 0, 0, 0, $icon // ''];
}

# the order of the items on the screen (rows, then columns), the added ones last, in the grid
my @order = ((sort { $item[$a][1] <=> $item[$b][1] || $item[$a][0] <=> $item[$b][0] } @old),
             grep { my $i = $_; !grep { $_ == $i } @old } grep { defined $item[$_] } 0 .. $#item);
@{$item[$order[$_]]}[0, 1] = (21 + 75 * ($_ % $cols), 72 * int($_ / $cols)) for 0 .. $#order;
my $rows = int(($#order + $cols) / $cols);
$rc[2] += 75 * ($cols - $cols0) if $cols > $cols0;
$rc[3] += 72 * ($rows - $rows0) if $rows > $rows0;

# the group: its header and the slots, its name, then each item, its strings and its icon
$n = @item > $n ? @item : $n;
my $out = substr($d, 0, 46) . "\0" x (2 * $n);
my $gname = str($pname);
substr($out, 34, 2) = pack 'v', length $out;
$out .= "$gname\0";
my @off = (0) x $n;
for my $i (0 .. $#item) {
    my $t = $item[$i] or next;
    $out .= "\0" x (-length($out) & 3);
    $off[$i] = length $out;
    my $p = $off[$i] + 28;
    my $pn = $p; $p += length($t->[2]) + 1;
    my $pc = $p; $p += length($t->[3]) + 1;
    my $pp = $p; $p += length($t->[4]) + 1;
    # (no icon: the end of the icon path, which Program Manager takes as an empty one)
    my $pi = length $t->[11] ? $p + (-$p & 3) : $p - 1;
    $out .= pack('l2 v10', @$t[0, 1, 5, 6, 7, 8, 9], $pi, $t->[10], $pn, $pc, $pp) . join('', map { "$_\0" } @$t[2 .. 4]);
    $out .= "\0" x ($pi - length $out) . $t->[11] if length $t->[11];
}
die "grp.pl: $grp: more than 64K\n" if length $out > 0xffff;
substr($out, 8, 16) = pack 'l4', @rc;
substr($out, 44, 2 + 2 * $n) = pack "v v$n", $n, @off;
substr($out, 6, 2) = pack 'v', length $out;
# (the checksum makes the words of the group add up to 0)
substr($out, 4, 2) = "\0\0";
my $sum = 0; $sum += $_ for unpack 'v*', $out . "\0";
substr($out, 4, 2) = pack 'v', -$sum & 0xffff;
open(my $f, '>:raw', $grp) or die "$grp: $!";
print $f $out, substr($d, $cb);
close $f;

sub str { my $p = shift; substr($d, $p, index($d, "\0", $p) - $p) }
sub slurp { open(my $f, '<:raw', $_[0]) or die "$_[0]: $!\n"; local $/; <$f> }

# icon PROGRAM: the resource id and the bits (RT_ICON) of the icon of a program, none without one
sub icon {
    my $x = slurp(shift);
    my $pe = unpack 'V', substr($x, 0x3c, 4);
    my ($nsec, $opt) = unpack 'v x12 v', substr($x, $pe + 6, 16);
    my $rva = unpack 'V', substr($x, $pe + 24 + 96 + 2 * 8, 4) or return;
    my $file = sub {
        my $r = shift;
        for my $s (0 .. $nsec - 1) {
            my ($vs, $va, $rs, $ra) = unpack 'V4', substr($x, $pe + 24 + $opt + 40 * $s + 8, 16);
            return $r - $va + $ra if $r >= $va && $r < $va + ($vs > $rs ? $vs : $rs);
        }
        die "grp.pl: no section for $r\n";
    };
    my $base = $file->($rva);
    my $entries = sub {
        my $o = (shift) & 0x7fffffff;
        my ($nn, $ni) = unpack 'v2', substr($x, $base + $o + 12, 4);
        map { [unpack 'V2', substr($x, $base + $o + 16 + 8 * $_, 8)] } 0 .. $nn + $ni - 1;
    };
    # the data of the first resource of a type (of the name given): type, name, language
    my $res = sub {
        my ($type, $name) = @_;
        my ($t) = grep { $_->[0] == $type } $entries->(0) or return;
        for my $e ($entries->($t->[1])) {
            next if defined $name && $e->[0] != $name;
            my ($l) = $entries->($e->[1]);
            my ($r, $s) = unpack 'V2', substr($x, $base + $l->[1], 8);
            return substr($x, $file->($r), $s);
        }
        return;
    };
    my $g = $res->(14) or return;                   # RT_GROUP_ICON
    # its icons: width, height, colours, reserved, planes, bits, bytes, id
    my @e = map { [unpack 'C4 v2 V v', substr($g, 6 + 14 * $_, 14)] } 0 .. unpack('v', substr($g, 4, 2)) - 1;
    my ($i) = (grep({ $_->[0] == 32 && $_->[1] == 32 && ($_->[2] == 16 || $_->[5] == 4) } @e),
               grep({ $_->[0] == 32 && $_->[1] == 32 } @e), @e);
    return ($i->[7], $res->(3, $i->[7]));           # RT_ICON
}
