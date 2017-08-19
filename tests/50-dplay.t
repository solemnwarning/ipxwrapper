# IPXWrapper test suite
# Copyright (C) 2015 Daniel Collins <solemnwarning@solemnwarning.net>
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

# Number of seconds to wait for things to propagate.
use constant PROP_TIME => 3;

use Test::Spec;

use FindBin;
use lib "$FindBin::Bin/lib/";

use IPXWrapper::Tool::DPTool;

require "$FindBin::Bin/config.pm";
our ($remote_ip_a, $remote_b_ip, $remote_c_ip);

# Nicked from dplay.h
use constant DPID_ALLPLAYERS => 0;

describe "A single DirectPlay client" => sub
{
	it "can find a published session" => sub
	{
		my $host   = IPXWrapper::Tool::DPTool->new($remote_b_ip);
		my $s_guid = $host->create_session("catarrhous");
		
		my $client   = IPXWrapper::Tool::DPTool->new($remote_ip_a);
		my %sessions = $client->list_sessions();
		
		cmp_deeply(\%sessions, {
			$s_guid => "catarrhous",
		});
	};
	
	it "can find concurrently published sessions" => sub
	{
		my $host_a  = IPXWrapper::Tool::DPTool->new($remote_b_ip);
		my $sa_guid = $host_a->create_session("sturt");
		
		my $host_b  = IPXWrapper::Tool::DPTool->new($remote_c_ip);
		my $sb_guid = $host_b->create_session("duodena");
		
		my $client   = IPXWrapper::Tool::DPTool->new($remote_ip_a);
		my %sessions = $client->list_sessions();
		
		cmp_deeply(\%sessions, {
			$sa_guid => "sturt",
			$sb_guid => "duodena",
		});
	};
	
	my $single_client_common_init = sub
	{
		my $host   = IPXWrapper::Tool::DPTool->new($remote_b_ip);
		my $s_guid = $host->create_session("chaya");
		
		my $client = IPXWrapper::Tool::DPTool->new($remote_ip_a);
		$client->list_sessions();
		$client->join_session($s_guid);
		
		return ($host, $client);
	};
	
	it "can join a published session" => sub
	{
		my ($host, $client) = $single_client_common_init->();
		
		pass(); # We didn't die!
	};
	
	it "can see players on the host" => sub
	{
		my ($host, $client) = $single_client_common_init->();
		
		my $host_player = $host->create_player("host");
		sleep(PROP_TIME);
		
		my %client_players = $client->list_players();
		
		cmp_deeply(\%client_players, {
			$host_player => "host",
		});
	};
	
	it "can create players visible to the host" => sub
	{
		my ($host, $client) = $single_client_common_init->();
		
		my $client_player = $client->create_player("client");
		sleep(PROP_TIME);
		
		my %host_players = $host->list_players();
		
		cmp_deeply(\%host_players, {
			$client_player => "client",
		});
	};
	
	it "can receive a message from the host" => sub
	{
		my ($host, $client) = $single_client_common_init->();
		
		my $host_player   = $host->create_player("host");
		my $client_player = $client->create_player("client");
		sleep(PROP_TIME);
		
		$host->send_message($host_player, $client_player, "foreordainment");
		
		sleep(PROP_TIME);
		
		$client->exit();
		$host->exit();
		
		my @host_messages   = $host->messages();
		my @client_messages = $client->messages();
		
		cmp_deeply(\@host_messages, []);
		
		cmp_deeply(\@client_messages, [
			{
				from    => $host_player,
				to      => $client_player,
				message => "foreordainment",
			},
		]);
	};
	
	it "can send messages to the host" => sub
	{
		my ($host, $client) = $single_client_common_init->();
		
		my $host_player   = $host->create_player("host");
		my $client_player = $client->create_player("client");
		sleep(PROP_TIME);
		
		$client->send_message($client_player, $host_player, "iskenderun");
		
		sleep(PROP_TIME);
		
		$client->exit();
		$host->exit();
		
		my @host_messages   = $host->messages();
		my @client_messages = $client->messages();
		
		cmp_deeply(\@host_messages, [
			{
				from    => $client_player,
				to      => $host_player,
				message => "iskenderun",
			},
		]);
		
		cmp_deeply(\@client_messages, []);
	};
};

