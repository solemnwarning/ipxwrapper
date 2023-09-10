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

use Test::Spec;

use FindBin;
use lib "$FindBin::Bin/lib/";

use IPXWrapper::DOSBoxClient;
use IPXWrapper::DOSBoxServer;
use IPXWrapper::Tool::IPXISR;
use IPXWrapper::Tool::IPXRecv;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);
our ($net_a_bcast, $net_b_bcast);
our ($dosbox_port);

describe "IPXWrapper using DOSBox UDP encapsulation" => sub
{
	my $dosbox_server;
	
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", ENCAP_TYPE_DOSBOX);
		reg_set_string($remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_addr", $local_ip_a);
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_port", $dosbox_port);
	};
	
	before each => sub
	{
		$dosbox_server = undef;
		$dosbox_server = IPXWrapper::DOSBoxServer->new($dosbox_port);
	};
	
	after all => sub
	{
		$dosbox_server = undef;
	};
	
	describe "a single process" => sub
	{
		it "handles unicast packets from the server" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-b", "00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			my $client = IPXWrapper::DOSBoxClient->new($local_ip_a, $dosbox_port);
			
			note("Their node number is ".$capture_a->node());
			note("My node number is ".$client->node());
			
			$client->send(
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:00",
				dest_node    => $capture_a->node(),
				dest_socket  => 4444,
				
				src_network => $client->net(),
				src_node    => $client->node(),
				src_socket  => 1234,
				
				data => "damage",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_network => "00:00:00:00",
					src_node    => $client->node(),
					src_socket  => 1234,
					
					data => "damage",
				},
			]);
		};
		
		it "handles broadcast packets from the server" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-b", "00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			my $client = IPXWrapper::DOSBoxClient->new($local_ip_a, $dosbox_port);
			
			note("Their node number is ".$capture_a->node());
			note("My node number is ".$client->node());
			
			$client->send(
				tc   => 0,
				type => 0,
				
				dest_network => "00:00:00:00",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => $client->net(),
				src_node    => $client->node(),
				src_socket  => 1234,
				
				data => "location",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_network => "00:00:00:00",
					src_node    => $client->node(),
					src_socket  => 1234,
					
					data => "location",
				},
			]);
		};
		
		it "sends unicast packets to server" => sub
		{
			my $client = IPXWrapper::DOSBoxClient->new($local_ip_a, $dosbox_port);
			
			note("My node number is ".$client->node());
			
			run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\ipx-send.exe",
				"-d" => "tasty",
				"-s" => "5555",
				"00:00:00:00", $client->node(), "4444",
			);
			
			sleep(1);
			
			my @packets = $client->recv_any();
			
			cmp_hashes_partial(\@packets, [
				{
					src_network => "00:00:00:00",
					src_socket  => 5555,
					
					dest_network => "00:00:00:00",
					dest_node    => $client->node(),
					dest_socket  => 4444,
					
					data => "tasty",
				},
			]);
		};
	};
	
	describe "concurrent processes" => sub
	{
		they "recieve direct unicast packets from another machine" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			my $capture_b = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"00:00:00:00", "00:00:00:00:00:00", "4445",
			);
			
			my $client = IPXWrapper::DOSBoxClient->new($local_ip_a, $dosbox_port);
			
			note("\$capture_a address is ".$capture_a->net()."/".$capture_a->node());
			note("\$capture_b address is ".$capture_b->net()."/".$capture_b->node());
			note("My address is ".$client->net()."/".$client->node());
			
			$client->send(
				tc   => 0,
				type => 0,
				
				dest_network => $capture_a->net(),,
				dest_node    => $capture_a->node(),
				dest_socket  => 4444,
				
				src_network => $client->net(),
				src_node    => $client->node(),
				src_socket  => 1234,
				
				data => "full",
			);
			
			$client->send(
				tc   => 0,
				type => 0,
				
				dest_network => $capture_b->net(),,
				dest_node    => $capture_b->node(),
				dest_socket  => 4445,
				
				src_network => $client->net(),
				src_node    => $client->node(),
				src_socket  => 1234,
				
				data => "accent",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			my @packets_b = $capture_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_network => $client->net(),
					src_node    => $client->node(),
					src_socket  => 1234,
					
					data => "full",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_network => $client->net(),
					src_node    => $client->node(),
					src_socket  => 1234,
					
					data => "accent",
				},
			]);
		};
		
		they "recieve direct unicast packets from each other" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXISR->new(
				$remote_ip_a,
				"00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			my $capture_b = IPXWrapper::Tool::IPXISR->new(
				$remote_ip_a,
				"00:00:00:00", "00:00:00:00:00:00", "4445",
			);
			
			note("\$capture_a address is ".$capture_a->net()."/".$capture_a->node());
			note("\$capture_b address is ".$capture_b->net()."/".$capture_b->node());
			
			$capture_a->send($capture_b->net(), $capture_b->node(), 4445, "reader");
			$capture_b->send($capture_a->net(), $capture_a->node(), 4444, "earwax");
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			my @packets_b = $capture_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_net    => $capture_b->net(),
					src_node   => $capture_b->node(),
					src_socket => 4445,
					
					data => "earwax",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_net    => $capture_a->net(),
					src_node   => $capture_a->node(),
					src_socket => 4444,
					
					data => "reader",
				},
			]);
		};
		
		they "recieve broadcast packets from another machine" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			my $capture_b = IPXWrapper::Tool::IPXRecv->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			my $client = IPXWrapper::DOSBoxClient->new($local_ip_a, $dosbox_port);
			
			note("\$capture_a address is ".$capture_a->net()."/".$capture_a->node());
			note("\$capture_b address is ".$capture_b->net()."/".$capture_b->node());
			note("My address is ".$client->net()."/".$client->node());
			
			$client->send(
				tc   => 0,
				type => 0,
				
				dest_network => "FF:FF:FF:FF",
				dest_node    => "FF:FF:FF:FF:FF:FF",
				dest_socket  => 4444,
				
				src_network => $client->net(),
				src_node    => $client->node(),
				src_socket  => 1234,
				
				data => "cylinder",
			);
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			my @packets_b = $capture_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_network => $client->net(),
					src_node    => $client->node(),
					src_socket  => 1234,
					
					data => "cylinder",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_network => $client->net(),
					src_node    => $client->node(),
					src_socket  => 1234,
					
					data => "cylinder",
				},
			]);
		};
		
		they "recieve broadcast packets from each other" => sub
		{
			my $capture_a = IPXWrapper::Tool::IPXISR->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			my $capture_b = IPXWrapper::Tool::IPXISR->new(
				$remote_ip_a,
				"-r", "-b", "00:00:00:00", "00:00:00:00:00:00", "4444",
			);
			
			note("\$capture_a address is ".$capture_a->net()."/".$capture_a->node());
			note("\$capture_b address is ".$capture_b->net()."/".$capture_b->node());
			
			$capture_a->send($capture_a->net(), "FF:FF:FF:FF:FF:FF", 4444, "export");
			$capture_b->send($capture_b->net(), "FF:FF:FF:FF:FF:FF", 4444, "addicted");
			
			sleep(1);
			
			my @packets_a = $capture_a->kill_and_read();
			my @packets_b = $capture_b->kill_and_read();
			
			cmp_hashes_partial(\@packets_a, [
				{
					src_net    => $capture_b->net(),
					src_node   => $capture_b->node(),
					src_socket => 4444,
					
					data => "addicted",
				},
			]);
			
			cmp_hashes_partial(\@packets_b, [
				{
					src_net    => $capture_a->net(),
					src_node   => $capture_a->node(),
					src_socket => 4444,
					
					data => "export",
				},
			]);
		};
	};
};

runtests unless caller;
