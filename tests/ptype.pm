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

use IPXWrapper::Tool::IPXRecv;
use IPXWrapper::Util;

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);

our $ptype_send_func;
our $ptype_capture_class;

shared_examples_for "ipx packet type handling" => sub
{
	it "transmits IPX packets with type 0 by default" => sub
	{
		my $capture = $ptype_capture_class->new($local_dev_a);
		
		run_remote_cmd(
			$remote_ip_a, "Z:\\tools\\ipx-send.exe",
			"-d" => "eddic",
			"-h" => $remote_mac_a,
			"00:00:00:01", $local_mac_a, "4444",
		);
		
		sleep(1);
		
		my @packets = $capture->read_available();
		
		cmp_hashes_partial(\@packets, [
			{
				type => 0,
				data => "eddic",
			},
		]);
	};
	
	it "transmits IPX packets with a custom type" => sub
	{
		my $capture = $ptype_capture_class->new($local_dev_a);
		
		run_remote_cmd(
			$remote_ip_a, "Z:\\tools\\ipx-send.exe",
			"-t" => "99",
			"-d" => "unmammalian",
			"-h" => $remote_mac_a,
			"00:00:00:01", $local_mac_a, "4444",
		);
		
		sleep(1);
		
		my @packets = $capture->read_available();
		
		cmp_hashes_partial(\@packets, [
			{
				type => 99,
				data => "unmammalian",
			},
		]);
	};
	
	it "receives IPX packets with any type by default" => sub
	{
		my $capture = IPXWrapper::Tool::IPXRecv->new(
			$remote_ip_a,
			"00:00:00:01", $remote_mac_a, "4444",
		);
		
		$ptype_send_func->(0, "aquaplaning");
		$ptype_send_func->(4, "pavement");
		
		sleep(1);
		
		my @packets = $capture->kill_and_read();
		
		cmp_hashes_partial(\@packets, [
			{
				data => "aquaplaning",
			},
			
			{
				data => "pavement",
			},
		]);
	};
	
	it "receives IPX packets with the requested type" => sub
	{
		my $capture = IPXWrapper::Tool::IPXRecv->new(
			$remote_ip_a,
			"-f" => "4", "00:00:00:01", $remote_mac_a, "4444",
		);
		
		$ptype_send_func->(0, "petrologic");
		$ptype_send_func->(4, "bimester");
		
		sleep(1);
		
		my @packets = $capture->kill_and_read();
		
		cmp_hashes_partial(\@packets, [
			{
				data => "bimester",
			},
		]);
	};
};

1;
