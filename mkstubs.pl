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

if(@ARGV != 2 && @ARGV != 3) {
	print STDERR "Usage: mkdll.pl <function list> <output code> [<dll name>]\n";
	exit(1);
}

open(STUBS, "<".$ARGV[0]) or die("Cannot open ".$ARGV[0].": $!");
open(CODE, ">".$ARGV[1]) or die("Cannot open ".$ARGV[1].": $!");

my @stubs = ();

foreach my $line(<STUBS>) {
	$line =~ s/[\r\n]//g;
	
	if($line ne "") {
		push @stubs, $line;
	}
}

if(@ARGV == 3) {
	print CODE "section .rdata:\n";
	print CODE "\tglobal\t_dllname\n";
	print CODE "\tdllname_s:\tdb\t'wsock32.dll'\n";
	print CODE "\t_dllname:\tdd\tdllname_s\n\n";
}

print CODE "section .data\n";

for($n = 0; $n < @stubs; $n++) {
	my $func = $stubs[$n];
	$func =~ s/^r_//;
	
	print CODE "\tname$n:\tdb\t'$func', 0\n";
	print CODE "\taddr$n:\tdd\t0\n";
}

print CODE "\nsection .text\n";
print CODE "\textern\t_find_sym\n";
#print CODE "\textern\t_log_call\n";

for($n = 0; $n < @stubs; $n++) {
	my $func = $stubs[$n];
	print CODE "\tglobal\t_$func\n";
}

for($n = 0; $n < @stubs; $n++) {
	my $func = $stubs[$n];

	print CODE "\n_$func:\n";
	#print CODE "\tpush\tname$n\n";
	#print CODE "\tcall\t_log_call\n";
	print CODE "\tcmp\tdword [addr$n], 0\n";
	print CODE "\tjne\tjmp$n\n";
	print CODE "\tpush\tname$n\n";
	print CODE "\tcall\t_find_sym\n";
	print CODE "\tmov\t[addr$n], eax\n";
	print CODE "jmp$n:\n";
	print CODE "\tjmp\t[addr$n]\n";
}

close(CODE);
close(STUBS);
