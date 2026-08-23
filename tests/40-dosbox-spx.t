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

use FindBin;
use lib "$FindBin::Bin/lib/";

use IPXWrapper::DOSBoxServer;
use IPXWrapper::Util;

require "$FindBin::Bin/config.pm";

our ($local_dev_a, $local_mac_a, $local_ip_a);
our ($local_dev_b, $local_mac_b, $local_ip_b);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);
our ($net_a_bcast, $net_b_bcast);
our ($dosbox_port);

require "$FindBin::Bin/spx.pm";

describe "IPXWrapper using DOSBox UDP encapsulation" => sub
{
	my $dosbox_server;
	
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", ENCAP_TYPE_DOSBOX);
		reg_set_string($remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_addr", "dosbox-ipv4.com");
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_port", $dosbox_port);
		reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "spx_retransmit_delay", 3000);
		
		$dosbox_server = IPXWrapper::DOSBoxServer->new($dosbox_port);
	};
	
	after all => sub
	{
		$dosbox_server = undef;
	};
	
	it_should_behave_like "spx self tests";
};

runtests unless caller;
