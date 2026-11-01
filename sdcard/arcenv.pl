#!/usr/bin/perl
# arcenv.pl IMAGE NAME=VALUE...: write an environment of the ARC firmware to an SD card image, in the
# three sectors that follow its master boot record (arcfw/arm/jxenvir.c): the "NAME=VALUE" strings,
# each zero terminated, names in upper case, zeros up to 1024 bytes, then the checksum, the one's
# complement of the sum of those bytes (low byte first).
use strict; use warnings;
my ($img, @vars) = @ARGV;
die "usage: arcenv.pl IMAGE NAME=VALUE...\n" unless defined $img;
my $env = join "", map { my ($n, $v) = split /=/, $_, 2; uc($n) . "=$v\0" } @vars;
die "arcenv.pl: the environment is longer than 1022 bytes\n" if length($env) > 1022;
$env .= "\0" x (1024 - length $env);
my $sum = 0;
$sum += $_ for unpack "C*", $env;
my $sectors = $env . pack("V", ~$sum & 0xFFFFFFFF);
$sectors .= "\0" x (3 * 512 - length $sectors);
open(my $f, "+<", $img) or die "arcenv.pl: $img: $!\n";
binmode $f;
seek($f, 512, 0) or die "arcenv.pl: $img: $!\n";
print $f $sectors or die "arcenv.pl: $img: $!\n";
close($f) or die "arcenv.pl: $img: $!\n";
