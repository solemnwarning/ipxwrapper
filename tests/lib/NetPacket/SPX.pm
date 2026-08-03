use strict;
use warnings;

package NetPacket::SPX;
use parent qw(NetPacket);

use Carp;
use Exporter qw(import);

our @EXPORT_OK = qw(
	SPX_CONNCTRL_XHD
	SPX_CONNCTRL_RES1
	SPX_CONNCTRL_NEG
	SPX_CONNCTRL_SPX2
	SPX_CONNCTRL_EOM
	SPX_CONNCTRL_ATN
	SPX_CONNCTRL_ACK
	SPX_CONNCTRL_SYS
);

our %EXPORT_TAGS = (
	constants => [ qw(
		SPX_CONNCTRL_XHD
		SPX_CONNCTRL_RES1
		SPX_CONNCTRL_NEG
		SPX_CONNCTRL_SPX2
		SPX_CONNCTRL_EOM
		SPX_CONNCTRL_ATN
		SPX_CONNCTRL_ACK
		SPX_CONNCTRL_SYS
	) ],
);

use constant {
	SPX_CONNCTRL_XHD  => 0x01,  # Reserved by SPX II for extended header
	SPX_CONNCTRL_RES1 => 0x02,  # Undefined, must be 0
	SPX_CONNCTRL_NEG  => 0x04,  # SPX II negotiate size request/response, must be 0 for SPX
	SPX_CONNCTRL_SPX2 => 0x08,  # SPX II type packet, must be 0 for SPX
	SPX_CONNCTRL_EOM  => 0x10,  # Set by an SPX client to indicate end of message.
	SPX_CONNCTRL_ATN  => 0x20,  # Reserved for attention indication (Not supported by SPX)
	SPX_CONNCTRL_ACK  => 0x40,  # Set to request the receiving partner acknowledge that this packet has been received. Acknowledgement requests and responses are handled by SPX.
	SPX_CONNCTRL_SYS  => 0x80,  # Set to indicate a packet is a system packet. System packets are internal SPX packets, are not delivered to the application, and do not consume sequence numbers.
};

sub new
{
	my ($class, %packet) = @_;
	
	foreach my $key(qw(
		connection_control
		datastream_type
		src_connection_id
		dst_connection_id
		seq_number
		ack_number
		allocation_number
		data))
	{
		croak("Missing $key argument") unless(defined($packet{$key}));
	}
	
	foreach my $key(qw(
		connection_control
		datastream_type))
	{
		croak("Invalid $key argument") unless($packet{$key} =~ m/^\d+$/ && $packet{$key} <= 255);
	}
	
	foreach my $key(qw(
		src_connection_id
		dst_connection_id
		seq_number
		ack_number
		allocation_number))
	{
		croak("Invalid $key argument") unless($packet{$key} =~ m/^\d+$/ && $packet{$key} <= 65535);
	}
	
	return bless(\%packet, $class);
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
		if(length($pkt) < 12)
		{
			carp("Truncated packet (incomplete header)");
			return $self;
		}
		
		(
			$self->{connection_control},
			$self->{datastream_type},
			$self->{src_connection_id},
			$self->{dst_connection_id},
			$self->{seq_number},
			$self->{ack_number},
			$self->{allocation_number},
		) = unpack("CCnnnnn", $pkt);
		
		$self->{data} = substr($pkt, 12, (length($pkt) - 12));
	}
	
	return $self;
}

#
# Strip header from packet and return the data contained in it
#

sub strip {
	my ($pkt) = @_;
	return NetPacket::SPX->decode($pkt)->{data};
}

#
# Encode a packet
#

sub encode
{
	my ($self) = @_;
	
	return pack("CCnnnnn",
		$self->{connection_control},
		$self->{datastream_type},
		$self->{src_connection_id},
		$self->{dst_connection_id},
		$self->{seq_number},
		$self->{ack_number},
		$self->{allocation_number})
		.$self->{data};
}

1;

__END__

=pod

=head1 NAME

NetPacket::SPX - Assemble and disassemble SPX packets.

=head1 SYNOPSIS

  use NetPacket::SPX qw(:constants);
  
  my $spx = NetPacket::SPX->decode($raw_pkt);
  
  my $raw_pkt = $spx->encode();
  
  my $spx = NetPacket::IPX->new(
      connection_control => SPX_CONNCTRL_SYS,
      datastream_type    => 0,
      src_connection_id => 1234,
      dst_connection_id => 5678,
      seq_number        => 5,
      ack_number        => 10,
      allocation_number => 10,
      
      data => "...",
  );

=head1 DESCRIPTION

C<NetPacket::SPX> is a C<NetPacket> class for encoding and decoding SPX packets.

=head1 METHODS

=head2 decode($raw_pkt)

Decode a packet and return a C<NetPacket::SPX> instance.

=head2 encode()

Return the encoded form of a C<NetPacket::SPX> instance.

=head2 new(%options)

Construct a C<NetPacket::SPX> instance with arbitrary contents. All arguments
listed in the SYNOPSIS are mandatory.

Throws an exception on missing/invalid arguments.

=head1 INSTANCE DATA

The following fields are available in a C<NetPacket::SPX> instance:

=over

=item connection_control

Connection Control field, consisting of any of the following constants bitwise
OR'd together:

  SPX_CONNCTRL_XHD
  SPX_CONNCTRL_RES1
  SPX_CONNCTRL_NEG
  SPX_CONNCTRL_SPX2
  SPX_CONNCTRL_EOM
  SPX_CONNCTRL_ATN
  SPX_CONNCTRL_ACK
  SPX_CONNCTRL_SYS

=item datastream_type

Datastream type field (unsigned 8-bit integer).

=item src_connection_id

Source connection ID field (unsigned 16-bit integer).

=item dst_connection_id

Destination connection ID field (unsigned 16-bit integer).

=item seq_number

Sequence number field (unsigned 16-bit integer).

=item ack_number

Acknowledgement number field (unsigned 16-bit integer).

=item allocation_number

Allocation number field (unsigned 16-bit integer).

=item data

Packet payload.

=back

=head1 COPYRIGHT

Copyright (C) 2026 Daniel Collins

This module is free software. You can redistribute it and/or
modify it under the same terms as Perl itself.

=head1 AUTHOR

Daniel Collins E<lt>solemnwarning@solemnwarning.netE<gt>

=cut
