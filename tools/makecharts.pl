#!/usr/bin/perl
# IPXWrapper - Generate benchmark charts
# Copyright (C) 2015 Daniel Collins <solemnwarning@solemnwarning.net>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published by
# the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

use strict;
use warnings;

unless(@ARGV && ((scalar @ARGV) % 2) == 0)
{
	print STDERR "Usage: $0 <file> <title> <file> <title> ...\n";
	exit(42); # EX_USAGE
}

open(my $gnuplot, "|-", "gnuplot")
	or die "Can't exec gnuplot: $!";

print {$gnuplot} <<EOF;
set terminal png size 1024,1024 enhanced font "Helvetica,20"

set logscale x

set xlabel "Payload size (bytes)"

set offset 0.1, 0.1, 0, 0
set key above title "Legend" box 3
EOF

do_chart("rtt.png",        "Round trip time (ms)",        "(\$11 / 1000)", "(\$4 / 1000)");
do_chart("sendto.png",     "sendto() call duration (µs)", "9",             "2");
do_chart("recv.png",       "recv() call duration (µs)",   "10",            "3");
do_chart("loss.png",       "Packet loss (\%)",            "7");
do_chart("throughput.png", "Throughput (kB/s)",           "(\$8 / 1000)");

close($gnuplot);
exit($?);

sub do_chart
{
	my ($output, $ylabel, $line_ycol, $point_ycol) = @_;
	
	print {$gnuplot} "set output '$output'\n";
	print {$gnuplot} "set ylabel '$ylabel'\n";
	print {$gnuplot} "plot ";
	
	for(my $i = 0; $i < (scalar @ARGV) / 2; ++$i)
	{
		my $file  = $ARGV[$i * 2];
		my $title = $ARGV[$i * 2 + 1];
		
		print {$gnuplot} ", " if($i > 0);
		
		my $xtic       = ($i == int((scalar @ARGV) / 4)) ? ":xtic(1)" : "";
		my $line_style = $point_ycol ? "lines" : "linespoints";
		
		print {$gnuplot} "'$file' using (\$1 * 1.$i):${point_ycol} notitle linecolor $i, " if($point_ycol);
		print {$gnuplot} "'$file' using (\$1 * 1.$i):${line_ycol}$xtic title '$title' with $line_style linecolor $i";
	}
	
	print {$gnuplot} "\n";
}
