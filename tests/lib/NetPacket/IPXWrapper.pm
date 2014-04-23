use strict;
use warnings;

package NetPacket::IPXWrapper;
use parent qw(NetPacket);

use Carp;

sub new
{
	my ($class, %packet) = @_;
	
	foreach my $key(qw(type dest_network dest_node dest_socket
		src_network src_node src_socket data))
	{
		croak("Missing $key argument") unless(defined($packet{$key}));
	}
	
	croak("Invalid type argument")  unless($packet{type} =~ m/^\d+$/ && $packet{type} <= 255);
	
	_check_address("destination", $packet{dest_network}, $packet{dest_node}, $packet{dest_socket});
	_check_address("source",      $packet{src_network},  $packet{src_node},  $packet{src_socket});
	
	return bless(\%packet, $class);
}

sub _check_address
{
	my ($direction, $network, $node, $socket) = @_;
	
	my $OCTET = qr/[0-9A-F][0-9A-F]?/i;
	
	croak("Invalid $direction network") unless($network =~ m/^$OCTET(:$OCTET){3}$/);
	croak("Invalid $direction node")    unless($node =~ m/^$OCTET(:$OCTET){5}$/);
	croak("Invalid $direction socket")  unless($socket =~ m/^\d+$/ && $socket <= 65535);
}

#
# Decode the packet
#

sub decode
{
	my ($class, $pkt, $parent) = @_;
	
	my $self = bless({
		_parent => $parent,
		_frame  => $pkt,
	}, $class);
	
	if(defined($pkt))
	{
		# Use array slices to capture the appropriate number of bytes
		# from each address field.
		
		my (
			$type,
			@dst_network, @dst_node, $dst_socket,
			@src_network, @src_node, $src_socket,
			$length,
		);
		
		(
			$type,
			@dst_network[0..3], @dst_node[0..5], $dst_socket,
			@src_network[0..3], @src_node[0..5], $src_socket,
			$length,
		) = unpack("C C4C6n C4C6n n", $pkt);
		
		$self->{type} = $type;
		
		$self->{dest_network} = _addr_to_string(@dst_network);
		$self->{dest_node}    = _addr_to_string(@dst_node);
		$self->{dest_socket}  = $dst_socket;
		
		$self->{src_network} = _addr_to_string(@src_network);
		$self->{src_node}    = _addr_to_string(@src_node);
		$self->{src_socket}  = $src_socket;
		
		$self->{data}   = substr($pkt, 27);
		
		return undef if($length != length($self->{data}));
	}
	
	return $self;
}

#
# Strip header from packet and return the data contained in it
#

sub strip {
	my ($pkt) = @_;
	return NetPacket::IPX->decode($pkt)->{data};
}   

#
# Encode a packet
#

sub encode
{
	my ($self) = @_;
	
	return pack("C", $self->{type})
		._addr_from_string($self->{dest_network})
		._addr_from_string($self->{dest_node})
		.pack("n", $self->{dest_socket})
		._addr_from_string($self->{src_network})
		._addr_from_string($self->{src_node})
		.pack("n", $self->{src_socket})
		.pack("n", length($self->{data}))
		.$self->{data};
}

sub _addr_to_string
{
	my (@bytes) = @_;
	return join(":", map { sprintf("%02X", $_) } @bytes);
}

sub _addr_from_string
{
	my ($string) = @_;
	return join("", map { pack("C", hex($_)) } split(m/:/, $string));
}

1;
