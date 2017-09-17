# IPXWrapper test suite
# Copyright (C) 2014 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::Tool::Generic;

use IPC::Open3;
use POSIX qw(:signal_h);
use Test::Spec;

sub new
{
	my ($class, $host_ip, $exe_name, @exe_args) = @_;
	
	my @command = ("ssh", $host_ip, $exe_name, @exe_args);
	note(join(" ", @command));
	
	# No need for error checking here - open3 throws on failure.
	my $pid = open3(my $in, my $out, undef, @command);
	
	my $self = bless({
		pid => $pid,
		in  => $in,
	}, $class);
	
	my $output = "";
	
	while(defined(my $line = <$out>))
	{
		$output .= $line;
		
		$line =~ s/[\r\n]//g;
		
		if($line eq "Ready")
		{
			return $self;
		}
	}
	
	die("Didn't get expected output from $exe_name:\n$output");
}

sub DESTROY
{
	my ($self) = @_;
	
	# Process should exit once it gets EOF
	delete $self->{in};
	
	local $SIG{ALRM} = sub
	{
		warn "Killing hung process";
		kill(SIGKILL, $self->{pid});
	};
	
	alarm(5);
	waitpid($self->{pid}, 0);
	alarm(0);
}

1;
