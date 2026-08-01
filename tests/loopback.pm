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

use IPXWrapper::Tool::IPXISR;
use IPXWrapper::Util;

our $remote_ip_a;

our $loopback_bind_net;
our $loopback_bind_node;

shared_examples_for "ipx loopback packet delivery" => sub
{
	it "receives unicast packets sent from the same socket" => sub
	{
		my $isr = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		note("Socket bound to ".$isr->net()."/".$isr->node()."/".$isr->socket());
		
		$isr->send($isr->net(), $isr->node(), $isr->socket(), "fierce");
		
		sleep(1);
		
		my @packets = $isr->kill_and_read();
		
		cmp_hashes_partial(\@packets, [
			{
				sock_idx    => 0,
				src_net     => $isr->net(),
				src_node    => $isr->node(),
				src_socket  => $isr->socket(),
				
				data => "fierce",
			},
		]);
	};
	
	it "receives unicast packets sent from another socket in the same process" => sub
	{
		my $isr = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0",
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		note("Socket 0 bound to ".$isr->net()."/".$isr->node()."/".$isr->socket());
		note("Socket 1 bound to ".$isr->net(1)."/".$isr->node(1)."/".$isr->socket(1));
		
		$isr->send($isr->net(), $isr->node(), $isr->socket(), "puffy", 1);
		
		sleep(1);
		
		my @packets = $isr->kill_and_read();
		
		cmp_hashes_partial(\@packets, [
			{
				sock_idx    => 0,
				src_net     => $isr->net(1),
				src_node    => $isr->node(1),
				src_socket  => $isr->socket(1),
				
				data => "puffy",
			},
		]);
	};
	
	it "receives broadcast packets sent from the same socket" => sub
	{
		my $isr = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		note("Socket bound to ".$isr->net()."/".$isr->node()."/".$isr->socket());
		
		$isr->send($isr->net(), "FF:FF:FF:FF:FF:FF", $isr->socket(), "ripe");
		
		sleep(1);
		
		my @packets = $isr->kill_and_read();
		
		cmp_hashes_partial(\@packets, [
			{
				sock_idx    => 0,
				src_net     => $isr->net(),
				src_node    => $isr->node(),
				src_socket  => $isr->socket(),
				
				data => "ripe",
			},
		]);
	};
	
	it "receives broadcast packets sent from another socket in the same process" => sub
	{
		my $isr = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0",
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		note("Socket 0 bound to ".$isr->net()."/".$isr->node()."/".$isr->socket());
		note("Socket 1 bound to ".$isr->net(1)."/".$isr->node(1)."/".$isr->socket(1));
		
		$isr->send($isr->net(), "FF:FF:FF:FF:FF:FF", $isr->socket(), "magical", 1);
		
		sleep(1);
		
		my @packets = $isr->kill_and_read();
		
		cmp_hashes_partial(\@packets, [
			{
				sock_idx    => 0,
				src_net     => $isr->net(1),
				src_node    => $isr->node(1),
				src_socket  => $isr->socket(1),
				
				data => "magical",
			},
		]);
	};
	
	it "receives unicast packets sent by another process on the same machine" => sub
	{
		my $isrA = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		my $isrB = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		note("Socket 0 bound to ".$isrA->net()."/".$isrA->node()."/".$isrA->socket());
		note("Socket 1 bound to ".$isrB->net()."/".$isrB->node()."/".$isrB->socket());
		
		$isrA->send($isrB->net(), $isrB->node(), $isrB->socket(), "hammer");
		
		sleep(1);
		
		my @packets_a = $isrA->kill_and_read();
		my @packets_b = $isrB->kill_and_read();
		
		cmp_hashes_partial(\@packets_a, []);
		
		cmp_hashes_partial(\@packets_b, [
			{
				src_net     => $isrA->net(),
				src_node    => $isrA->node(),
				src_socket  => $isrA->socket(),
				
				data => "hammer",
			},
		]);
	};
	
	it "receives broadcast packets sent by another process on the same machine" => sub
	{
		my $isrA = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		my $isrB = IPXWrapper::Tool::IPXISR->new(
			$remote_ip_a,
			"-b", $loopback_bind_net, $loopback_bind_node, "0");
		
		note("Socket 0 bound to ".$isrA->net()."/".$isrA->node()."/".$isrA->socket());
		note("Socket 1 bound to ".$isrB->net()."/".$isrB->node()."/".$isrB->socket());
		
		$isrA->send($isrB->net(), "FF:FF:FF:FF:FF:FF", $isrB->socket(), "degree");
		
		sleep(1);
		
		my @packets_a = $isrA->kill_and_read();
		my @packets_b = $isrB->kill_and_read();
		
		cmp_hashes_partial(\@packets_a, []);
		
		cmp_hashes_partial(\@packets_b, [
			{
				src_net     => $isrA->net(),
				src_node    => $isrA->node(),
				src_socket  => $isrA->socket(),
				
				data => "degree",
			},
		]);
	};
};

1;
