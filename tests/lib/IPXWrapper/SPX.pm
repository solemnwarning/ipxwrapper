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

package IPXWrapper::SPX;

use Exporter qw(import);

our @EXPORT = qw(
	perform_spxlookup
	connect_to_spx_socket
	pack_spxlookup_req
);

use IO::Socket::INET;
use Socket qw(pack_sockaddr_in inet_aton unpack_sockaddr_in inet_ntoa);

sub perform_spxlookup
{
	my ($local_ip, $bcast_ip, $network, $node, $socket) = @_;
	
	my $fmt_addr = sub
	{
		return join(":", map { sprintf("%02X", $_) } @_);
	};
	
	my $req = NetPacket::IPXWrapper->new(
		type => 1,
		
		dest_network => "00:00:00:00",
		dest_node    => "00:00:00:00:00:00",
		dest_socket  => 0,
		
		src_network => "00:00:00:00",
		src_node    => "00:00:00:00:00:00",
		src_socket  => 0,
		
		data => pack_spxlookup_req($network, $node, $socket),
	)->encode();
	
	my $sock = IO::Socket::INET->new(
		Proto     => "udp",
		Broadcast => 1,
		LocalAddr => $local_ip,
		Blocking  => 0,
	) or die("Can't create socket: $!");
	
	$sock->send($req, 0, pack_sockaddr_in(54792, inet_aton($bcast_ip)))
		or die("Can't send data: $!");
	
	sleep(1);
	
	my @replies = ();
	
	my $buffer;
	while(defined(my $addr = $sock->recv($buffer, 256)))
	{
		my ($recv_port, $recv_ip) = unpack_sockaddr_in($addr);
		
		my %reply = ();
		
		if(length($buffer) == 32)
		{
			my (@network, @node, $socket, $port);
			(@network[0..3], @node[0..5], $socket, $port)
				= unpack("C4C6nn", $buffer);
			
			$reply{network} = $fmt_addr->(@network);
			$reply{node}    = $fmt_addr->(@node);
			$reply{socket}  = $socket;
			$reply{port}    = $port;
			$reply{ip}      = inet_ntoa($recv_ip);
		}
		
		push(@replies, \%reply);
	}
	
	return @replies;
}

sub connect_to_spx_socket
{
	my ($local_ip, $bcast_ip, $network, $node, $socket) = @_;
	
	my @replies = perform_spxlookup($local_ip, $bcast_ip,
		$network, $node, $socket);
	
	die("Couldn't determine remote TCP port number")
		if((scalar @replies) != 1);
	
	my $sock = IO::Socket::INET->new(
		Proto     => "tcp",
		LocalAddr => $local_ip,
		PeerAddr  => $replies[0]->{ip},
		PeerPort  => $replies[0]->{port},
		Blocking  => 0,
	) or die("Can't connect: $!");
	
	$sock->send(_pack_spxinit($network, $node, $socket))
		or die("Can't send spxinit: $!");
	
	return $sock;
}

sub pack_spxlookup_req
{
	my ($network, $node, $socket) = @_;
	
	return _pack_addr->($network)
		._pack_addr->($node)
		.pack("n", $socket)
		.pack("C*", map { 0 } (1 .. 20)),
}

sub _pack_spxinit
{
	my ($network, $node, $socket) = @_;
	
	return _pack_addr->($network)
		._pack_addr->($node)
		.pack("n", $socket)
		.pack("C*", map { 0 } (1 .. 20)),
}

sub _pack_addr
{
	return pack("C*", map { hex($_) } split(m/:/, $_[0]));
}
