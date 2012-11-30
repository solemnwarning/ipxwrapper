# IPXWrapper - Test helpers
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

package IPXWrapper::Testing;

use strict;
use warnings;

use IPC::Open2;

sub run_tests
{
	my (@tests) = @_;
	
	my $total_tests  = 0;
	my $passed_tests = 0;
	
	my $test_n = 1;
	
	foreach my $test(@tests)
	{
		my @procs = ();
		
		for(my $i = 0, my $subtest = 1; $i < scalar @$test; $i += 2, $subtest++)
		{
			my $command = $test->[$i];
			my $expect  = $test->[$i + 1];
			
			my $pid = open2(my $stdout, my $stdin, $command);
			
			push(@procs, $stdin);
			
			(my $result = <$stdout>) =~ s/[\r\n]//g;
			
			print "Test $test_n-$subtest: ";
			
			if($result eq $expect)
			{
				print "PASSED\n";
				
				$passed_tests++;
			}
			else{
				print "FAILED\n";
				print "\tCommand:  $command\n";
				print "\tResult:   $result\n";
				print "\tExpected: $expect\n";
			}
			
			$total_tests++;
		}
		
		# Tell any remaining processes to exit.
		
		foreach my $stdin(@procs)
		{
			print {$stdin} "\n";
		}
		
		$test_n++;
	}
	
	print "\n-- Passed $passed_tests/$total_tests tests\n";
	
	exit($total_tests == $passed_tests ? 0 : 1);
}

1;
