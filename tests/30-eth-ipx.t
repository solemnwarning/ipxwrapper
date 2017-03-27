# IPXWrapper test suite
# Copyright (C) 2014-2017 Daniel Collins <solemnwarning@solemnwarning.net>
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

use IPXWrapper::Capture::IPX;
use IPXWrapper::Capture::IPXNovell;
use IPXWrapper::Tool::IPXRecv;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);

require "$FindBin::Bin/ptype.pm";

our $ptype_send_func;
our $ptype_capture_class;

my $node_c_net = "00:00:00:00";

my $ipx_eth_capture_class;
my $ipx_eth_send_func;

shared_examples_for "ipx over ethernet" => sub
{
	describe "unicast packets" => sub
	{
		they "are received" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", $remote_mac_a, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network  => "00:00:00:01",
				src_node     => $local_mac_a,
				src_socket   => 8888,
				
				data => "oceanography",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 8888,
					
					data => "oceanography",
				},
			]);
		};
		
		# TODO
		they "are dropped if received on the wrong interface";
		
		they "are transmitted" => sub
		{
			my $capture = $ipx_eth_capture_class->new($local_dev_a);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-d" => "mammillae",
				"-s" => "5555", "-h" => $remote_mac_a,
				"00:00:00:01", $local_mac_a, "4444",
			);
			
			sleep(1);
			
			my @packets = $capture->read_available();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $remote_mac_a,
					src_socket  => 5555,
					
					dst_network => "00:00:00:01",
					dst_node    => $local_mac_a,
					dst_socket  => 4444,
					
					data => "mammillae",
				},
			]);
		};
	};
	
	describe "broadcast packets" => sub
	{
		they "are transmitted by sockets with SO_BROADCAST" => sub
		{
			my $capture = $ipx_eth_capture_class->new($local_dev_a);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-s" => "5555",
				"-h" => $remote_mac_a,
				"-b",
				"-d" => "recondite",
				"00:00:00:01", "FF:FF:FF:FF:FF:FF", "4444",
			);
			
			sleep(1);
			
			my @packets = $capture->read_available();
			
			cmp_hashes_partial(\@packets, [
				{
					dst_network => "00:00:00:01",
					dst_node    => "FF:FF:FF:FF:FF:FF",
					dst_socket  => 4444,
					
					src_network => "00:00:00:01",
					src_node    => $remote_mac_a,
					src_socket  => 5555,
					
					type => 0,
					data => "recondite",
				},
			]);
		};
		
		they "are only transmitted on the bound interface (Network A)" => sub
		{
			my $capture_a = $ipx_eth_capture_class->new($local_dev_a);
			my $capture_b = $ipx_eth_capture_class->new($local_dev_b);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-s" => "5555",
				"-h" => $remote_mac_a,
				"-b",
				"-d" => "condottiere",
				"00:00:00:01", "FF:FF:FF:FF:FF:FF", "4444",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->read_available();
			my @packets_b = $capture_b->read_available();
			
			cmp_hashes_partial(\@packets_a, [
				{
					dst_network => "00:00:00:01",
					dst_node    => "FF:FF:FF:FF:FF:FF",
					dst_socket  => 4444,
					
					src_network => "00:00:00:01",
					src_node    => $remote_mac_a,
					src_socket  => 5555,
					
					type => 0,
					data => "condottiere",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, []);
		};
		
		they "are only transmitted on the bound interface (Network B)" => sub
		{
			my $capture_a = $ipx_eth_capture_class->new($local_dev_a);
			my $capture_b = $ipx_eth_capture_class->new($local_dev_b);
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-s" => "5555",
				"-h" => $remote_mac_b,
				"-b",
				"-d" => "microstomous",
				$node_c_net, "FF:FF:FF:FF:FF:FF", "4444",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->read_available();
			my @packets_b = $capture_b->read_available();
			
			cmp_hashes_partial(\@packets_a, []);
			
			cmp_hashes_partial(\@packets_b, [
				{
					dst_network => $node_c_net,
					dst_node    => "FF:FF:FF:FF:FF:FF",
					dst_socket  => 4444,
					
					src_network => $node_c_net,
					src_node    => $remote_mac_b,
					src_socket  => 5555,
					
					type => 0,
					data => "microstomous",
				},
			]);
		};
		
		they "are received by SO_BROADCAST sockets (Network A)" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network  => "00:00:00:01",
				src_node     => $local_mac_a,
				src_socket   => 8888,
				
				data => "dyspepsia",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 8888,
					
					data => "dyspepsia",
				},
			]);
		};
		
		they "are received by SO_BROADCAST sockets (Network B)" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-b", "00:00:00:00", $remote_mac_b, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => $node_c_net,
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network  => $node_c_net,
				src_node     => $local_mac_b,
				src_socket   => 8888,
				
				data => "datelining",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => $node_c_net,
					src_node    => $local_mac_b,
					src_socket  => 8888,
					
					data => "datelining",
				},
			]);
		};
		
		they "aren't received if bound to another interface" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => $node_c_net,
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network  => $node_c_net,
				src_node     => $local_mac_b,
				src_socket   => 8888,
				
				data => "tripod",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, []);
		};
		
		they "aren't received by non-SO_BROADCAST sockets when win95 bug is enabled" => sub
		{
			reg_set_dword($remote_ip_a, "HKCU\\Software\\IPXWrapper", "w95_bug", 1);
			
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", $remote_mac_a, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network  => "00:00:00:01",
				src_node     => $local_mac_a,
				src_socket   => 8888,
				
				data => "boohoo",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, []);
		};
		
		they "are received by non-SO_BROADCAST sockets when win95 bug is disabled" => sub
		{
			reg_set_dword($remote_ip_a, "HKCU\\Software\\IPXWrapper", "w95_bug", 0);
			
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", $remote_mac_a, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network  => "00:00:00:01",
				src_node     => $local_mac_a,
				src_socket   => 8888,
				
				data => "horseshoer",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 8888,
					
					data => "horseshoer",
				},
			]);
		};
		
		they "are received by different sockets within the same process" => sub
		{
			my $capture = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network  => "00:00:00:01",
				src_node     => $local_mac_a,
				src_socket   => 8888,
				
				data => "guelfism",
			);
			
			sleep(1);
			
			my @packets = $capture->kill_and_read();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 8888,
					
					data => "guelfism",
				},
				
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 8888,
					
					data => "guelfism",
				},
			]) and isnt($packets[0]->{sock}, $packets[1]->{sock});
		};
		
		they "are received by sockets in different processes" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			my $capture_b = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", $remote_mac_a, "4444",
			);
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:01",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network  => "00:00:00:01",
				src_node     => $local_mac_a,
				src_socket   => 8888,
				
				data => "urinant",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			my @packets_b = $capture_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 8888,
					
					data => "urinant",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_network => "00:00:00:01",
					src_node    => $local_mac_a,
					src_socket  => 8888,
					
					data => "urinant",
				},
			]);
		};
	};
	
	before all => sub
	{
		$ptype_capture_class = $ipx_eth_capture_class;
		
		$ptype_send_func = sub
		{
			my ($type, $data) = @_;
			
			$ipx_eth_send_func->($local_dev_a,
				tc   => 0,
				type => $type,
				
				dest_network => "00:00:00:01",
				dest_node    => $remote_mac_a,
				dest_socket  => 4444,
				
				src_network  => "00:00:00:01",
				src_node     => $local_mac_a,
				src_socket   => 8888,
				
				data => $data,
			);
		};
	};
	
	it_should_behave_like "ipx packet type handling";
};

describe "IPXWrapper using Ethernet encapsulation" => sub
{
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", 1);
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "frame_type", 1);
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:00");
		
		$ipx_eth_capture_class = "IPXWrapper::Capture::IPX";
		$ipx_eth_send_func     = \&send_ipx_packet_ethernet;
	};
	
	it_should_behave_like "ipx over ethernet";
};

describe "IPXWrapper using Novell Ethernet encapsulation" => sub
{
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", 1);
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "frame_type", 2);
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:00");
		
		$ipx_eth_capture_class = "IPXWrapper::Capture::IPXNovell";
		$ipx_eth_send_func     = \&send_ipx_packet_novell;
	};
	
	it_should_behave_like "ipx over ethernet";
};

runtests unless caller;
