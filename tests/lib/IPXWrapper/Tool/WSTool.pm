# IPXWrapper test suite
# Copyright (C) 2026 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::Tool::WSTool;

use Carp;
use IPC::Open3;
use POSIX qw(:signal_h);
use Test::Spec;

use Exporter qw(import);

our @EXPORT = qw(
	AF_IPX
	SOCK_STREAM
	SOCK_DGRAM
	SOCK_SEQPACKET
	NSPROTO_IPX
	NSPROTO_SPX
	NSPROTO_SPXII
	FIONBIO
);

use constant {
	AF_IPX		 => 6,
	SOCK_STREAM	=> 1,
	SOCK_DGRAM	 => 2,
	SOCK_SEQPACKET => 5,
	NSPROTO_IPX	=> 1000,
	NSPROTO_SPX	=> 1256,
	NSPROTO_SPXII => 1257,
	FIONBIO => 0x8004667E,
};

sub new
{
	my ($class, $host_ip) = @_;
	
	my @command = ("ssh", $host_ip, "Z:\\tools\\wstool.exe");
	# my @command = ("wine", "./tools/wstool.exe");
	note(join(" ", @command));
	
	# No need for error checking here - open3 throws on failure.
	my $pid = open3(my $in, my $out, undef, @command);
	
	my $self = bless({
		pid => $pid,
		in  => $in,
		out => $out,
		pending => undef,
	}, $class);
	
	my $output = "";
	
	while(1)
	{
		my $line = $self->_read_line();
		
		$output .= "$line\n";
		
		if($line eq "ready")
		{
			return $self;
		}
		elsif($line eq "<EOF>")
		{
			last;
		}
	}
	
	die("Unexpected output from wstool.exe:\n$output");
}

sub DESTROY
{
	my ($self) = @_;

	close($self->{in});
	waitpid($self->{pid}, 0);
}

sub _write_line
{
	my ($self, $line) = @_;
	
	note("[".$self->{pid}."] < $line");
	
	print { $self->{in} } "$line\n";
}

sub _read_line
{
	my ($self) = @_;
	
	my $out = $self->{out};
	my $line = <$out>;
	
	if(defined $line)
	{
		$line =~ s/\r?\n$//;
		note("[".$self->{pid}."] > $line");
		
		if($line =~ m/^!/)
		{
			return $self->_read_line();
		}
		
		return $line;
	}
	else{
		note("[".$self->{pid}."] > EOF");
		return "<EOF>";
	}
}

sub socket
{
	my ($self, $family, $type, $protocol) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("socket ${family} ${type} ${protocol}");

	my $response = $self->_read_line();

	if($response =~ m/^socket = (\d+)$/)
	{
		return $1;
	}
	elsif($response =~ m/^socket = -1 (\d+)$/)
	{
		croak("socket failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub closesocket
{
	my ($self, $sock) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("closesocket ${sock}");

	my $response = $self->_read_line();

	if($response =~ m/^closesocket = 0$/)
	{
		return;
	}
	elsif($response =~ m/^closesocket = -1 (\d+)$/)
	{
		croak("closesocket failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub bind
{
	my ($self, $sock, $ipx_netnum, $ipx_nodenum, $ipx_socket) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("bind ${sock} ${ipx_netnum} ${ipx_nodenum} ${ipx_socket}");

	my $response = $self->_read_line();

	if($response =~ m/^bind = 0$/)
	{
		return;
	}
	elsif($response =~ m/^bind = -1 (\d+)$/)
	{
		croak("bind failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub getsockname
{
	my ($self, $sock) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("getsockname ${sock}");

	my $response = $self->_read_line();

	if($response =~ m/^getsockname = 0 AF_IPX (\S+) (\S+) (\d+)$/)
	{
		return {
			ipx_netnum  => $1,
			ipx_nodenum => $2,
			ipx_socket  => $3,
		};
	}
	elsif($response =~ m/^getsockname = -1 (\d+)$/)
	{
		croak("getsockname failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub listen
{
	my ($self, $sock, $backlog) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("listen ${sock} ${backlog}");

	my $response = $self->_read_line();

	if($response =~ m/^listen = 0$/)
	{
		return;
	}
	elsif($response =~ m/^listen = -1 (\d+)$/)
	{
		croak("listen failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub connect_start
{
	my ($self, $sock, $ipx_netnum, $ipx_nodenum, $ipx_socket) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("connect ${sock} ${ipx_netnum} ${ipx_nodenum} ${ipx_socket}");

	$self->{pending} = "connect";
}

sub connect_finish
{
	my ($self) = @_;

	confess("No pending connect operation") unless(($self->{pending} // "") eq "connect");

	my $response = $self->_read_line();

	$self->{pending} = undef;

	if($response =~ m/^connect = 0$/)
	{
		return;
	}
	elsif($response =~ m/^connect = -1 (\d+)$/)
	{
		croak("connect failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub accept_start
{
	my ($self, $sock) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("accept ${sock}");

	$self->{pending} = "accept";
}

sub accept_finish
{
	my ($self) = @_;

	confess("No pending accept operation") unless(($self->{pending} // "") eq "accept");

	my $response = $self->_read_line();

	$self->{pending} = undef;

	if($response =~ m/^accept = (\d+) AF_IPX (\S+) (\S+) (\d+)$/)
	{
		return {
			socket	  => $1,
			ipx_netnum  => $2,
			ipx_nodenum => $3,
			ipx_socket  => $4,
		};
	}
	elsif($response =~ m/^accept = -1 (\d+)$/)
	{
		croak("accept failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub send
{
	my ($self, $sock, $string) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("send ${sock} ${string}");

	my $response = $self->_read_line();

	if($response =~ m/^send = (\d+)$/)
	{
		return $1;
	}
	elsif($response =~ m/^send = -1 (\d+)$/)
	{
		croak("send failed with error code $1");
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub recv
{
	my ($self, $sock, $length) = @_;

	$self->recv_start($sock, $length);
	return $self->recv_finish();
}

sub recv_start
{
	my ($self, $sock, $length) = @_;

	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});

	$self->_write_line("recv ${sock} ${length}");

	$self->{pending} = "recv";
}

sub recv_finish
{
	my ($self) = @_;

	confess("No pending recv operation") unless(($self->{pending} // "") eq "recv");

	my $response = $self->_read_line();

	$self->{pending} = undef;

	if($response =~ m/^recv = -1 (\d+)$/)
	{
		croak("recv failed with error code $1");
	}
	elsif($response =~ m/^recv = (.*)$/)
	{
		return $1;
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

sub ioctlsocket
{
	my ($self, $sock, $cmd, $arg) = @_;
	
	confess("Finish pending ".$self->{pending}." operation first") if(defined $self->{pending});
	
	confess("Invalid arg string") unless($arg =~ m/^([0-9a-fA-F]{2})+$/);
	
	$self->_write_line("ioctlsocket $sock $cmd $arg");
	
	my $response = $self->_read_line();
	
	if($response =~ m/^ioctlsocket = -1 (\d+)$/)
	{
		croak("ioctlsocket failed with error code $1");
	}
	elsif($response =~ m/^ioctlsocket = ((?:[0-9a-fA-F]{2})+)$/)
	{
		return $1;
	}
	else{
		confess("Unexpected output from wstool.exe: '${response}'");
	}
}

1;
