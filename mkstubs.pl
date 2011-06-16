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

print CODE "section .rdata:\n";

if(@ARGV == 3) {
	print CODE "\tglobal\t_dllname\n";
	print CODE "\tdllname_s:\tdb\t'wsock32.dll'\n";
	print CODE "\t_dllname:\tdd\tdllname_s\n";
}

foreach my $func(@stubs) {
	my $real_func = $func;
	$real_func =~ s/^r_//;
	
	print CODE "\t$func\_sym:\tdb\t'$real_func', 0\n";
}

print CODE "\nsection .data\n";

if(@ARGV == 3) {
	print CODE "\textern\t_call_log\n";
}

foreach my $func(@stubs) {
	print CODE "\t$func\_addr:\tdd\t0\n";
}

print CODE "\nsection .text\n";
print CODE "\textern\t_find_sym\n";

if(@ARGV == 3) {
	print CODE "\textern\t_fputs\n";
	print CODE "\textern\t_fputc\n";
}

foreach my $func(@stubs) {
	print CODE "\nglobal\t_$func\n";
	print CODE "_$func:\n";
	
	if(@ARGV == 3) {
		print CODE "\tcmp\tdword [dword _call_log], 0\n";
		print CODE "\tje\t$func\_nolog\n";
		
		# Write symbol name to log with fputs
		#
		print CODE "\tpush\tdword [dword _call_log]\n";
		print CODE "\tpush\t$func\_sym\n";
		print CODE "\tcall\t_fputs\n";
		print CODE "\tadd esp, 8\n";
		
		# Write newline to log with fputc
		#
		print CODE "\tpush\tdword [dword _call_log]\n";
		print CODE "\tpush\tdword 0x0A\n";
		print CODE "\tcall\t_fputc\n";
		print CODE "\tadd esp, 8\n";
		
		print CODE "\t$func\_nolog:\n";
	}
	
	print CODE "\tcmp\tdword [$func\_addr], 0\n";
	print CODE "\tjne\t$func\_jmp\n";
	print CODE "\tpush\t$func\_sym\n";
	print CODE "\tcall\t_find_sym\n";
	print CODE "\tmov\t[$func\_addr], eax\n";
	print CODE "\t$func\_jmp:\n";
	print CODE "\tjmp\t[$func\_addr]\n";
}

close(CODE);
close(STUBS);
