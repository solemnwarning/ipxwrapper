# IPXWrapper test suite
# Copyright (C) 2014-2026 Daniel Collins <solemnwarning@solemnwarning.net>
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

use FindBin;
use lib "$FindBin::Bin/lib/";

use IPXWrapper::Capture::IPXOverUDP;
use IPXWrapper::SPX;
use IPXWrapper::Tool::Generic;
use IPXWrapper::Tool::WSTool;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);
our ($net_a_bcast, $net_b_bcast);

describe "IPXWrapper using IP encapsulation" => sub
{
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\00:00:00:00:00:00", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:02");
	};
	
	# TODO: Test wildcard specific cases.

	it "can exchange data between SPX sockets (blocking I/O)" => sub
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

		$proc_b->exit();

		my $start_time = time();
		
		throws_ok { $proc_a->recv($client_peer->{socket}, 64); } qr/recv failed with error code 10054/;
		
		my $end_time = time();
		
		# Default timeout is 30s, check for >25s in case of heavy system load.
		
		ok(($end_time - $start_time) > 25);
	};
};

runtests unless caller;
