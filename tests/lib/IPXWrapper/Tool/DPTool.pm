# IPXWrapper test suite
# Copyright (C) 2015 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::Tool::DPTool;

use Carp;
use IPC::Open3;
use POSIX qw(:signal_h);
use Test::Spec;

sub new
{
	my ($class, $host_ip) = @_;
	
	my @command = ("ssh", $host_ip, "Z:\\tools\\dptool.exe");
	note(join(" ", @command));
	
	# No need for error checking here - open3 throws on failure.
	my $pid = open3(my $in, my $out, undef, @command);
	
	my $self = bless({
		pid => $pid,
		in  => $in,
		out => $out,
		
		messages => [],
	}, $class);
	
	my $output = "";
	
	while(defined(my $line = <$out>))
	{
		$output .= $line;
		
		$line =~ s/[\r\n]//g;
		
		if($line eq "ready")
		{
			return $self;
		}
	}
	
	die("Unexpected output from dptool.exe:\n$output");
}

sub DESTROY
{
	my ($self) = @_;
	
	kill(SIGKILL, $self->{pid});
	waitpid($self->{pid}, 0);
}

sub _do_cmd
{
	my ($self, $cmd, $expect) = @_;
	
	my $stdin = $self->{in};
	print {$stdin} join(" ", @$cmd), "\n";
	
	my $out    = $self->{out};
	my $output = "";
	
	while(defined(my $line = <$out>))
	{
		$line =~ s/\r//g;
		
		if($line =~ m/^ready$/m)
		{
			last;
		}
		
		if($line =~ m/^message (\d+) (\d+) (.*)$/)
		{
			push(@{ $self->{messages} }, {
				from    => $1,
				to      => $2,
				message => $3,
			});
		}
		else{
			$output .= $line;
		}
	}
	
	if($output !~ $expect)
	{
		confess("Unexpected output from dptool.exe: $output");
	}
	
	return $output;
}

sub create_session
{
	my ($self, $session_name) = @_;
	
	my $output = $self->_do_cmd([ "create_session", $session_name ], qr/^session_guid \S+$/);
	
	my ($session_guid) = ($output =~ m/ (\S+)$/);
	return $session_guid;
}

sub list_sessions
{
	my ($self) = @_;
	
	my $output = $self->_do_cmd([ "list_sessions" ], qr/^(?:session \S+ \S*\n)*$/);
	
	my %sessions = map { m/(\S+) (\S*)$/; ($1 => $2) } ($output =~ m/(.+)/g);
	return %sessions;
}

sub join_session
{
	my ($self, $session_guid) = @_;
	
	$self->_do_cmd([ "join_session", $session_guid ], qr/^$/);
}

sub create_player
{
	my ($self, $player_longname) = @_;
	
	my $output = $self->_do_cmd([ "create_player", $player_longname ], qr/^player_id [0-9]+$/);
	
	my ($player_id) = ($output =~ m/([0-9]+)/);
	return $player_id;
}

sub list_players
{
	my ($self) = @_;
	
	my $output = $self->_do_cmd([ "list_players" ], qr/^(?:player [0-9]+ (\S+)\n)*$/);
	
	my %sessions = map { m/(\S+) (\S*)$/ } ($output =~ m/(.+)/g);
	return %sessions;
}

sub send_message
{
	my ($self, $player_from, $player_to, $message) = @_;
	
	$self->_do_cmd([ "send_message", $player_from, $player_to, $message ], qr/^$/);
}

sub exit
{
	my ($self) = @_;
	
	$self->_do_cmd([ "exit" ], qr/^$/);
}

sub messages
{
	return @{ (shift)->{messages} };
}

1;
