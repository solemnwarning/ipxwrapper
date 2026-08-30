# IPXWrapper test suite
# Copyright (C) 2026 Daniel Collins <solemnwarning@solemnwarning.net>
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
use Test::Exception;

use NetPacket::SPX qw(:constants);
use IPXWrapper::Tool::WSTool;
use IPXWrapper::Util;

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);

our $spx_send_func;
our $spx_capture_class;

shared_examples_for "spx protocol tests" => sub
{
	my $random_id = sub
	{
		return 1 + int(rand(65534));
	};
	
	my $send_to_app = sub
	{
		my ($dst_net, $dst_node, $dst_socket, $dst_connection_id, $src_net, $src_node, $src_socket, $src_connection_id, $seq, $ack, $data, $capture, $wstool, $sock) = @_;
		
		$spx_send_func->($local_dev_a,
			tc   => 0,
			type => 5,
			
			dest_network => $dst_net,
			dest_node    => $dst_node,
			dest_socket  => $dst_socket,
			
			src_network  => $src_net,
			src_node     => $src_node,
			src_socket   => $src_socket,
			
			connection_control  => SPX_CONNCTRL_ACK | SPX_CONNCTRL_EOM,
			datastream_type     => 0,
			src_connection_id   => $src_connection_id,
			dst_connection_id   => $dst_connection_id,
			seq_number          => $seq,
			ack_number          => $ack,
			allocation_number   => $ack,
			
			data => $data,
		);
		
		sleep(1);
		
		my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
		
		cmp_hashes_partial(\@packets, [
			{
				dst_network  => $src_net,
				dst_node     => $src_node,
				dst_socket   => $src_socket,
				
				src_network => $dst_net,
				src_node    => $dst_node,
				src_socket  => $dst_socket,
				
				connection_control  => SPX_CONNCTRL_SYS,
				datastream_type     => 0,
				dst_connection_id   => $src_connection_id,
				src_connection_id   => $dst_connection_id,
				seq_number          => $ack,
				ack_number          => ($seq + 1),
				allocation_number   => ($seq + 1),
			},
		]);
		
		$wstool->recv_start($sock, 64);
		my $recv_data = $wstool->recv_finish();
		
		is($recv_data, $data);
	};
	
	my $send_from_app = sub
	{
		my ($dst_net, $dst_node, $dst_socket, $dst_connection_id, $src_net, $src_node, $src_socket, $src_connection_id, $seq, $ack, $data, $capture, $wstool, $sock) = @_;
		
		$wstool->send($sock, $data);
		
		sleep(1);
		
		my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
		
		cmp_hashes_partial(\@packets, [
			{
				dst_network  => $dst_net,
				dst_node     => $dst_node,
				dst_socket   => $dst_socket,
				
				src_network => $src_net,
				src_node    => $src_node,
				src_socket  => $src_socket,
				
				connection_control  => SPX_CONNCTRL_ACK | SPX_CONNCTRL_EOM,
				datastream_type     => 0,
				dst_connection_id   => $dst_connection_id,
				src_connection_id   => $src_connection_id,
				seq_number          => $seq,
				ack_number          => $ack,
				allocation_number   => $ack,
				
				data => $data,
			},
		]);
		
		$spx_send_func->($local_dev_a,
			tc   => 0,
			type => 5,
			
			dest_network => $src_net,
			dest_node    => $src_node,
			dest_socket  => $src_socket,
			
			src_network  => $dst_net,
			src_node     => $dst_node,
			src_socket   => $dst_socket,
			
			connection_control  => SPX_CONNCTRL_SYS,
			datastream_type     => 0,
			src_connection_id   => $dst_connection_id,
			dst_connection_id   => $src_connection_id,
			seq_number          => $ack,
			ack_number          => ($seq + 1),
			allocation_number   => ($seq + 1),
			
			data => $data,
		);
	};
	
	describe "SPX clients" => sub
	{
		my $connect_client = sub
		{
			my ($client, $capture, $server_net, $server_node, $server_socket) = @_;
			
			my $client_sock = $client->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
			
			$client->connect_start($client_sock, $server_net, $server_node, $server_socket);
			
			sleep(1);

			my $conn_request = {
				dst_network  => $server_net,
				dst_node     => $server_node,
				dst_socket   => $server_socket,
				
				connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
				datastream_type     => 0,
				dst_connection_id   => 0xFFFF,
				seq_number          => 0,
				ack_number          => 0,
			};

			my @packets = $capture->read_available();

			# There may be a retransmitted connection request depending on timing.
			cmp_hashes_partial(\@packets, [ $conn_request ], [ $conn_request ]) or return;
			
			isnt($packets[0]->{src_connection_id}, 0);
			isnt($packets[0]->{src_connection_id}, 0xFFFF);
			
			my $server_conn_id = $random_id->();
			
			$spx_send_func->($local_dev_a,
				tc   => 0,
				type => 5,
				
				dest_network => $packets[0]->{src_network},
				dest_node    => $packets[0]->{src_node},
				dest_socket  => $packets[0]->{src_socket},
				
				src_network  => $server_net,
				src_node     => $server_node,
				src_socket   => $server_socket,
				
				connection_control  => SPX_CONNCTRL_SYS,
				datastream_type     => 0,
				src_connection_id   => $server_conn_id,
				dst_connection_id   => $packets[0]->{src_connection_id},
				seq_number          => 0,
				ack_number          => 0,
				allocation_number   => 0,
				
				data => "",
			);
			
			$client->connect_finish();
			
			return ($client_sock, $packets[0]->{src_connection_id}, $server_conn_id);
		};
		
		they "can communicate with an SPX server" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $client = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			
			my ($client_sock, $client_conn_id, $server_conn_id) = $connect_client->($client, $capture, "00:00:00:01", $local_mac_a, 1234);
			my $client_addr = $client->getsockname($client_sock);
			
			my $app_to_test_seq = 0;
			my $test_to_app_seq = 0;
			
			while($test_to_app_seq < 30)
			{
				$send_to_app->(
					$client_addr->{ipx_netnum}, $client_addr->{ipx_nodenum}, $client_addr->{ipx_socket}, $client_conn_id,
					"00:00:00:01", $local_mac_a, 1234, $server_conn_id,
					$test_to_app_seq,
					$app_to_test_seq,
					"remote server to ipxwrapper spx client ${test_to_app_seq}",
					$capture,
					$client,
					$client_sock);
				
				++$test_to_app_seq;
			}
			
			while($app_to_test_seq < 30)
			{
				$send_from_app->(
					"00:00:00:01", $local_mac_a, 1234, $server_conn_id,
					$client_addr->{ipx_netnum}, $client_addr->{ipx_nodenum}, $client_addr->{ipx_socket}, $client_conn_id,
					$app_to_test_seq,
					$test_to_app_seq,
					"ipxwrapper spx client to remote server ${app_to_test_seq}",
					$capture,
					$client,
					$client_sock);
				
				++$app_to_test_seq;
			}
		};
		
		they "retransmit connection requests until the server answers" => sub
		{
			my $server_net    = "00:00:00:01";
			my $server_node   = $local_mac_a;
			my $server_socket = 1234;
			
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $client = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			
			my $client_sock = $client->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
			
			$client->connect_start($client_sock, $server_net, $server_node, $server_socket);
			
			sleep(10);

			my $conn_request = {
				dst_network  => $server_net,
				dst_node     => $server_node,
				dst_socket   => $server_socket,
				
				connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
				datastream_type     => 0,
				dst_connection_id   => 0xFFFF,
				seq_number          => 0,
				ack_number          => 0,
			};
			
			my @conn_request_x4 = map { $conn_request } (1..4);

			my @packets = $capture->read_available();

			cmp_hashes_partial(\@packets, \@conn_request_x4, [ $conn_request ]) or return;
			
			isnt($packets[0]->{src_connection_id}, 0);
			isnt($packets[0]->{src_connection_id}, 0xFFFF);
			
			my $server_conn_id = $random_id->();
			
			$spx_send_func->($local_dev_a,
				tc   => 0,
				type => 5,
				
				dest_network => $packets[0]->{src_network},
				dest_node    => $packets[0]->{src_node},
				dest_socket  => $packets[0]->{src_socket},
				
				src_network  => $server_net,
				src_node     => $server_node,
				src_socket   => $server_socket,
				
				connection_control  => SPX_CONNCTRL_SYS,
				datastream_type     => 0,
				src_connection_id   => $server_conn_id,
				dst_connection_id   => $packets[0]->{src_connection_id},
				seq_number          => 0,
				ack_number          => 0,
				allocation_number   => 0,
				
				data => "",
			);
			
			sleep(10);
			
			my $watchdog_request = {
				src_network => $packets[0]->{src_network},
				src_node    => $packets[0]->{src_node},
				src_socket  => $packets[0]->{src_socket},
				
				dst_network  => "00:00:00:01",
				dst_node     => $local_mac_a,
				dst_socket   => 1234,
				
				connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
				datastream_type     => 0,
				src_connection_id   => $packets[0]->{src_connection_id},
				dst_connection_id   => $server_conn_id,
				seq_number          => 0,
				ack_number          => 0,
			};
			
			@packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
			cmp_hashes_partial(\@packets, [ $watchdog_request, $watchdog_request, $watchdog_request ], [ $conn_request ]) or return;
			
			$client->connect_finish();
		};
		
		they "time out when the server doesn't respond to the connection request" => sub
		{
			my $server_net    = "00:00:00:01";
			my $server_node   = $local_mac_a;
			my $server_socket = 1234;
			
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $client = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			
			my $client_sock = $client->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
			
			$client->connect_start($client_sock, $server_net, $server_node, $server_socket);
			
			sleep(40);

			my $conn_request = {
				dst_network  => $server_net,
				dst_node     => $server_node,
				dst_socket   => $server_socket,
				
				connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
				datastream_type     => 0,
				dst_connection_id   => 0xFFFF,
				seq_number          => 0,
				ack_number          => 0,
			};
			
			my @conn_request_x10 = map { $conn_request } (1..10);

			my @packets = $capture->read_available();

			cmp_hashes_partial(\@packets, \@conn_request_x10) or return;
			
			isnt($packets[0]->{src_connection_id}, 0);
			isnt($packets[0]->{src_connection_id}, 0xFFFF);
			
			throws_ok { $client->connect_finish(); } qr/connect failed with error code 10061/;
		};
		
		they "transmit watchdog packets during inactivity" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $client = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			
			my ($client_sock, $client_conn_id, $server_conn_id) = $connect_client->($client, $capture, "00:00:00:01", $local_mac_a, 1234);
			my $client_addr = $client->getsockname($client_sock);
			
			sleep(1);
			
			for(my $i = 0; $i < 15; ++$i)
			{
				sleep(3);
				
				my $watchdog_request = {
					src_network => $client_addr->{ipx_netnum},
					src_node    => $client_addr->{ipx_nodenum},
					src_socket  => $client_addr->{ipx_socket},
					
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => 1234,
					
					connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
					datastream_type     => 0,
					src_connection_id   => $client_conn_id,
					dst_connection_id   => $server_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				};
				
				my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
				
				cmp_hashes_partial(\@packets, [ $watchdog_request ]) or return;
				
				$spx_send_func->($local_dev_a,
					tc   => 0,
					type => 5,
					
					src_network  => "00:00:00:01",
					src_node     => $local_mac_a,
					src_socket   => 1234,
					
					dest_network => $client_addr->{ipx_netnum},
					dest_node    => $client_addr->{ipx_nodenum},
					dest_socket  => $client_addr->{ipx_socket},
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					src_connection_id   => $server_conn_id,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
					allocation_number   => 0,
					
					data => "",
				);
			}
			
			# Make sure the connection is still functional
			$send_to_app->(
				$client_addr->{ipx_netnum}, $client_addr->{ipx_nodenum}, $client_addr->{ipx_socket}, $client_conn_id,
				"00:00:00:01", $local_mac_a, 1234, $server_conn_id,
				0,
				0,
				"still alive?",
				$capture,
				$client,
				$client_sock);
		};
		
		they "time out when the server doesn't respond to watchdog packets" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $client = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			
			my ($client_sock, $client_conn_id, $server_conn_id) = $connect_client->($client, $capture, "00:00:00:01", $local_mac_a, 1234);
			my $client_addr = $client->getsockname($client_sock);
			
			sleep(1);
			
			for(my $i = 0; $i < 9; ++$i)
			{
				sleep(3);
				
				my $watchdog_request = {
					src_network => $client_addr->{ipx_netnum},
					src_node    => $client_addr->{ipx_nodenum},
					src_socket  => $client_addr->{ipx_socket},
					
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => 1234,
					
					connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
					datastream_type     => 0,
					src_connection_id   => $client_conn_id,
					dst_connection_id   => $server_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				};
				
				my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
				
				cmp_hashes_partial(\@packets, [ $watchdog_request ]) or return;
				
				# Verify socket isn't signalled yet.
				my ($read_ready, undef, undef) = $client->select([ $client_sock ], undef, undef, 0);
				cmp_set($read_ready, []);
			}
			
			sleep(3);
			
			# Connection should've timed out now - check it stopped sending watchdog requests.
			
			cmp_hashes_partial([ $capture->read_available() ], []) or return;
			
			# Socket should be flagged as readable now.
			my ($read_ready, undef, undef) = $client->select([ $client_sock ], undef, undef, 0);
			cmp_set($read_ready, [ $client_sock ]);
			
			# Verify recv() returns an error.
			throws_ok { $client->recv($client_sock, 64); } qr/recv failed with error code 10054/;
		};
		
		they "responds to graceful disconnects from the server";
		
		they "propagate graceful disconnects from the application";
		
		they "respond to watchdog packets from the server" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $client = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			
			my ($client_sock, $client_conn_id, $server_conn_id) = $connect_client->($client, $capture, "00:00:00:01", $local_mac_a, 1234);
			my $client_addr = $client->getsockname($client_sock);
			
			for(my $i = 0; $i < 10; ++$i)
			{
				$spx_send_func->($local_dev_a,
					tc   => 0,
					type => 5,
					
					src_network  => "00:00:00:01",
					src_node     => $local_mac_a,
					src_socket   => 1234,
					
					dest_network => $client_addr->{ipx_netnum},
					dest_node    => $client_addr->{ipx_nodenum},
					dest_socket  => $client_addr->{ipx_socket},
					
					connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
					datastream_type     => 0,
					src_connection_id   => $server_conn_id,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
					allocation_number   => 0,
					
					data => "",
				);
				
				sleep(1);
				
				my $watchdog_response = {
					src_network => $client_addr->{ipx_netnum},
					src_node    => $client_addr->{ipx_nodenum},
					src_socket  => $client_addr->{ipx_socket},
					
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => 1234,
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					src_connection_id   => $client_conn_id,
					dst_connection_id   => $server_conn_id,
					seq_number          => 0,
					ack_number          => 0,
					allocation_number   => 0,
				};
				
				my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
				
				cmp_hashes_partial(\@packets, [ $watchdog_response ]) or return;
			}
		};
		
		they "can queue multiple outgoing messages";
	};
	
	describe "SPX servers" => sub
	{
		my $setup_listener = sub
		{
			my ($server, $bind_net, $bind_node, $bind_socket) = @_;
			
			my $listener = $server->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
			
			$server->bind($listener, $bind_net, $bind_node, $bind_socket);
			$server->listen($listener, 10);
			
			my $listener_addr = $server->getsockname($listener);
			
			return ($listener, $listener_addr->{ipx_netnum}, $listener_addr->{ipx_nodenum}, $listener_addr->{ipx_socket});
		};
		
		my $send_conn_request = sub
		{
			my ($server_net, $server_node, $server_socket, $client_net, $client_node, $client_socket, $client_conn_id) = @_;
			
			$spx_send_func->($local_dev_a,
				tc   => 0,
				type => 5,
				
				dest_network => $server_net,
				dest_node    => $server_node,
				dest_socket  => $server_socket,
				
				src_network  => $client_net,
				src_node     => $client_node,
				src_socket   => $client_socket,
				
				connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
				datastream_type     => 0,
				src_connection_id   => $client_conn_id,
				dst_connection_id   => 0xFFFF,
				seq_number          => 0,
				ack_number          => 0,
				allocation_number   => 0,
				
				data => "",
			);
		};
		
		they "can communicate with an SPX client" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $server = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			my ($listener, $listener_net, $listener_node, $listener_socket) = $setup_listener->($server, "00:00:00:00", $remote_mac_a, "0");
			
			# Enable non-blocking I/O on listener socket.
			$server->ioctlsocket($listener, FIONBIO, "00000001");
			
			# Send SPX connection request.
			
			my $client_socket = $random_id->();
			my $client_conn_id = $random_id->();
			
			$send_conn_request->($listener_net, $listener_node, $listener_socket, "00:00:00:01", $local_mac_a, $client_socket, $client_conn_id);
			
			# Process the connection request.
			
			sleep(1);
			
			$server->accept_start($listener);
			my $server_client = $server->accept_finish();
			
			# Check for connection acknowledgement.
			
			sleep(1);
			
			my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
			
			cmp_hashes_partial(\@packets, [
				{
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => $client_socket,
					
					src_network => $listener_net,
					src_node    => $listener_node,
					src_socket  => $listener_socket,
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				},
			]) or return;
			
			my ($server_conn_id) = $packets[0]->{src_connection_id};
			
			isnt($server_conn_id, 0);
			isnt($server_conn_id, 0xFFFF);
			
			is($server_client->{ipx_netnum}, "00:00:00:01");
			is($server_client->{ipx_nodenum}, $local_mac_a);
			is($server_client->{ipx_socket}, $client_socket);
			
			my $app_to_test_seq = 0;
			my $test_to_app_seq = 0;
			
			while($test_to_app_seq < 30)
			{
				$send_to_app->(
					$listener_net, $listener_node, $listener_socket, $server_conn_id,
					"00:00:00:01", $local_mac_a, $client_socket, $client_conn_id,
					$test_to_app_seq,
					$app_to_test_seq,
					"remote client to ipxwrapper spx server ${test_to_app_seq}",
					$capture,
					$server,
					$server_client->{socket});
				
				++$test_to_app_seq;
			}
			
			while($app_to_test_seq < 30)
			{
				$send_from_app->(
					"00:00:00:01", $local_mac_a, $client_socket, $client_conn_id,
					$listener_net, $listener_node, $listener_socket, $server_conn_id,
					$app_to_test_seq,
					$test_to_app_seq,
					"ipxwrapper spx server to remote client ${app_to_test_seq}",
					$capture,
					$server,
					$server_client->{socket});
				
				++$app_to_test_seq;
			}
		};
		
		they "discard connection requests when a duplicate request is received (before accept)";
		
		they "retransmit connection acknowledgements when a duplicate request is received (after accept)";
		
		they "queue multiple distinct connection requests";
		
		they "transmit watchdog packets during inactivity" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $server = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			my ($listener, $listener_net, $listener_node, $listener_socket) = $setup_listener->($server, "00:00:00:00", $remote_mac_a, "0");
			
			# Enable non-blocking I/O on listener socket.
			$server->ioctlsocket($listener, FIONBIO, "00000001");
			
			# Send SPX connection request.
			
			my $client_socket = $random_id->();
			my $client_conn_id = $random_id->();
			
			$send_conn_request->($listener_net, $listener_node, $listener_socket, "00:00:00:01", $local_mac_a, $client_socket, $client_conn_id);
			
			# Process the connection request.
			
			sleep(1);
			
			$server->accept_start($listener);
			my $server_client = $server->accept_finish();
			
			# Check for connection acknowledgement.
			
			sleep(1);
			
			my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
			
			cmp_hashes_partial(\@packets, [
				{
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => $client_socket,
					
					src_network => $listener_net,
					src_node    => $listener_node,
					src_socket  => $listener_socket,
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				},
			]) or return;
			
			my ($server_conn_id) = $packets[0]->{src_connection_id};
			
			for(my $i = 0; $i < 15; ++$i)
			{
				sleep(3);
				
				my $watchdog_request = {
					src_network => $listener_net,
					src_node    => $listener_node,
					src_socket  => $listener_socket,
					
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => $client_socket,
					
					connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
					datastream_type     => 0,
					src_connection_id   => $server_conn_id,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				};
				
				my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
				
				cmp_hashes_partial(\@packets, [ $watchdog_request ]) or return;
				
				$spx_send_func->($local_dev_a,
					tc   => 0,
					type => 5,
					
					src_network  => "00:00:00:01",
					src_node     => $local_mac_a,
					src_socket   => 1234,
					
					dest_network => $listener_net,
					dest_node    => $listener_node,
					dest_socket  => $listener_socket,
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					src_connection_id   => $client_conn_id,
					dst_connection_id   => $server_conn_id,
					seq_number          => 0,
					ack_number          => 0,
					allocation_number   => 0,
					
					data => "",
				);
			}
			
			# Make sure the connection is still functional
			$send_to_app->(
					$listener_net, $listener_node, $listener_socket, $server_conn_id,
					"00:00:00:01", $local_mac_a, $client_socket, $client_conn_id,
					0,
					0,
					"numerous beef control",
					$capture,
					$server,
					$server_client->{socket});
		};
		
		they "time out when the client doesn't respond to watchdog packets" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $server = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			my ($listener, $listener_net, $listener_node, $listener_socket) = $setup_listener->($server, "00:00:00:00", $remote_mac_a, "0");
			
			# Enable non-blocking I/O on listener socket.
			$server->ioctlsocket($listener, FIONBIO, "00000001");
			
			# Send SPX connection request.
			
			my $client_socket = $random_id->();
			my $client_conn_id = $random_id->();
			
			$send_conn_request->($listener_net, $listener_node, $listener_socket, "00:00:00:01", $local_mac_a, $client_socket, $client_conn_id);
			
			# Process the connection request.
			
			sleep(1);
			
			$server->accept_start($listener);
			my $server_client = $server->accept_finish();
			
			# Check for connection acknowledgement.
			
			sleep(1);
			
			my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
			
			cmp_hashes_partial(\@packets, [
				{
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => $client_socket,
					
					src_network => $listener_net,
					src_node    => $listener_node,
					src_socket  => $listener_socket,
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				},
			]) or return;
			
			my ($server_conn_id) = $packets[0]->{src_connection_id};
			
			for(my $i = 0; $i < 9; ++$i)
			{
				sleep(3);
				
				my $watchdog_request = {
					src_network => $listener_net,
					src_node    => $listener_node,
					src_socket  => $listener_socket,
					
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => $client_socket,
					
					connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
					datastream_type     => 0,
					src_connection_id   => $server_conn_id,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				};
				
				my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
				
				cmp_hashes_partial(\@packets, [ $watchdog_request ]) or return;
				
				# Verify socket isn't signalled yet.
				my ($read_ready, undef, undef) = $server->select([ $server_client->{socket} ], undef, undef, 0);
				cmp_set($read_ready, []);
			}
			
			sleep(3);
			
			# Connection should've timed out now - check it stopped sending watchdog requests.
			
			cmp_hashes_partial([ $capture->read_available() ], []) or return;
			
			# Socket should be flagged as readable now.
			my ($read_ready, undef, undef) = $server->select([ $server_client->{socket} ], undef, undef, 0);
			cmp_set($read_ready, [ $server_client->{socket} ]);
			
			# Verify recv() returns an error.
			throws_ok { $server->recv($server_client->{socket}, 64); } qr/recv failed with error code 10054/;
		};
		
		they "responds to graceful disconnects from the client";
		
		they "propagate graceful disconnects from the application";
		
		they "respond to watchdog packets from the client" => sub
		{
			my $capture = $spx_capture_class->new($local_dev_a);
			
			my $server = IPXWrapper::Tool::WSTool->new($remote_ip_a);
			my ($listener, $listener_net, $listener_node, $listener_socket) = $setup_listener->($server, "00:00:00:00", $remote_mac_a, "0");
			
			# Enable non-blocking I/O on listener socket.
			$server->ioctlsocket($listener, FIONBIO, "00000001");
			
			# Send SPX connection request.
			
			my $client_socket = $random_id->();
			my $client_conn_id = $random_id->();
			
			$send_conn_request->($listener_net, $listener_node, $listener_socket, "00:00:00:01", $local_mac_a, $client_socket, $client_conn_id);
			
			# Process the connection request.
			
			sleep(1);
			
			$server->accept_start($listener);
			my $server_client = $server->accept_finish();
			
			# Check for connection acknowledgement.
			
			sleep(1);
			
			my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
			
			cmp_hashes_partial(\@packets, [
				{
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => $client_socket,
					
					src_network => $listener_net,
					src_node    => $listener_node,
					src_socket  => $listener_socket,
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
				},
			]) or return;
			
			my ($server_conn_id) = $packets[0]->{src_connection_id};
			
			for(my $i = 0; $i < 10; ++$i)
			{
				$spx_send_func->($local_dev_a,
					tc   => 0,
					type => 5,
					
					src_network  => "00:00:00:01",
					src_node     => $local_mac_a,
					src_socket   => $client_socket,
					
					dest_network => $listener_net,
					dest_node    => $listener_node,
					dest_socket  => $listener_socket,
					
					connection_control  => SPX_CONNCTRL_SYS | SPX_CONNCTRL_ACK,
					datastream_type     => 0,
					src_connection_id   => $client_conn_id,
					dst_connection_id   => $server_conn_id,
					seq_number          => 0,
					ack_number          => 0,
					allocation_number   => 0,
					
					data => "",
				);
				
				sleep(1);
				
				my $watchdog_response = {
					src_network => $listener_net,
					src_node    => $listener_node,
					src_socket  => $listener_socket,
					
					dst_network  => "00:00:00:01",
					dst_node     => $local_mac_a,
					dst_socket   => $client_socket,
					
					connection_control  => SPX_CONNCTRL_SYS,
					datastream_type     => 0,
					src_connection_id   => $server_conn_id,
					dst_connection_id   => $client_conn_id,
					seq_number          => 0,
					ack_number          => 0,
					allocation_number   => 0,
				};
				
				my @packets = grep { !mac_eq($_->{src_mac}, $local_mac_a) } $capture->read_available();
				
				cmp_hashes_partial(\@packets, [ $watchdog_response ]) or return;
			}
		};
		
		they "can queue multiple outgoing messages";
	};
};

