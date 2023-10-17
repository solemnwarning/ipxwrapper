# IPXWrapper test suite
# Copyright (C) 2014-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::Tool::IPXISR;

use Carp;
use IPC::Open3;
use POSIX qw(:signal_h);
use Test::Spec;

sub new
{
	my ($class, $host_ip, @exe_args) = @_;
	
	my @command = ("ssh", $host_ip, "Z:\\tools\\ipx-isr.exe", @exe_args);
	note(join(" ", @command));
	
	# No need for error checking here - open3 throws on failure.
	my $pid = open3(my $in, my $out, undef, @command);
	
	my $self = bless({
		pid => $pid,
		in  => $in,
		out => $out,
	}, $class);
	
	my $output = "";
	
	while(defined(my $line = <$out>))
	{
		$output .= $line;
		
		$line =~ s/[\r\n]//g;
		
		if($line =~ m/^Ready (\S+) (\S+) (\S+)$/)
		{
			$self->{net}    = $1;
			$self->{node}   = $2;
			$self->{socket} = $3;
			
			return $self;
		}
	}
	
	die("Didn't get expected output from ipx-recv.exe:\n$output");
}

sub DESTROY
{
	my ($self) = @_;
	
	if(defined($self->{pid}))
	{
		$self->_end();
	}
}

sub net
{
	my ($self) = @_;
	return $self->{net};
}

sub node
{
	my ($self) = @_;
	return $self->{node};
}

sub _end
{
	my ($self) = @_;
	
	# ipx-isr.exe will exit when it reads EOF
	delete $self->{in};
	
	local $SIG{ALRM} = sub
	{
		warn "Killing hung ipx-isr.exe process";
		kill(SIGKILL, $self->{pid});
	};
	
	alarm(5);
	waitpid($self->{pid}, 0);
	alarm(0);
	
	delete $self->{pid};
	
	return ($? == 0);
}

sub kill_and_read
{
	my ($self) = @_;
	
	unless(defined($self->{pid}))
	{
		confess("kill_and_read called multiple times");
	}
	
	# Kill the child process so we can read EOF from the pipe once we have
	# all the output.
	
	if(!$self->_end())
	{
		# Didn't exit properly, pipe might still have a writer.
		die "ipx-isr.exe didn't exit cleanly";
	}
	
	my $out = $self->{out};
	my @packets = ();
	
	while(defined(my $line = <$out>))
	{
		$line =~ s/[\r\n]//g;
		
		if($line =~ m{^(\S+) (\S+) (\S+) (.*)$})
		{
			push(@packets, {
				src_net    => $1,
				src_node   => $2,
				src_socket => $3,
				
				data => $4,
			});
		}
		else{
			die("Malformed line read from ipx-recv.exe: $line");
		}
	}
	
	return @packets;
}

sub send
{
	my ($self, $sa_net, $sa_node, $sa_socket, $data) = @_;
	
	my $stdin = $self->{in};
	print {$stdin} "$sa_net $sa_node $sa_socket $data\n";
}

1;
