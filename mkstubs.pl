# ipxwrapper - Create stub functions from headers
# Copyright (C) 2008 Daniel Collins <solemnwarning@solemnwarning.net>
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

if(@ARGV != 2 && @ARGV != 3) {
	print STDERR "Usage: mkdll.pl <input header> <output code> [<dll name>]\n";
	exit(1);
}

open(STUBS, "<".$ARGV[0]) or die("Cannot open ".$ARGV[0].": $!");
open(CODE, ">".$ARGV[1]) or die("Cannot open ".$ARGV[1].": $!");

print CODE <<EOF;
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <nspapi.h>
#include <ws2spi.h>

void *find_sym(char const *symbol);
EOF

if(@ARGV == 3) {
	print CODE "char const *dllname = \"".$ARGV[2]."\";\n";
}

foreach my $line(<STUBS>) {
	$line =~ s/[\r\n;]//g;
	$line =~ s/\/\*.*\*\///g;
	
	if($line eq "") {
		next;
	}
	
	my $type = "";
	my $func = "";
	my $args = "";
	
	foreach my $word(split(/ /, $line)) {
		if($word =~ /\w+\(/) {
			if($func ne "") {
				$type .= " $func($args";
			}
			
			($func, $args) = split(/\(/, $word, 2);
			next;
		}
		
		if($args ne "") {
			$args .= " $word";
		}else{
			if($type ne "") {
				$type .= " ";
			}
			
			$type .= $word;
		}
	}
	
	$args =~ s/\)$//;
	
	my $argdefs = "void";
	my $argnames = "";
	my $count = 0;
	
	if($args ne "void") {
		foreach my $arg(split(/,/, $args)) {
			if($count == 0) {
				$argdefs = "$arg arg$count";
				$argnames = "arg$count";
			}else{
				$argdefs .= ", $arg arg$count";
				$argnames .= ", arg$count";
			}
			
			$count++;
		}
	}
	
	my $symbol = $func;
	$symbol =~ s/^r_//;
	
	print CODE "\n$type $func($argdefs) {\n";
	print CODE "\tstatic $type (*real_$func)($args) = NULL;\n";
	print CODE "\tif(!real_$func) {\n";
	print CODE "\t\treal_$func = find_sym(\"$symbol\");\n";
	print CODE "\t}\n";
	print CODE "\treturn real_$func($argnames);\n";
	print CODE "}\n";
	
}

close(CODE);
close(STUBS);
