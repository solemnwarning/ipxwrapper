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

package IPXWrapper::Tool::IPXRecv;

use Carp;
use IPC::Open3;
use POSIX qw(:signal_h);
use Test::Spec;

sub new
{
	my ($class, $host_ip, @exe_args) = @_;
	
	my @command = ("ssh", $host_ip, "Z:\\tools\\ipx-recv.exe", @exe_args);
	
	note(join(" ", @command));
	
	# No need for error checking here - open3 throws on failure.
	my $pid = open3(my $in, my $out, undef, @command);
	
	my $self = bless({
		pid => $pid,
		out => $out,
		in  => $in,
		
		sockets => [],
	}, $class);
	
	my $output = "";
	
	while(defined(my $line = <$out>))
	{
		$output .= $line;
		
		$line =~ s/[\r\n]//g;
		
		if($line =~ m{^Bound socket (\d+) to local address: (.+)/(.+)/(.+)$})
		{
			push(@{ $self->{sockets} }, {
				fd   => $1,
				
				net  => $2,
				node => $3,
				sock => $4,
			});
		}
		elsif($line eq "Ready")
		{
			return $self;
		}
	}
	
	die("Didn't get expected output from ipx-recv.exe:\n$output");
}

sub net
{
	my ($self, $sock_idx) = @_;
	
	if((scalar @{ $self->{sockets} }) > 1 && !defined($sock_idx))
	{
		confess("IPXWrapper::Tool::IPXRecv::net() called without a \$sock_idx but multiple sockets are bound");
	}
	
	$sock_idx //= 0;
	
	return $self->{sockets}->[$sock_idx]->{net};
}

sub node
{
	my ($self, $sock_idx) = @_;
	
	if((scalar @{ $self->{sockets} }) > 1 && !defined($sock_idx))
	{
		confess("IPXWrapper::Tool::IPXRecv::node() called without a \$sock_idx but multiple sockets are bound");
	}
	
	$sock_idx //= 0;
	
	return $self->{sockets}->[$sock_idx]->{node};
}

sub DESTROY
{
	my ($self) = @_;
	
	if(defined($self->{pid}))
	{
		$self->_end();
	}
}

sub _end
{
	my ($self) = @_;
	
	# ipx-recv.exe will exit once we close its stdin
	delete $self->{in};
	
	local $SIG{ALRM} = sub
	{
		warn "Killing hung ipx-recv.exe process";
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
		die "ipx-recv.exe didn't exit cleanly";
	}
	
	my $out = $self->{out};
	my @packets = ();
	
	while(defined(my $line = <$out>))
	{
		$line =~ s/[\r\n]//g;
		
		if($line =~ m{^Received (\d+) bytes \((.*)\) on socket (\d+) from (.+)/(.+)/(.+)$})
		{
			my ($sock) = grep { $_->{fd} == $3 } @{ $self->{sockets} };
			
			die("Received packet on unknown socket $3")
				unless(defined $sock);
			
			die("Read a packet with the wrong size, binary data used in test?")
				unless($1 == length($2));
			
			push(@packets, {
				sock => $3,
				data => $2,
				
				local_net  => $sock->{net},
				local_node => $sock->{node},
				local_sock => $sock->{sock},
				
				src_network => $4,
				src_node    => $5,
				src_socket  => $6,
			});
		}
		else{
			die("Malformed line read from ipx-recv.exe: $line");
		}
	}
	
	return @packets;
}

1;