shared_examples_for "spx self tests" => sub
{
	it "can exchange data between SPX sockets in different processes (blocking I/O)" => sub
	{
		my $proc_a = IPXWrapper::Tool::WSTool->new($remote_ip_a);

		my $listener = $proc_a->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);

		$proc_a->bind($listener, "00:00:00:00", "00:00:00:00:00:00", "0");
		$proc_a->listen($listener, 10);

		my $listener_addr = $proc_a->getsockname($listener);
		$proc_a->accept_start($listener);

		my $proc_b = IPXWrapper::Tool::WSTool->new($remote_ip_a);

		my $client = $proc_b->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
		$proc_b->connect_start($client, $listener_addr->{ipx_netnum}, $listener_addr->{ipx_nodenum}, $listener_addr->{ipx_socket});
		$proc_b->connect_finish();

		my $client_addr = $proc_b->getsockname($client);

		my $client_peer = $proc_a->accept_finish();

		# Check address returned by accept() matches the local address of the client socket.
		cmp_hashes_partial([ $client_peer ], [ $client_addr ]);

		$proc_a->send($client_peer->{socket}, "class");

		is($proc_b->recv($client, 64), "class");

		$proc_b->send($client, "industrious");

		is($proc_a->recv($client_peer->{socket}, 64), "industrious");
	};
	
	it "times out when SPX connection fails (blocking I/O)" => sub
	{
		my $proc_a = IPXWrapper::Tool::WSTool->new($remote_ip_a);

		my $client = $proc_a->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
		$proc_a->connect_start($client, "00:00:00:00", "AA:BB:CC:DD:EE:FF", 1234);
		
		my $start_time = time();
		
		throws_ok { $proc_a->connect_finish(); } qr/connect failed with error code 10061/;
		
		my $end_time = time();
		
		# Default timeout is 30s, check for >25s in case of heavy system load.
		
		ok(($end_time - $start_time) > 25);
	};

	it "propagates a graceful disconnect" => sub
	{
		my $proc_a = IPXWrapper::Tool::WSTool->new($remote_ip_a);

		my $listener = $proc_a->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);

		$proc_a->bind($listener, "00:00:00:00", "00:00:00:00:00:00", "0");
		$proc_a->listen($listener, 10);

		my $listener_addr = $proc_a->getsockname($listener);
		$proc_a->accept_start($listener);

		my $proc_b = IPXWrapper::Tool::WSTool->new($remote_ip_a);

		my $client = $proc_b->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
		$proc_b->connect_start($client, $listener_addr->{ipx_netnum}, $listener_addr->{ipx_nodenum}, $listener_addr->{ipx_socket});
		$proc_b->connect_finish();

		my $client_addr = $proc_b->getsockname($client);

		my $client_peer = $proc_a->accept_finish();

		$proc_b->closesocket($client);

		is($proc_a->recv($client_peer->{socket}, 64), "");
	};

	it "times out when connected peer stops responding" => sub
	{
		my $proc_a = IPXWrapper::Tool::WSTool->new($remote_ip_a);

		my $listener = $proc_a->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);

		$proc_a->bind($listener, "00:00:00:00", "00:00:00:00:00:00", "0");
		$proc_a->listen($listener, 10);

		my $listener_addr = $proc_a->getsockname($listener);
		$proc_a->accept_start($listener);

		my $proc_b = IPXWrapper::Tool::WSTool->new($remote_ip_a);

		my $client = $proc_b->socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX);
		$proc_b->connect_start($client, $listener_addr->{ipx_netnum}, $listener_addr->{ipx_nodenum}, $listener_addr->{ipx_socket});
		$proc_b->connect_finish();

		my $client_addr = $proc_b->getsockname($client);

		my $client_peer = $proc_a->accept_finish();

		$proc_b = undef;

		my $start_time = time();
		
		throws_ok { $proc_a->recv($client_peer->{socket}, 64); } qr/recv failed with error code 10054/;
		
		my $end_time = time();
		
		# Default timeout is 30s, check for >25s in case of heavy system load.
		
		diag("recv finished after approx ".($end_time - $start_time)." seconds");
		ok(($end_time - $start_time) > 25);
	};
};

1;
