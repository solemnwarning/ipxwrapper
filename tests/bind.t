# IPXWrapper - Tests for bind
# Copyright (C) 2012-2014 Daniel Collins <solemnwarning@solemnwarning.net>
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

use Test::More tests => 28;
use IPC::Open2;

# TODO: Test there are enough interfaces.

try_binds(
	"Single process, single socket",
	
	"bind.exe 0 1234" => "OK",
);

try_binds(
	"Single process, unique socket numbers, same interface",
	
	"bind.exe 0 1234 0 1235" => "OK",
);

try_binds(
	"Single process, unique socket numbers, seperate interfaces",
	
	"bind.exe 0 1234 1 1235" => "OK",
);

# TODO: Test randomly allocated socket numbers are sensible.

try_binds(
	"Single process, random socket numbers, same interface",
	
	"bind.exe 0 0 0 0" => "OK",
);

try_binds(
	"Single process, random socket numbers, seperate interfaces",
	
	"bind.exe 0 0 1 0" => "OK",
);

try_binds(
	"Single process, conflicting socket numbers, same interface",
	
	"bind.exe 0 1234 0 1234" => "FAIL",
);

try_binds(
	"Single process, conflicting socket numbers, seperate interfaces",
	
	"bind.exe 0 1234 1 1234" => "FAIL",
);

try_binds(
	"Single process, conflicting socket numbers, both using SO_REUSEADDR",
	
	"bind.exe --reuse 0 1234 --reuse 0 1234" => "OK",
);

try_binds(
	"Single process, conflicting socket numbers, first using SO_REUSEADDR",
	
	"bind.exe --reuse 0 1234 0 1234" => "FAIL",
);

try_binds(
	"Single process, conflicting socket numbers, second using SO_REUSEADDR",
	
	"bind.exe 0 1234 --reuse 0 1234" => "FAIL",
);

try_binds(
	"Two processes, unique socket numbers, same interface",
	
	"bind.exe 0 1234" => "OK",
	"bind.exe 0 1235" => "OK",
);

try_binds(
	"Two processes, unique socket numbers, seperate interfaces",
	
	"bind.exe 0 1234" => "OK",
	"bind.exe 1 1235" => "OK",
);

try_binds(
	"Two processes, random socket numbers, same interface",
	
	"bind.exe 0 0" => "OK",
	"bind.exe 0 0" => "OK",
);

try_binds(
	"Two processes, random socket numbers, seperate interfaces",
	
	"bind.exe 0 0" => "OK",
	"bind.exe 1 0" => "OK",
);

try_binds(
	"Two processes, conflicting socket numbers, same interface",
	
	"bind.exe 0 1234" => "OK",
	"bind.exe 0 1234" => "FAIL",
);

try_binds(
	"Two processes, conflicting socket numbers, seperate interfaces",
	
	"bind.exe 0 1234" => "OK",
	"bind.exe 1 1234" => "FAIL",
);

try_binds(
	"Two processes, conflicting socket numbers, both using SO_REUSEADDR",
	
	"bind.exe --reuse 0 1234" => "OK",
	"bind.exe --reuse 0 1234" => "OK",
);

try_binds(
	"Two processes, conflicting socket numbers, first using SO_REUSEADDR",
	
	"bind.exe --reuse 0 1234" => "OK",
	"bind.exe 0 1234"         => "FAIL",
);

try_binds(
	"Two processes, conflicting socket numbers, second using SO_REUSEADDR",
	
	"bind.exe 0 1234"         => "OK",
	"bind.exe --reuse 0 1234" => "FAIL",
);

sub try_binds
{
	my ($desc, @cmds) = @_;
	
	diag("$desc...");
	
	my @stdins = ();
	
	for(my $i = 0; $i < (scalar @cmds); $i += 2)
	{
		my $command = $cmds[$i];
		my $expect  = $cmds[$i + 1];
		
		my $pid = open2(my $stdout, my $stdin, $command);
		push(@stdins, $stdin);
		
		(my $result = <$stdout>) =~ s/[\r\n]//g;
		
		is($result, $expect, $command);
	}
	
	# Tell any remaining processes to exit.
	
	foreach my $stdin(@stdins)
	{
		print {$stdin} "\n";
	}
}
