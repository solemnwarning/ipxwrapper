# IPXWrapper - Tests for bind
# Copyright (C) 2012 Daniel Collins <solemnwarning@solemnwarning.net>
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

use ipxtest;

my @tests = (
	# Single process, unique socket numbers.
	
	[ "bind.exe 0 1234 0 1235" => "OK" ],
	[ "bind.exe 0 1234 1 1235" => "OK" ],
	
	# Two processes, unique socket numbers.
	
	[
		"bind.exe 0 1234" => "OK",
		"bind.exe 0 1235" => "OK",
	],
	
	[
		"bind.exe 0 1234" => "OK",
		"bind.exe 1 1235" => "OK",
	],
	
	# Single process, bind to two random sockets.
	
	[ "bind.exe 0 0 0 0" => "OK" ],
	[ "bind.exe 0 0 1 0" => "OK" ],
	
	# Two processes, bind to random sockets.
	
	[
		"bind.exe 0 0" => "OK",
		"bind.exe 0 0" => "OK",
	],
	
	[
		"bind.exe 0 0" => "OK",
		"bind.exe 1 0" => "OK",
	],
	
	# Single process, conflicting addresses.
	
	[ "bind.exe 0 1234 0 1234" => "FAIL" ],
	[ "bind.exe 0 1234 1 1234" => "FAIL" ],
	
	# Two processes, conflicting addresses.
	
	[
		"bind.exe 0 1234" => "OK",
		"bind.exe 0 1234" => "FAIL"
	],
	
	[
		"bind.exe 0 1234" => "OK",
		"bind.exe 1 1234" => "FAIL"
	],
	
	# SO_REUSEADDR within one process
	
	[ "bind.exe --reuse 0 1234 --reuse 0 1234" => "OK" ],
	[ "bind.exe --reuse 0 1234 0 1234" => "FAIL" ],
	[ "bind.exe 0 1234 --reuse 0 1234" => "FAIL" ],
	
	# SO_REUSEADDR between processes
	
	[
		"bind.exe --reuse 0 1234" => "OK",
		"bind.exe --reuse 0 1234" => "OK",
	],
	
	[
		"bind.exe --reuse 0 1234" => "OK",
		"bind.exe 0 1234"         => "FAIL",
	],
	
	[
		"bind.exe 0 1234"         => "OK",
		"bind.exe --reuse 0 1234" => "FAIL",
	],
);

IPXWrapper::Testing::run_tests(@tests);
