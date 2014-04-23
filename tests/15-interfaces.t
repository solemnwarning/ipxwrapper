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

use IPXWrapper::Tool::Bind;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);

use constant {
	IP_MAX_DATA_SIZE    => 8192,
	ETHER_MAX_DATA_SIZE => 1470,
};

my @expected_addrs;

shared_examples_for "getsockopt" => sub
{
	it "returns correct addresses" => sub
	{
		my @addrs = getsockopt_interfaces($remote_ip_a);
		cmp_hashes_partial(\@addrs, \@expected_addrs);
	};
	
	it "returns correct IPX_MAX_ADAPTER_NUM" => sub
	{
		my @addrs = getsockopt_interfaces($remote_ip_a);
		
		my $output = run_remote_cmd($remote_ip_a, "Z:\\tools\\list-interfaces.exe");
		my ($got_num) = ($output =~ m/^IPX_MAX_ADAPTER_NUM = (\d+)$/m);
		
		is($got_num, (scalar @addrs));
	};
	
	it "returns the configured primary interface first" => sub
	{
		reg_set_addr($remote_ip_a, "HKCU\\Software\\IPXWrapper", "primary", $remote_mac_a);
		my $first_a = get_first_addr_node() // "";
		
		reg_set_addr($remote_ip_a, "HKCU\\Software\\IPXWrapper", "primary", $remote_mac_b);
		my $first_b = get_first_addr_node() // "";
		
		ok($first_a eq $remote_mac_a && $first_b eq $remote_mac_b);
	};
};

describe "IPXWrapper" => sub
{
	describe "using IP encapsulation" => sub
	{
		before all => sub
		{
			reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
			reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\00:00:00:00:00:00", "net", "00:00:00:01");
			reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
			reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:02");
		};
		
		describe "with the wildcard interface disabled" => sub
		{
			before all => sub
			{
				reg_set_dword($remote_ip_a, "HKCU\\Software\\IPXWrapper\\00:00:00:00:00:00", "enabled", 0);
				
				@expected_addrs = (
					{
						net    => "00:00:00:01",
						node   => $remote_mac_a,
						maxpkt => IP_MAX_DATA_SIZE,
					},
					
					{
						net    => "00:00:00:02",
						node   => $remote_mac_b,
						maxpkt => IP_MAX_DATA_SIZE,
					},
				);
			};
			
			describe "getsockopt" => sub
			{
				it_should_behave_like "getsockopt";
			};
		};
		
		describe "with the wildcard interface enabled" => sub
		{
			before all => sub
			{
				reg_set_dword($remote_ip_a, "HKCU\\Software\\IPXWrapper\\00:00:00:00:00:00", "enabled", 1);
				
				@expected_addrs = (
					{
						net    => "00:00:00:01",
						maxpkt => IP_MAX_DATA_SIZE,
					},
					
					{
						net    => "00:00:00:01",
						node   => $remote_mac_a,
						maxpkt => IP_MAX_DATA_SIZE,
					},
					
					{
						net    => "00:00:00:02",
						node   => $remote_mac_b,
						maxpkt => IP_MAX_DATA_SIZE,
					},
				);
			};
			
			describe "getsockopt" => sub
			{
				it_should_behave_like "getsockopt";
				
				it "returns the wildcard interface first by default" => sub
				{
					my $wildcard_node = wildcard_node();
					return unless(defined($wildcard_node));
					
					reg_delete_value($remote_ip_a, "HKCU\\Software\\IPXWrapper", "primary");
					is(get_first_addr_node(), $wildcard_node);
				};
			};
			
			it "remembers the wildcard node number" => sub
			{
				my $node_a = wildcard_node();
				my $node_b = wildcard_node();
				
				is($node_a, $node_b);
			};
			
			it "generates a new wildcard node number if the old is deleted" => sub
			{
				my $old_node = wildcard_node();
				return unless(defined($old_node));
				
				reg_delete_value($remote_ip_a, "HKCU\\Software\\IPXWrapper\\00:00:00:00:00:00", "node");
				
				my $new_node = wildcard_node();
				isnt($new_node, $old_node);
			};
		};
	};
	
	describe "using Ethernet encapsulation" => sub
	{
		before all => sub
		{
			reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
			reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", 1);
			reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
			reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:02");
			
			@expected_addrs = (
				{
					net    => "00:00:00:01",
					node   => $remote_mac_a,
					maxpkt => ETHER_MAX_DATA_SIZE,
				},
				
				{
					net    => "00:00:00:02",
					node   => $remote_mac_b,
					maxpkt => ETHER_MAX_DATA_SIZE,
				},
			);
		};
		
		describe "getsockopt" => sub
		{
			it_should_behave_like "getsockopt";
		};
	};
};

sub get_first_addr_node
{
	my ($first) = getsockopt_interfaces($remote_ip_a);
	
	return ($first // {})->{node};
}

sub wildcard_node
{
	my ($wildcard) = grep {
		$_->{node} ne $remote_mac_a && $_->{node} ne $remote_mac_b
	} getsockopt_interfaces($remote_ip_a);
	
	return ($wildcard // {})->{node};
}

runtests unless caller;
