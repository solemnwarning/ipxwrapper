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

package IPXWrapper::Capture::IPXOverUDP;

use Net::Pcap;
use NetPacket::Ethernet;
use NetPacket::IP;
use NetPacket::UDP;
use NetPacket::IPXWrapper;

sub new
{
	my ($class, $dev) = @_;
	
	my $err;
	my $pcap = Net::Pcap::pcap_open_live($dev, 1500, 0, 1, \$err)
		or die("Cannot open device $dev: $err");
	
	return bless(\$pcap, $class);
}

sub read_available
{
	my ($self) = @_;
	
	my @packets = ();
	
	Net::Pcap::pcap_dispatch($$self, -1, sub
	{
		my ($user_data, $header, $packet) = @_;
		
		my $ether = NetPacket::Ethernet->decode($packet);
		return unless($ether->{type} == 0x0800);
		
		my $ip = NetPacket::IP->decode($ether->{data});
		return unless($ip->{proto} == NetPacket::IP::IP_PROTO_UDP());
		
		my $udp = NetPacket::UDP->decode($ip->{data});
		
		my $ipx = NetPacket::IPXWrapper->decode($udp->{data});
		return unless(defined($ipx));
		
		my %packet = (
			src_mac => $ether->{src_mac},
			dst_mac => $ether->{dest_mac},
			
			src_ip   => $ip->{src_ip},
			src_port => $udp->{src_port},
			
			dst_ip   => $ip->{dest_ip},
			dst_port => $udp->{dest_port},
			
			src_network => $ipx->{src_network},
			src_node    => $ipx->{src_node},
			src_socket  => $ipx->{src_socket},
			
			dst_network => $ipx->{dest_network},
			dst_node    => $ipx->{dest_node},
			dst_socket  => $ipx->{dest_socket},
			
			type => $ipx->{type},
			data => $ipx->{data},
		);
		
		push(@packets, \%packet);
	}, undef);
	
	return @packets;
}

1;