describe "Concurrent DirectPlay clients" => sub
{
	my $multi_client_common_init = sub
	{
		my $host   = IPXWrapper::Tool::DPTool->new($remote_ip_a);
		my $s_guid = $host->create_session("meninges");
		
		my $client_a = IPXWrapper::Tool::DPTool->new($remote_b_ip);
		$client_a->list_sessions();
		$client_a->join_session($s_guid);
		
		my $client_b = IPXWrapper::Tool::DPTool->new($remote_c_ip);
		$client_b->list_sessions();
		$client_b->join_session($s_guid);
		
		return ($host, $client_a, $client_b);
	};
	
	they "can join a published session" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		pass(); # Didn't die!
	};
	
	they "can see players on the host" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		my $host_player = $host->create_player("host");
		
		sleep(PROP_TIME);
		
		my %ca_players = $client_a->list_players();
		my %cb_players = $client_b->list_players();
		
		cmp_deeply(\%ca_players, { $host_player => "host" });
		cmp_deeply(\%cb_players, { $host_player => "host" });
	};
	
	they "can create players visible to the host" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		my $ca_player = $client_a->create_player("client_a");
		my $cb_player = $client_b->create_player("client_b");
		
		sleep(PROP_TIME);
		
		my %host_players = $host->list_players();
		
		cmp_deeply(\%host_players, {
			$ca_player => "client_a",
			$cb_player => "client_b",
		});
	};
	
	they "can create players visible to each other" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		my $ca_player = $client_a->create_player("client_a");
		my $cb_player = $client_b->create_player("client_b");
		
		sleep(PROP_TIME);
		
		my %ca_players = $client_a->list_players();
		my %cb_players = $client_b->list_players();
		
		my %expect_players = (
			$ca_player => "client_a",
			$cb_player => "client_b",
		);
		
		cmp_deeply(\%ca_players, \%expect_players);
		cmp_deeply(\%cb_players, \%expect_players);
	};
	
	they "can receive messages from the host" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		my $host_player = $host->create_player("host");
		my $ca_player   = $client_a->create_player("client_a");
		my $cb_player   = $client_b->create_player("client_b");
		sleep(PROP_TIME);
		
		$host->send_message($host_player, $ca_player, "myrmecophilous");
		$host->send_message($host_player, $cb_player, "indigestibly");
		sleep(PROP_TIME);
		
		$client_b->exit();
		$client_a->exit();
		$host->exit();
		
		my @host_messages = $host->messages();
		my @ca_messages   = $client_a->messages();
		my @cb_messages   = $client_b->messages();
		
		cmp_deeply(\@host_messages, []);
		
		cmp_deeply(\@ca_messages, [
			{
				from    => $host_player,
				to      => $ca_player,
				message => "myrmecophilous",
			},
		]);
		
		cmp_deeply(\@cb_messages, [
			{
				from    => $host_player,
				to      => $cb_player,
				message => "indigestibly",
			},
		]);
	};
	
	they "can send messages to the host" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		my $host_player = $host->create_player("host");
		my $ca_player   = $client_a->create_player("client_a");
		my $cb_player   = $client_b->create_player("client_b");
		sleep(PROP_TIME);
		
		$client_a->send_message($ca_player, $host_player, "unvivid");
		$client_b->send_message($cb_player, $host_player, "matrilateral");
		sleep(PROP_TIME);
		
		$client_b->exit();
		$client_a->exit();
		$host->exit();
		
		my @host_messages = $host->messages();
		my @ca_messages   = $client_a->messages();
		my @cb_messages   = $client_b->messages();
		
		cmp_deeply(\@host_messages, [
			{
				from    => $ca_player,
				to      => $host_player,
				message => "unvivid",
			},
			{
				from    => $cb_player,
				to      => $host_player,
				message => "matrilateral",
			},
		]);
		
		cmp_deeply(\@ca_messages, []);
		cmp_deeply(\@cb_messages, []);
	};
	
	they "can send messages to each other" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		my $host_player = $host->create_player("host");
		my $ca_player   = $client_a->create_player("client_a");
		my $cb_player   = $client_b->create_player("client_b");
		sleep(PROP_TIME);
		
		$client_a->send_message($ca_player, $cb_player, "postpuberty");
		$client_b->send_message($cb_player, $ca_player, "veridic");
		sleep(PROP_TIME);
		
		$client_b->exit();
		$client_a->exit();
		$host->exit();
		
		my @host_messages = $host->messages();
		my @ca_messages   = $client_a->messages();
		my @cb_messages   = $client_b->messages();
		
		cmp_deeply(\@host_messages, []);
		
		cmp_deeply(\@ca_messages, [
			{
				from    => $cb_player,
				to      => $ca_player,
				message => "veridic",
			},
		]);
		
		cmp_deeply(\@cb_messages, [
			{
				from    => $ca_player,
				to      => $cb_player,
				message => "postpuberty",
			},
		]);
	};
	
	they "can send a message to all players" => sub
	{
		my ($host, $client_a, $client_b) = $multi_client_common_init->();
		
		my $host_player = $host->create_player("host");
		my $ca_player   = $client_a->create_player("client_a");
		my $cb_player   = $client_b->create_player("client_b");
		sleep(PROP_TIME);
		
		$client_a->send_message($ca_player, DPID_ALLPLAYERS, "kendrew");
		$client_b->send_message($cb_player, DPID_ALLPLAYERS, "derivation");
		sleep(PROP_TIME);
		
		$client_b->exit();
		$client_a->exit();
		$host->exit();
		
		my @host_messages = $host->messages();
		my @ca_messages   = $client_a->messages();
		my @cb_messages   = $client_b->messages();
		
		cmp_deeply(\@host_messages, [
			{
				from    => $ca_player,
				to      => $host_player,
				message => "kendrew",
			},
			{
				from    => $cb_player,
				to      => $host_player,
				message => "derivation",
			},
		]);
		
		cmp_deeply(\@ca_messages, [
			{
				from    => $cb_player,
				to      => $ca_player,
				message => "derivation",
			},
		]);
		
		cmp_deeply(\@cb_messages, [
			{
				from    => $ca_player,
				to      => $cb_player,
				message => "kendrew",
			},
		]);
	};
};

runtests unless caller;
