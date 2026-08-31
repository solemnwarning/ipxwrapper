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

use FindBin;
use lib "$FindBin::Bin/lib/";

use IPXWrapper::Capture::IPXNovell;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);
our ($net_a_bcast, $net_b_bcast);

require "$FindBin::Bin/spx.pm";

our $spx_send_func;
our $spx_capture_class;

describe "IPXWrapper using Novell Ethernet encapsulation" => sub
{
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", 1);
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "frame_type", 2);
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "spx_retransmit_delay", 3000);
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:00");
		
		$spx_capture_class = "IPXWrapper::Capture::IPXNovell";
		$spx_send_func     = \&send_spx_packet_novell;
	};
	
	it_should_behave_like "spx protocol tests";
	it_should_behave_like "spx self tests";
};

runtests unless caller;
