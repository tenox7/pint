#!/usr/bin/perl
# from35.pl SCRIPT [-o FILE] [SOURCE35=SOURCE40...]: carry an edit script of the NT 3.5 port
# (../nt35/RASPI/SCRIPT, tools/ntdiff.pl) over to the NT 4.0 tree. Writes FILE (by default
# the file of SCRIPT): the lines the script copied, as NT 4.0 has them, and the port's own.
# An NT 4.0 change inside the lines copied is taken; one that reaches past them or into the
# port's own lines is left between <<<<<<< 3.5, ======= and >>>>>>> 4.0 to resolve by hand;
# the changes to the lines the script left out are listed. Then ntdiff.pl make writes the
# NT 4.0 script of FILE. A source of the NT 3.5 tree is the same path in lower case, or one
# of the directories that moved, or SOURCE40 given for it.
use strict; use warnings;
use File::Basename qw(dirname);
use File::Path qw(make_path);
use File::Temp qw(tempfile);

my $R = dirname(dirname(__FILE__));
my $N35 = "$R/../../nt35";
my $N40 = "$R/..";
my ($script, $out, %map) = (shift @ARGV);
die "usage: from35.pl SCRIPT [-o FILE] [SOURCE35=SOURCE40...]\n" unless $script;
while (@ARGV) { my $a = shift @ARGV; if ($a eq '-o') { $out = shift @ARGV; next; } my ($o, $n) = split /=/, $a, 2; $map{$o} = $n; }
($out = $script) =~ s/\.ntdiff$// unless $out;

my @moved = ([qr{^private/windows/user/server/}, 'private/ntos/w32/ntuser/kernel/'],
             [qr{^private/windows/user/client/}, 'private/ntos/w32/ntuser/client/'],
             [qr{^private/windows/user/inc/}, 'private/ntos/w32/ntuser/inc/'],
             [qr{^private/windows/gdi/}, 'private/ntos/w32/ntgdi/'],
             [qr{^private/windows/wincon/server/}, 'private/ntos/w32/ntcon/server/'],
             [qr{^private/windows/user/winlogon/}, 'private/windows/gina/winlogon/'],
             [qr{^private/crt32nt/}, 'private/crt32/'], [qr{^private/fp32nt/}, 'private/fp32/']);
