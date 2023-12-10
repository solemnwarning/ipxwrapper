# IPXWrapper - Generate assembly stub functions
# Copyright (C) 2008-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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
	print STDERR "Usage: mkdll.pl <stub definitions file> <asm output file> <dll name>\n";
	exit(1);
}

# Must be kept in sync with dll_names in common.c!
my %DLL_INDICES = (
	"ipxwrapper.dll" => 0,
	"wsock32.dll"    => 1,
	"mswsock.dll"    => 2,
	"dpwsockx.dll"   => 3,
	"ws2_32.dll"     => 4,
	"wpcap.dll"      => 5,
	"ipxconfig.exe"  => 6,
);

my ($stub_file, $asm_file, $dll_name) = @ARGV;

my $dll_index = $DLL_INDICES{$dll_name}
	// die "Unknown DLL name: $dll_name";

open(STUBS, "<$stub_file") or die("Cannot open $stub_file: $!");
open(CODE, ">$asm_file") or die("Cannot open $asm_file: $!");

my @stubs = ();

# Skip over header
(scalar <STUBS>);
(scalar <STUBS>);

# Read in stub definitions
foreach my $line(<STUBS>)
{
	$line =~ s/[\r\n]//g;
	
	if($line ne "") {
		my ($name, $target_dll, $target_func, $params) = split(/\s+/, $line);
		
		my $target_dll_index = $DLL_INDICES{$target_dll}
			// die "Unknown DLL: $target_dll\n";
		
		push(@stubs, {
			name             => $name,
			target_dll       => $target_dll,
			target_dll_index => $target_dll_index,
			target_func      => $target_func,
			params           => $params,
		});
	}
}

print CODE <<"END";
extern _QueryPerformanceCounter\@4

extern _find_sym
extern _log_call
extern _fprof_record_timed
extern _fprof_record_untimed

struc FuncStats
	.func_name:   resd 1
	.min_time:    resd 1
	.max_time:    resd 1
	.total_time:  resd 1
	.n_calls:     resd 1

	.cs:  resb 24
endstruc
END

print CODE <<"END";
section .rdata
END

foreach my $func(@stubs)
{
	print CODE <<"END";
		$func->{name}_name:         db '$func->{name}', 0
		$func->{name}_target_func:  db '$func->{target_func}', 0
END
}

my $num_funcs = (scalar @stubs);

print CODE <<"END";
global _NUM_STUBS
_NUM_STUBS: dd $num_funcs

DLL_NAME: db '$dll_name', 0
global _STUBS_DLL_NAME
_STUBS_DLL_NAME: dd DLL_NAME
END

print CODE <<"END";
section .data

global _stubs_enable_profile
_stubs_enable_profile: db 0

END

foreach my $func(@stubs)
{
	print CODE <<"END";
		$func->{name}_addr:  dd 0
END
}

print CODE <<"END";
global _stub_fstats
_stub_fstats:
END

foreach my $func(@stubs)
{
	print CODE <<"END";
		$func->{name}_fstats:
			istruc FuncStats
			at FuncStats.func_name, dd $func->{name}_name
			iend
END
}

print CODE <<"END";
section .text
END

foreach my $func(@stubs)
{
	if(defined $func->{params})
	{
		my $to_copy = $func->{params};
		
		print CODE <<"END";
			global _$func->{name}
			_$func->{name}:
				; Log the call
				push dword $func->{target_dll_index}
				push $func->{name}_target_func
				push dword $dll_index
				call _log_call
				
				; Check if we have address cached
				cmp dword [$func->{name}_addr], 0
				jne $func->{name}_go
				
				; Fetch target function address
				push $func->{name}_target_func
				push dword $func->{target_dll_index}
				call _find_sym
				mov dword [$func->{name}_addr], eax
				
				$func->{name}_go:
				
				; Bypass the profiling code and jump straight into the taget
				; function when not profiling.
				cmp byte [_stubs_enable_profile], 0
				je $func->{name}_skip
				
				push ebp
				mov ebp, esp
				
				; Push tick count onto stack (ebp - 8)
				sub esp, 8
				push esp
				call _QueryPerformanceCounter\@4
				
				; Copy original arguments ($to_copy bytes)
END
		
		for(; $to_copy >= 4;)
		{
			$to_copy -= 4;
			print CODE <<"END";
				push dword [ebp + 4 + 4 + $to_copy]
END
		}
		
		for(; $to_copy >= 2;)
		{
			$to_copy -= 2;
			print CODE <<"END";
				push word [ebp + 4 + 4 + $to_copy]
END
		}
		
		for(; $to_copy >= 1;)
		{
			$to_copy -= 1;
			print CODE <<"END";
				push byte [ebp + 4 + 4 + $to_copy]
END
		}
		
		print CODE <<"END";
				; Call target function
				call [$func->{name}_addr]
				
				; Push target function return value onto stack (ebp - 12)
				push eax
				
				; Push tick count onto stack (ebp - 20)
				sub esp, 8
				push esp
				call _QueryPerformanceCounter\@4
				
				; End tick parameter to _fprof_record_timed
				push dword ebp
				sub dword [esp], 20
				
				; Start tick parameter to _fprof_record_timed
				push dword ebp
				sub dword [esp], 8
				
				; FuncStats parameter to _fprof_record_timed
				push dword $func->{name}_fstats
				
				; Record profiling data
				call _fprof_record_timed
				
				add esp, 8  ; Pop end tick count
				pop eax     ; Pop return value
				add esp, 8  ; Pop start tick count
				
				pop ebp ; Restore caller's ebp
				
				ret $func->{params}
				
				$func->{name}_skip:
				jmp [$func->{name}_addr]
END
	}
	else{
		print CODE <<"END";
			global _$func->{name}
			_$func->{name}:
				; Log the call
				push dword $func->{target_dll_index}
				push $func->{name}_target_func
				push dword $dll_index
				call _log_call
				
				; Check if we have address cached
				cmp dword [$func->{name}_addr], 0
				jne $func->{name}_go
				
				; Fetch target function address
				push $func->{name}_target_func
				push dword $func->{target_dll_index}
				call _find_sym
				mov dword [$func->{name}_addr], eax
				
				$func->{name}_go:
				
				; Record that we were called
				push dword $func->{name}_fstats
				call _fprof_record_untimed
				
				; Jump into target function. We have left the stack as we found it
				; so it can take over our frame.
				jmp [$func->{name}_addr]
END
	}
}

close(CODE);
close(STUBS);
