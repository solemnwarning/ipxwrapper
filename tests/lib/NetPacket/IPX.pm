use strict;
use warnings;

package NetPacket::IPX;
use parent qw(NetPacket);

use Carp;

sub new
{
	my ($class, %packet) = @_;
	
	foreach my $key(qw(tc type dest_network dest_node dest_socket
		src_network src_node src_socket data))
	{
		croak("Missing $key argument") unless(defined($packet{$key}));
	}
	
	croak("Invalid tc argument")    unless($packet{tc}   =~ m/^\d+$/ && $packet{tc}   <= 255);
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
		if(length($pkt) < 30)
		{
			carp("Truncated packet (incomplete header)");
			return $self;
		}
		
		# Use array slices to capture the appropriate number of bytes
		# from each address field.
		
		my (
			$checksum, $length, $tc, $type,
			@dst_network, @dst_node, $dst_socket,
			@src_network, @src_node, $src_socket,
		);
		
		(
			$checksum, $length, $tc, $type,
			@dst_network[0..3], @dst_node[0..5], $dst_socket,
			@src_network[0..3], @src_node[0..5], $src_socket,
		) = unpack("nnCC C4C6n C4C6n", $pkt);
		
		$self->{tc}   = $tc;
		$self->{type} = $type;
		
		$self->{dest_network} = _addr_to_string(@dst_network);
		$self->{dest_node}    = _addr_to_string(@dst_node);
		$self->{dest_socket}  = $dst_socket;
		
		$self->{src_network} = _addr_to_string(@src_network);
		$self->{src_node}    = _addr_to_string(@src_node);
		$self->{src_socket}  = $src_socket;
		
		if($length < 30)
		{
			carp("Invalid packet (length < 30)");
			return $self;
		}
		
		if(length($pkt) < $length)
		{
			carp("Truncated packet (data truncated)");
			$self->{data} = substr($pkt, 30);
		}
		else{
			$self->{data} = substr($pkt, 30, ($length - 30));
		}
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
	
	return pack("nnCC", 0xFFFF, 30 + length($self->{data}), $self->{tc}, $self->{type})
		._addr_from_string($self->{dest_network})
		._addr_from_string($self->{dest_node})
		.pack("n", $self->{dest_socket})
		._addr_from_string($self->{src_network})
		._addr_from_string($self->{src_node})
		.pack("n", $self->{src_socket})
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

__END__

=pod

=head1 NAME

NetPacket::IPX - Assemble and disassemble IPX packets.

=head1 SYNOPSIS

  use NetPacket::IPX;
  
  my $ipx = NetPacket::IPX->decode($raw_pkt);
  
  my $raw_pkt = $ipx->encode();
  
  my $ipx = NetPacket::IPX->new(
	  tc   => 0,
	  type => 1,
	  
	  dest_network => "00:00:00:01",
	  dest_node    => "FF:FF:FF:FF:FF:FF",
	  dest_socket  => 1234,
	  
	  src_network => "00:00:00:01",
	  src_node    => "12:34:56:78:90:AB",
	  src_socket  => 5678,
	  
	  data => "...",
  );

=head1 DESCRIPTION

C<NetPacket::IPX> is a C<NetPacket> class for encoding and decoding IPX packets.

=head1 METHODS

=head2 decode($raw_pkt)

Decode a packet and return a C<NetPacket::IPX> instance.

=head2 encode()

Return the encoded form of a C<NetPacket::IPX> instance.

=head2 new(%options)

Construct a C<NetPacket::IPX> instance with arbitrary contents. All arguments
listed in the SYNOPSIS are mandatory.

Throws an exception on missing/invalid arguments.

=head1 INSTANCE DATA

The following fields are available in a C<NetPacket::IPX> instance:

=over

=item tc

Traffic Control field, the number of routers an IPX packet has passed through.

=item type

Type field.

=item dest_network

Destination network number, in the format C<XX:XX:XX:XX>.

=item dest_node

Destination node number, in the format C<XX:XX:XX:XX:XX:XX>.

=item dest_socket

Destination socket number.

=item src_network

Source network number, in the format C<XX:XX:XX:XX>.

=item dest_node

Source node number, in the format C<XX:XX:XX:XX:XX:XX>.

=item dest_socket

Source socket number.

=item data

Packet payload.

=back

=head1 COPYRIGHT

Copyright (C) 2014 Daniel Collins

This module is free software. You can redistribute it and/or
modify it under the same terms as Perl itself.

=head1 AUTHOR

Daniel Collins E<lt>solemnwarning@solemnwarning.netE<gt>

=cut