sub to40 {
    my $s = shift;
    return $map{$s} if exists $map{$s};
    my $p = lc $s;
    return $p if -f "$N40/$p";
    for my $m (@moved) { (my $q = $p) =~ s/$m->[0]/$m->[1]/; return $q if $q ne $p && -f "$N40/$q"; }
    die "from35: no NT 4.0 file for $s (give $s=SOURCE40)\n";
}
sub slurp { my $f = shift; open(my $h, '<:raw', $f) or die "from35: $f: $!\n"; local $/; my $d = <$h>; return $d // ''; }
sub lines { my $d = slurp(shift); $d =~ s/[\r\x1a]//g; my @l = split /\n/, $d, -1; pop @l if @l && $l[-1] eq ''; return \@l; }
sub indent { my ($l, $d) = @_; return $l if $l eq "" || $d == 0; return (" " x $d) . $l if $d > 0; my $n = -$d; $l =~ s/^ {0,$n}//; return $l; }

my @cmds = split /\n/, slurp("$N35/RASPI/$script"), -1;
pop @cmds if @cmds && $cmds[-1] eq '';
my ($tag, @names) = split ' ', shift @cmds;
die "from35: $script: no #ntdiff line\n" unless $tag eq '#ntdiff';
my @new = map { to40($_) } @names;
my @a = map { lines("$N35/$_") } @names;
my @b = map { lines("$N40/$_") } @new;

# The changes from each NT 3.5 source to its NT 4.0 one (diff): [first, last] of the old
# lines (last = first - 1: an insertion before first) and the new lines.
my @hunks;
for my $s (0 .. $#names) {
    my @f = map { my ($h, $n) = tempfile(); print $h map { "$_\n" } @$_; close $h; $n } $a[$s], $b[$s];
    for (`diff --minimal -b $f[0] $f[1]`) {
        next unless /^(\d+)(?:,(\d+))?([acd])(\d+)(?:,(\d+))?$/;
        my ($o1, $o2, $op, $n1, $n2) = ($1, $2 // $1, $3, $4, $5 // $4);
        ($o1, $o2) = ($o1 + 1, $o1) if $op eq 'a';
        ($n1, $n2) = ($n1 + 1, $n1) if $op eq 'd';
        push @{$hunks[$s]}, [$o1, $o2, $n1, $n2];
    }
    unlink @f;
}
# a line of an NT 3.5 source in its NT 4.0 one, if it is unchanged
sub at { my ($s, $l) = @_; my $d = 0; for (@{$hunks[$s] || []}) { last if $_->[0] > $l; return undef if $l <= $_->[1]; $d = $_->[3] - $_->[1]; } return $l + $d; }

# Lines NT 4.0 has elsewhere: each a line of one of its sources (blanks and spacing aside),
# in runs as long as its sources have them.
sub norm { my $l = shift; $l =~ s/^\s+|\s+$//g; $l =~ s/\s+/ /g; return $l; }
my @joined = map { my $s = $_; "\n" . join("\n", map { norm($_) } @$s) . "\n" } @b;
sub moved {
    my @l = map { norm($_) } @_;
    return 0 unless grep { length } @l;
    for (my $i = 0; $i < @l;) {
        my $n = 0;
        $n++ while $i + $n < @l && grep { index($_, "\n" . join("\n", @l[$i .. $i + $n]) . "\n") >= 0 } @joined;
        return 0 unless $n;
        $i += $n;
    }
    return 1;
}
my (@o, @rep, @used, $noeol, $conflicts);
my $eol = "\n";
# the unchanged lines F..L of source S, as NT 4.0 has them
sub same { my ($s, $f, $l, $d) = @_; return map { indent($b[$s][at($s, $_) - 1], $d) } $f .. $l; }
for my $i (0 .. $#cmds) {
    local $_ = $cmds[$i];
    if (/^\+(.*)$/s) { push @o, $1; next; }
    if ($_ eq '#crlf') { $eol = "\r\n"; next; }
    if ($_ eq '#noeol') { $noeol = 1; next; }
    die "from35: $script: bad line: $_\n" unless /^=(\d+) (\d+) (\d+)(?: (-?\d+))?$/;
    my ($s, $f, $l, $d) = ($1 - 1, $2, $2 + $3 - 1, $4 // 0);
    # the commands before and after copy on from the same source (another indentation):
    # this one takes a line NT 4.0 inserted between the two
    # (and one copied up to the end of the source takes the lines NT 4.0 added at its end,
    # one copied from its start those it added at the start)
    my $goes = $i < $#cmds && $cmds[$i + 1] =~ /^=(\d+) (\d+) / && $1 - 1 == $s && $2 == $l + 1 || $l == @{$a[$s]};
    my $came = $i > 0 && $cmds[$i - 1] =~ /^=(\d+) (\d+) (\d+)/ && $1 - 1 == $s && $2 + $3 == $f;
    $came = 0 if $f == 1;
    $used[$s][$_] = 1 for $f .. $l;
    my $k = $f;
    for my $h (@{$hunks[$s] || []}) {
        my ($o1, $o2, $n1, $n2) = @$h;
        my @nl = map { indent($b[$s][$_ - 1], $d) } $n1 .. $n2;
        if ($o1 > $o2) {
            # lines NT 4.0 inserted before line O1
            next if $o1 < $f;
            last if $o1 > $l + 1;
            if ($o1 == $f || $o1 == $l + 1) {
                last if $o1 == $f && $came;
                if ($o1 == $l + 1 && $goes || $o1 == 1) {
                    push @o, same($s, $k, $o1 - 1, $d), @nl;
                    $k = $o1;
                    push @rep, "taken    $new[$s] $n1-$n2 (inserted)";
                    last if $o1 == $l + 1;
                    next;
                }
                push @rep, "EDGE     $new[$s] $n1-$n2: inserted next to the lines copied ($names[$s] $f-$l), not taken";
                last if $o1 == $l + 1;
                next;
            }
            push @o, same($s, $k, $o1 - 1, $d), @nl;
            $k = $o1;
            push @rep, "taken    $new[$s] $n1-$n2 (inserted)";
            next;
        }
        next if $o2 < $f;
        last if $o1 > $l;
        push @o, same($s, $k, $o1 - 1, $d) if $o1 > $k;
        # the lines of the change the script copied: kept where NT 4.0 has them elsewhere
        # (moved, in this source or another); else NT 4.0's lines are taken when the change
        # is all inside the lines copied, left to resolve by hand when it reaches past them
        my @ol = map { indent($a[$s][$_ - 1], $d) } ($o1 < $f ? $f : $o1) .. ($o2 > $l ? $l : $o2);
        if (moved(@ol)) {
            push @o, @ol;
            push @rep, sprintf("moved    %s %d-%d (%d lines kept, NT 4.0 has them elsewhere)", $names[$s], $o1 < $f ? $f : $o1, $o2 > $l ? $l : $o2, scalar @ol);
        } elsif ($o1 >= $f && $o2 <= $l) {
            push @o, @nl;
            push @rep, sprintf("taken    %s %d-%d (%d lines for %d)", $new[$s], $n1, $n2, $n2 - $n1 + 1, $o2 - $o1 + 1);
        } else {
            push @o, '<<<<<<< 3.5', @ol, '=======', @nl, '>>>>>>> 4.0';
            push @rep, "CONFLICT $new[$s] $n1-$n2: the change of $names[$s] $o1-$o2 reaches past the lines copied ($f-$l)";
            $conflicts++;
        }
        $k = $o2 + 1;
    }
    push @o, same($s, $k, $l, $d) if $k <= $l;
}
# the NT 4.0 changes of the lines the script did not copy
for my $s (0 .. $#names) {
    for (@{$hunks[$s] || []}) {
        my ($o1, $o2, $n1, $n2) = @$_;
        next if grep { $used[$s][$_] } $o1 .. $o2;
        next if $o1 > $o2 && ($used[$s][$o1 - 1] || $used[$s][$o1]);
        push @rep, sprintf("outside  %s %d-%d (%s %d-%d)", $new[$s], $n1, $n2, $names[$s], $o1, $o2);
    }
}
my $path = $out =~ m{^/} ? $out : "$R/$out";
make_path(dirname($path));
open(my $h, ">:raw", $path) or die "from35: $out: $!\n";
my $t = join($eol, @o) . $eol;
$t =~ s/\r?\n\z// if $noeol;
print $h $t;
close $h;
print "$out: ", scalar(@o), " lines from ", join(' ', @new), ($conflicts ? ", $conflicts CONFLICTS" : ""), "\n";
print "  $_\n" for @rep;
exit($conflicts ? 1 : 0);
