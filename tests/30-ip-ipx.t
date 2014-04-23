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

use Test::Spec;

use FindBin;
use lib "$FindBin::Bin/lib/";

use IPXWrapper::Capture::IPXOverUDP;
use IPXWrapper::Tool::IPXISR;
use IPXWrapper::Tool::IPXRecv;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);
our ($net_a_bcast, $net_b_bcast);

require "$FindBin::Bin/ptype.pm";

our $ptype_send_func;
our $ptype_capture_class;

use constant {
	UDP_BCAST_PORT => 54792,
};

my $node_c_net = "00:00:00:02";

describe "IPXWrapper using IP encapsulation" => sub
{
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\00:00:00:00:00:00", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:02");
		
		$node_c_net   = "00:00:00:02";
	};
	
	describe "packets received on the shared port" => sub
	{
		they "are only accepted from the bound interface" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			my $capture_b = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "00:00:00:00", $remote_mac_b, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 1234,
				
				data => "chamferer",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_b_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_b,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 1234,
				
				data => "fragmentariness",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => $node_c_net,
				dest_node    => $remote_mac_b,
				dest_socket  => 4444,
				
				src_network => $node_c_net,
				src_node    => $local_mac_b,
				src_socket  => 1234,
				
				data => "dowy",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_b_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_b,
				
				type => 0,
				
				dest_network => $node_c_net,
				dest_node    => $remote_mac_b,
				dest_socket  => 4444,
				
				src_network => $node_c_net,
				src_node    => $local_mac_b,
				src_socket  => 1234,
				
				data => "papermch",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			my @packets_b = $capture_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 1234,
					
					data => "chamferer",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_network => $node_c_net,
					src_node    => $local_mac_b,
					src_socket  => 1234,
					
					data => "papermch",
				},
			]);
		};
	};
	
	describe "packets received on the private port" => sub
	{
		they "are only accepted on the bound interface" => sub
		{
			my $isr_a = IPXWrapper::Tool::IPXISR->new($remote_ip_a,
				"-r", "-b", "00:00:00:01", $remote_mac_a, "4444");
			
			my $isr_b = IPXWrapper::Tool::IPXISR->new($remote_ip_a,
				"-r", "-b", $node_c_net, $remote_mac_b, "4444");
			
			my $port_a = determine_private_port($isr_a, $local_dev_a, "fleawort");
			my $port_b = determine_private_port($isr_b, $local_dev_b, "effervesce");
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => $port_a,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5678,
				
				data => "infernal",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_b_bcast,
				dest_port => $port_a,
				src_ip    => $local_ip_b,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5678,
				
				data => "multireflex",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => $port_b,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => $node_c_net,
				dest_node    => $remote_mac_b,
				dest_socket  => 4444,
				
				src_network => $node_c_net,
				src_node    => $local_mac_b,
				src_socket  => 5678,
				
				data => "metropolises",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_b_bcast,
				dest_port => $port_b,
				src_ip    => $local_ip_b,
				
				type => 0,
				
				dest_network => $node_c_net,
				dest_node    => $remote_mac_b,
				dest_socket  => 4444,
				
				src_network => $node_c_net,
				src_node    => $local_mac_b,
				src_socket  => 5678,
				
				data => "photoionization",
			);
			
			sleep(1);
			
			my @packets_a = $isr_a->kill_and_read();
			my @packets_b = $isr_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_net    => "00:00:00:01",
					src_node   => $local_mac_a,
					src_socket => 5678,
					
					data => "infernal",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_net    => $node_c_net,
					src_node   => $local_mac_b,
					src_socket => 5678,
					
					data => "photoionization",
				},
			]);
		};
	};
	
	describe "broadcast packets" => sub
	{
		they "are received by sockets with SO_BROADCAST" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "amity",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "amity",
				},
			]);
		};
		
		they "aren't received by sockets without SO_BROADCAST when win95 bug is enabled" => sub
		{
			reg_set_dword($remote_ip_a, "HKCU\\Software\\IPXWrapper", "w95_bug", 1);
			
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", $remote_mac_a, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "unslotted",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, []);
		};
		
		they "are received by sockets without SO_BROADCAST when win95 bug is disabled" => sub
		{
			reg_set_dword($remote_ip_a, "HKCU\\Software\\IPXWrapper", "w95_bug", 0);
			
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", $remote_mac_a, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "declivity",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "declivity",
				},
			]);
		};
		
		they "are received by concurrent sockets within the same process" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "isohyet",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "isohyet",
				},
				
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "isohyet",
				},
			]) and isnt($packets[0]->{sock}, $packets[1]->{sock});
		};
		
		they "are received by concurrent sockets in different processes" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			my $capture_b = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "januaries",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			my @packets_b = $capture_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "januaries",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "januaries",
				},
			]);
		};
		
		# TODO
		they "are only received on the bound interfaces";
	};
	
	describe "packets sent to an unknown address" => sub
	{
		they "are only broadcast on the bound interface" => sub
		{
			my $capture_a = IPXWrapper::Capture::IPXOverUDP->new($local_dev_a);
			my $capture_b = IPXWrapper::Capture::IPXOverUDP->new($local_dev_b);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-d" => "ligule",
				"-s" => "5555", "-h" => $remote_mac_a,
				"00:00:00:01", "11:11:11:11:11:11", "4444",
			);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-d" => "hardheads",
				"-s" => "5555", "-h" => $remote_mac_b,
				"00:00:00:02", "22:22:22:22:22:22", "4444",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->read_available();
			my @packets_b = $capture_b->read_available();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_ip   => $remote_ip_a,
					dst_ip   => $net_a_bcast,
					dst_port => UDP_BCAST_PORT,
					
					dst_network => "00:00:00:01",
					dst_node    => "11:11:11:11:11:11",
					dst_socket  => 4444,
					
					src_network => "00:00:00:01",
					src_node    => $remote_mac_a,
					src_socket  => 5555,
					
					data => "ligule",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_ip   => $remote_ip_b,
					dst_ip   => $net_b_bcast,
					dst_port => UDP_BCAST_PORT,
					
					dst_network => "00:00:00:02",
					dst_node    => "22:22:22:22:22:22",
					dst_socket  => 4444,
					
					src_network => $node_c_net,
					src_node    => $remote_mac_b,
					src_socket  => 5555,
					
					data => "hardheads",
				},
			]);
		};
	};
	
	describe "packets sent to a known address" => sub
	{
		my @packets_a;
		my @packets_b;
		
		before all => sub
		{
			my $sender = IPXWrapper::Tool::IPXISR->new($remote_ip_a,
				"00:00:00:01", $remote_mac_a, "7777");
			
			# Send a packet from our target address. It
			# isn't aimed at an open socket, but will still
			# be seen by the router thread and should update
			# the address cache.
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				src_port  => 6666,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 7778,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 8888,
				
				data => "",
			);
			
			# Lay in some chaff - send packets with other
			# source addresses to make sure the correct
			# address gets pulled out of the cache.
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				src_port  => 6667,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 7778,
				
				src_network => "00:00:00:01",
				src_node    => "00:00:00:12:34:56",
				src_socket  => 8888,
				
				data => "",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				src_port  => 6668,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 7778,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 8889,
				
				data => "",
			);
			
			sleep(1);
			
			my $capture_a = IPXWrapper::Capture::IPXOverUDP->new($local_dev_a);
			my $capture_b = IPXWrapper::Capture::IPXOverUDP->new($local_dev_b);
			
			$sender->send("00:00:00:01", $local_mac_a, "8888", "nondomesticating");
			
			sleep(1);
			
			@packets_a = $capture_a->read_available();
			@packets_b = $capture_b->read_available();
		};
		
		they "are sent to the process's UDP port" => sub
		{
			cmp_hashes_partial(\@packets_a, [
				{
					src_ip   => $remote_ip_a,
					dst_ip   => $local_ip_a,
					dst_port => 6666,
					
					src_network => "00:00:00:01",
					src_node    => $remote_mac_a,
					src_socket  => 7777,
					
					dst_network => "00:00:00:01",
					dst_node    => $local_mac_a,
					dst_socket  => 8888,
					
					data => "nondomesticating",
				},
			]);
		};
		
		they "aren't transmitted on the other network" => sub
		{
			cmp_hashes_partial(\@packets_b, []);
		};
	};
	
	describe "sockets bound to the wildcard interface" => sub
	{
		my ($wildcard_net, $wildcard_node);
		
		before each => sub
		{
			return if(defined($wildcard_net));
			
			my @interfaces = getsockopt_interfaces($remote_ip_a);
			
			my ($wc) = grep {
				$_->{node} ne $remote_mac_a && $_->{node} ne $remote_mac_b
			} @interfaces;
			
			die("Could not determine wildcard address")
				unless($wc && (scalar @interfaces) == 3);
			
			$wildcard_net  = $wc->{net};
			$wildcard_node = $wc->{node};
		};
		
		they "transmit broadcast packets on all underlying interfaces" => sub
		{
			my $capture_a = IPXWrapper::Capture::IPXOverUDP->new($local_dev_a);
			my $capture_b = IPXWrapper::Capture::IPXOverUDP->new($local_dev_b);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-b",
				"-h" => $wildcard_node,
				"-s" => "5555",
				"-d" => "tibetan",
				"FF:FF:FF:FF", "FF:FF:FF:FF:FF:FF", "4444"
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->read_available();
			my @packets_b = $capture_b->read_available();
			
			my $ref_packet = sub
			{
				my ($dst_ip) = @_;
				
				return {
					dst_port => UDP_BCAST_PORT,
					dst_ip   => $dst_ip,
					
					dst_network => "FF:FF:FF:FF",
					dst_node    => "FF:FF:FF:FF:FF:FF",
					dst_socket  => 4444,
					
					src_network => $wildcard_net,
					src_node    => $wildcard_node,
					src_socket  => 5555,
					
					data => "tibetan",
				};
			};
			
			cmp_hashes_partial(\@packets_a, [ $ref_packet->($net_a_bcast) ]);
			cmp_hashes_partial(\@packets_b, [ $ref_packet->($net_b_bcast) ]);
		};
		
		they "receive broadcast packets on all underlying interfaces" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-b", "00:00:00:00", $wildcard_node, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "overtake",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_b_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_b,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_b,
				src_socket  => 5555,
				
				data => "leakey",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "overtake",
				},
				
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_b,
					src_socket  => 5555,
					
					data => "leakey",
				},
			]);
		};
		
		they "broadcast packets to an unknown address on all underlying interfaces" => sub
		{
			my $capture_a = IPXWrapper::Capture::IPXOverUDP->new($local_dev_a);
			my $capture_b = IPXWrapper::Capture::IPXOverUDP->new($local_dev_b);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-h", $wildcard_node, "-s", "5555",
				"-d", "bowdrill",
				"00:00:00:01", "11:11:11:11:11:11", "4444",
			);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-h", $wildcard_node, "-s", "5555",
				"-d", "developer",
				"00:00:00:02", "22:22:22:22:22:22", "4444",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->read_available();
			my @packets_b = $capture_b->read_available();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_ip   => $remote_ip_a,
					dst_ip   => $net_a_bcast,
					dst_port => UDP_BCAST_PORT,
					
					dst_network => "00:00:00:01",
					dst_node    => "11:11:11:11:11:11",
					dst_socket  => 4444,
					
					src_network => "00:00:00:01",
					src_node    => $wildcard_node,
					src_socket  => 5555,
					
					data => "bowdrill",
				},
				
				{
					src_ip   => $remote_ip_a,
					dst_ip   => $net_a_bcast,
					dst_port => UDP_BCAST_PORT,
					
					dst_network => "00:00:00:02",
					dst_node    => "22:22:22:22:22:22",
					dst_socket  => 4444,
					
					src_network => "00:00:00:01",
					src_node    => $wildcard_node,
					src_socket  => 5555,
					
					data => "developer",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_ip   => $remote_ip_b,
					dst_ip   => $net_b_bcast,
					dst_port => UDP_BCAST_PORT,
					
					dst_network => "00:00:00:01",
					dst_node    => "11:11:11:11:11:11",
					dst_socket  => 4444,
					
					src_network => "00:00:00:01",
					src_node    => $wildcard_node,
					src_socket  => 5555,
					
					data => "bowdrill",
				},
				
				{
					src_ip   => $remote_ip_b,
					dst_ip   => $net_b_bcast,
					dst_port => UDP_BCAST_PORT,
					
					dst_network => "00:00:00:02",
					dst_node    => "22:22:22:22:22:22",
					dst_socket  => 4444,
					
					src_network => "00:00:00:01",
					src_node    => $wildcard_node,
					src_socket  => 5555,
					
					data => "developer",
				},
			]);
		};
		
		# TODO: Test transmission to a known IPX address.
		
		they "receive unicast packets on all underlying interfaces" => sub
		{
			# TODO: Sent to private port, or not bcast at least.
			
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", $wildcard_node, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $wildcard_node,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "orville",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_b_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_b,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $wildcard_node,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_b,
				src_socket  => 5555,
				
				data => "eliminability",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 5555,
					
					data => "orville",
				},
				
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_b,
					src_socket  => 5555,
					
					data => "eliminability",
				},
			]);
		};
		
		they "do not receive packets addressed to underlying interfaces" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", $wildcard_node, "4444",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 5555,
				
				data => "antisuffragist",
			);
			
			send_ipx_over_udp(
				dest_ip   => $net_b_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_b,
				
				type => 0,
				
				dest_network => "00:00:00:02",
				dest_node    => $remote_mac_b,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_b,
				src_socket  => 5555,
				
				data => "ambrosiaceous",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, []);
		};
	};
	
	before all => sub
	{
		$ptype_capture_class = "IPXWrapper::Capture::IPXOverUDP";
		
		$ptype_send_func = sub
		{
			my ($type, $data) = @_;
			
			send_ipx_over_udp(
				dest_ip   => $net_a_bcast,
				dest_port => UDP_BCAST_PORT,
				src_ip    => $local_ip_a,
				
				type => $type,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network => "00:00:00:01",
				src_node    => $local_mac_a,
				src_socket  => 1234,
				
				data => $data,
			);
		};
	};
	
	it_should_behave_like "ipx packet type handling";
};

runtests unless caller;

sub determine_private_port
{
	my ($isr, $capture_dev, $key) = @_;
	
	my $capture = IPXWrapper::Capture::IPXOverUDP->new($capture_dev);
	
	$isr->send("00:00:00:00", "FF:FF:FF:FF:FF:FF", "1234", $key);
	
	sleep(1);
	
	my ($ip_packet) = grep { $_->{data} eq $key }
		$capture->read_available();
	
	die("Couldn't determine port number of process")
		unless($ip_packet);
	
	return $ip_packet->{src_port};
}
