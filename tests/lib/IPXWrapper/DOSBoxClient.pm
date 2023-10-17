# IPXWrapper test suite
# Copyright (C) 2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::DOSBoxClient;

use IO::Select;
use IO::Socket::INET;
use NetPacket::IPX;
use Test::Spec;

sub new
{
	my ($class, $host, $port) = @_;
	
	my $sock = IO::Socket::INET->new(
		Proto     => "udp",
		PeerAddr  => $host,
		PeerPort  => $port,
	) or die("Can't create socket: $!");
	
	my $reg_req = NetPacket::IPX->new(
		tc   => 0,
		type => 2,
		
		src_network => "00:00:00:00",
		src_node    => "00:00:00:00:00:00",
		src_socket  => 2,
		
		dest_network => "00:00:00:00",
		dest_node    => "00:00:00:00:00:00",
		dest_socket  => 2,
		
		data => "",
	);
	
	$sock->send($reg_req->encode())
		or die("Can't send data: $!");
	
	my $buf;
	$sock->recv($buf, 1024, 0);
	
	my $reg_response = NetPacket::IPX->decode($buf);
	
	my $self = bless({
		sock => $sock,
		
		net  => $reg_response->{dest_network},
		node => $reg_response->{dest_node},
	}, $class);
	
	return $self;
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

sub send
{
	my ($self, %options) = @_;
	
	my $packet     = NetPacket::IPX->new(%options);
	my $enc_packet = $packet->encode();
	
	$self->{sock}->send($enc_packet)
		or die("Can't send data: $!");
}

sub recv_any
{
	my ($self) = @_;
	
	my @packets = ();
	
	my $select = IO::Select->new($self->{sock});
	
	while($select->can_read(0))
	{
		my $buf;
		$self->{sock}->recv($buf, 2048);
		
		my $pkt = NetPacket::IPX->decode($buf);
		push(@packets, $pkt);
	}
	
	return @packets;
}

1;
