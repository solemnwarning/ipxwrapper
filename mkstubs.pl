# IPXWrapper - Generate assembly stub functions
# Copyright (C) 2008-2011 Daniel Collins <solemnwarning@solemnwarning.net>
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

if(@ARGV != 3) {
	print STDERR "Usage: mkdll.pl <function list> <output file> <dll number>\n";
	exit(1);
}

my $stub_file = $ARGV[0];
my $asm_file = $ARGV[1];
my $dllnum = $ARGV[2];
my $do_logging = ($dllnum != 0);

open(STUBS, "<$stub_file") or die("Cannot open $stub_file: $!");
open(CODE, ">$asm_file") or die("Cannot open $asm_file: $!");

my @stubs = ();
my @stubs_dll = ();

foreach my $line(<STUBS>) {
	$line =~ s/[\r\n]//g;
	
	if($line ne "") {
		my ($func, $dn) = split(/:/, $line);
		$dn = $dllnum if(!defined($dn));
		
		my $sym = $func;
		$sym =~ s/^r_//;
		
		push(@stubs, {"name" => $func, "sym" => $sym, "dllnum" => $dn});
	}
}

print CODE "section .rdata:\n";

foreach my $func(@stubs) {
	print CODE "\t".$func->{"name"}."_sym:\tdb\t'".$func->{"sym"}."', 0\n";
}

print CODE "\nsection .data\n";

foreach my $func(@stubs) {
	print CODE "\t".$func->{"name"}."_addr:\tdd\t0\n";
}

print CODE "\nsection .text\n";
print CODE "\textern\t_find_sym\n";
print CODE "\textern\t_log_call\n" if($do_logging);

foreach my $func(@stubs) {
	my $f_name = $func->{"name"};
	
	print CODE "\nglobal\t_$f_name\n";
	print CODE "_$f_name:\n";
	
	if($do_logging) {
		print CODE "\tpush\tdword ".$func->{"dllnum"}."\n";
		print CODE "\tpush\t$f_name\_sym\n";
		print CODE "\tpush\tdword $dllnum\n";
		print CODE "\tcall\t_log_call\n";
	}
	
	print CODE "\tcmp\tdword [$f_name\_addr], 0\n";
	print CODE "\tjne\t$f_name\_jmp\n";
	
	print CODE "\tpush\t$f_name\_sym\n";
	print CODE "\tpush\tdword ".$func->{"dllnum"}."\n";
	print CODE "\tcall\t_find_sym\n";
	print CODE "\tmov\t[$f_name\_addr], eax\n";
	
	print CODE "\t$f_name\_jmp:\n";
	print CODE "\tjmp\t[$f_name\_addr]\n";
}

close(CODE);
close(STUBS);
