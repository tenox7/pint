#!/usr/bin/perl
# The sources of the port that are NT's own with ARM changes are kept as edit scripts
# (FILE.ntdiff) over the NT tree (PRIVATE, PUBLIC), which this repository does not carry.
# An edit script names its NT sources on the first line, then copies their lines
# ("=SOURCE LINE COUNT", "=SOURCE LINE COUNT INDENT" moved by INDENT columns,
# "=SOURCE LINE COUNT INDENT TAGS" with TAGS in front: the "@x:" tags of the setup INFs,
# PRODFILT, after a negative INDENT cut the source's own) and adds the port's own ("+TEXT").
#
#   ntdiff.pl apply  NTROOT DIR|FILE.ntdiff...   write each FILE that is missing or older than its script
#   ntdiff.pl update NTROOT DIR|FILE.ntdiff...   rewrite each script from its FILE, over the same sources
#   ntdiff.pl make   NTROOT FILE SOURCE...       write FILE.ntdiff, SOURCE relative to NTROOT
use strict; use warnings;

my ($cmd, $root, @args) = @ARGV;
die "usage: ntdiff.pl apply|update|make NTROOT ...\n" unless $cmd && $root && @args;

sub slurp { my $f = shift; open(my $h, '<:raw', $f) or die "ntdiff: $f: $!\n"; local $/; my $d = <$h>; return $d // ''; }
sub source { my $d = slurp("$root/$_[0]"); $d =~ s/[\r\x1a]//g; my @l = split /\n/, $d, -1; pop @l if @l && $l[-1] eq ''; return \@l; }
sub spill { my ($f, $d) = @_; my $t = "$f." . $$ . int(rand(1e9)); open(my $h, '>:raw', $t) or die "ntdiff: $f: $!\n"; print $h $d; close $h; rename $t, $f or die "ntdiff: $f: $!\n"; }
sub scripts {
    my @s;
    for my $a (@_) {
        if (!-d $a) { push @s, $a; next; }
        opendir(my $d, $a) or die "ntdiff: $a: $!\n";
        for (sort readdir $d) {
            next if /^\./ || $_ eq 'obj';
            push @s, -d "$a/$_" ? scripts("$a/$_") : /\.ntdiff$/ ? "$a/$_" : ();
        }
    }
    return @s;
}
sub indent { my ($l, $d) = @_; return $l if $l eq '' || $d == 0; return (' ' x $d) . $l if $d > 0; return length($l) >= -$d ? substr($l, -$d) : "\0"; }
# the INF tags a line starts with (PRODFILT: "@x:", one or more) and the rest of it
sub untag { my ($t) = $_[0] =~ /^((?:\@.:)*)/s; return ($t, substr($_[0], length $t)); }

sub generate {
    my ($script) = @_;
    my @cmds = split /\n/, slurp($script), -1;
    pop @cmds if @cmds && $cmds[-1] eq '';
    my ($tag, @names) = split ' ', shift @cmds;
    die "ntdiff: $script: no #ntdiff line\n" unless $tag eq '#ntdiff';
    my @src = map { source($_) } @names;
    my ($out, $eol) = ('', "\n");
    for (@cmds) {
        if (/^\+(.*)$/s) { $out .= $1 . $eol; next; }
        if (/^=(\d+) (\d+) (\d+)(?: (-?\d+)(?: (.+))?)?$/) { my ($d, $p) = ($4 // 0, $5 // ''); $out .= $p . indent($_, $d) . $eol for @{$src[$1 - 1]}[$2 - 1 .. $2 + $3 - 2]; next; }
        if ($_ eq '#crlf') { $eol = "\r\n"; next; }
        if ($_ eq '#noeol') { $out =~ s/\r?\n\z//; next; }
        die "ntdiff: $script: bad line: $_\n";
    }
    return ($out, @names);
}

sub diff {
    my ($file, @names) = @_;
    my $d = slurp($file);
    my $crlf = $d =~ /\r\n/;
    $d =~ s/\r\n/\n/g if $crlf;
    my $noeol = $d ne '' && $d !~ /\n\z/;
    my @t = split /\n/, $d, -1;
    pop @t if @t && $t[-1] eq '';
    my @src = map { source($_) } @names;
    my (%at, %body);
    for my $s (0 .. $#src) {
        for (0 .. $#{$src[$s]}) {
            (my $k = $src[$s][$_]) =~ s/^\s+//; push @{$at{$k}}, [$s, $_];
            my ($g, $b) = untag($src[$s][$_]); push @{$body{$b}}, [$s, $_, $g] if $b =~ /\S/;
        }
    }
    my @out = ("#ntdiff @names");
    push @out, '#crlf' if $crlf;
    my ($ps, $pe) = (-1, -1);
    for (my $i = 0; $i < @t;) {
        my ($bs, $bi, $bl, $bd) = (-1, -1, 0, 0);
        (my $k = $t[$i]) =~ s/^\s+//;
        for my $c (@{$at{$k} || []}) {
            my ($s, $j) = @$c;
            my $in = (length($t[$i]) - length($k)) - (length($src[$s][$j]) - length($k));
            my $l = 0;
            $l++ while $i + $l < @t && $j + $l < @{$src[$s]} && indent($src[$s][$j + $l], $in) eq $t[$i + $l];
            next if $l < $bl || $l == 0;
            next if $l == $bl && !($in == 0 && $bd != 0) && !($in == $bd && $s == $ps && $j == $pe);
            ($bs, $bi, $bl, $bd) = ($s, $j, $l, $in);
        }
        my $chars = 0;
        $chars += () = $t[$_] =~ /\S/g for $i .. $i + $bl - 1;
        if ($bl >= 3 || ($bl >= 1 && $chars >= 20)) {
            push @out, sprintf('=%d %d %d', $bs + 1, $bi + 1, $bl) . ($bd ? " $bd" : '');
            ($ps, $pe) = ($bs, $bi + $bl);
            $i += $bl;
            next;
        }
        # a line of a source under other INF tags: copied without them, the line's own in front
        # (the same source line again first, as a line copied for each CPU)
        my ($g, $b) = untag($t[$i]);
        my @c = grep { $_->[2] ne $g } @{$body{$b} || []};
        if (@c) {
            my ($c) = (grep({ $_->[0] == $ps && $_->[1] == $pe - 1 } @c), grep({ $_->[0] == $ps && $_->[1] == $pe } @c), @c);
            my ($s, $j, $cut) = ($c->[0], $c->[1], -length $c->[2]);
            my $l = 1;
            $l++ while $i + $l < @t && $j + $l < @{$src[$s]} && $g . indent($src[$s][$j + $l], $cut) eq $t[$i + $l];
            push @out, sprintf('=%d %d %d %d', $s + 1, $j + 1, $l, $cut) . ($g ne '' ? " $g" : '');
            ($ps, $pe) = ($s, $j + $l);
            $i += $l;
            next;
        }
        push @out, "+$t[$i]";
        $i++;
    }
    push @out, '#noeol' if $noeol;
    return join("\n", @out) . "\n";
}

if ($cmd eq 'make') {
    my ($file, @names) = @args;
    spill("$file.ntdiff", diff($file, @names));
    exit 0;
}

for my $script (scripts(@args)) {
    (my $file = $script) =~ s/\.ntdiff$//;
    my ($want, @names) = generate($script);
    if ($cmd eq 'update') {
        my $new = diff($file, @names);
        spill($script, $new) if $new ne slurp($script);
        next;
    }
    die "ntdiff: unknown command $cmd\n" unless $cmd eq 'apply';
    next if -e $file && slurp($file) eq $want;
    if (-e $file && -M $file < -M $script) {
        print STDERR "ntdiff: $file has changes its script does not (ntdiff.pl update)\n";
        next;
    }
    spill($file, $want);
}
exit 0;
