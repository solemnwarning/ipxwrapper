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
use IPXWrapper::SPX;
use IPXWrapper::Tool::Generic;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);
our ($net_a_bcast, $net_b_bcast);

use constant {
	UDP_BCAST_PORT => 54792,
	IPX_CONNECT_TRIES => 3,
};

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
	
	# TODO: Properly test spxinit step.
	
	it "can exchange data between SPX sockets" => sub
	{
		my $listener = IPXWrapper::Tool::Generic->new(
			$remote_ip_a, "Z:\\tools\\spx-server.exe",
			"00:00:00:01", $remote_mac_a, 2222);
		
		my $output = run_remote_cmd(
			$remote_ip_a, "Z:\\tools\\spx-client.exe",
			"00:00:00:01", $remote_mac_a, "2222",
		);
		
		like($output, qr/^success$/m);
	};
};

runtests unless caller;
