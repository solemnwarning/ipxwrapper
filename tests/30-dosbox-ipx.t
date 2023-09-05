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

require "$FindBin::Bin/ptype.pm";

our $ptype_send_func;
our $ptype_capture_class;

describe "IPXWrapper using DOSBox UDP encapsulation" => sub
{
	my $dosbox_server;
	
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", ENCAP_TYPE_DOSBOX);
		reg_set_string($remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_addr", $local_ip_a);
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_port", $dosbox_port);
		
		$dosbox_server = IPXWrapper::DOSBoxServer->new($dosbox_port);
	};
	
	after all => sub
	{
		$dosbox_server = undef;
	};
	
	it "handles unicast packets from the server" => sub
	{
		my $capture_a = IPXWrapper::Tool::IPXRecv->new(
			$remote_ip_a,
			"-b", "-r", "00:00:00:00", "00:00:00:00:00:00", "4444",
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
			"-b", "-r", "00:00:00:00", "00:00:00:00:00:00", "4444",
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

runtests unless caller;
