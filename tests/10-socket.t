# IPXWrapper test suite
# Copyright (C) 2014-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

our ($local_ip_a);
our ($remote_mac_a, $remote_ip_a);
our ($remote_mac_b, $remote_ip_b);
our ($dosbox_port);

# NOTE: These constants are the values used on Windows, not the host.
use constant {
	AF_IPX         => 6,
	SOCK_STREAM    => 1,
	SOCK_DGRAM     => 2,
	SOCK_SEQPACKET => 5,
	NSPROTO_IPX    => 1000,
	NSPROTO_SPX    => 1256,
	NSPROTO_SPXII  => 1257,
};

shared_examples_for "socket initialisation" => sub
{
	foreach my $ptype(0, 1, 100, 255)
	{
		it "socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX + $ptype) creates a socket with IPX_PTYPE $ptype" => sub
		{
			my $output = run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\socket.exe",
				AF_IPX, SOCK_DGRAM, NSPROTO_IPX + $ptype,
			);
			
			like($output, qr/^socket: \d+$/m);
			like($output, qr/^IPX_PTYPE: \Q$ptype\E$/m);
		};
	}
	
	# No SOCK_SEQPACKET support (yet), so make sure any attempts to create
	# such a socket fail.
	
	it "socket(AF_IPX, SOCK_SEQPACKET, NSPROTO_SPX) fails" => sub
	{
		my $output = run_remote_cmd(
			$remote_ip_a, "Z:\\tools\\socket.exe",
			AF_IPX, SOCK_SEQPACKET, NSPROTO_SPX,
		);
		
		like($output, qr/^socket: -1$/m);
	};
	
	it "socket(AF_IPX, SOCK_SEQPACKET, NSPROTO_SPXII) fails" => sub
	{
		my $output = run_remote_cmd(
			$remote_ip_a, "Z:\\tools\\socket.exe",
			AF_IPX, SOCK_SEQPACKET, NSPROTO_SPXII,
		);
		
		like($output, qr/^socket: -1$/m);
	};
};

describe "IPXWrapper" => sub
{
	describe "using IP encapsulation" => sub
	{
		before all => sub
		{
			reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		};
		
		it_should_behave_like "socket initialisation";
		
		it "socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX) succeeds" => sub
		{
			my $output = run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\socket.exe",
				AF_IPX, SOCK_STREAM, NSPROTO_SPX,
			);
			
			like($output, qr/^socket: \d+$/m);
		};
		
		it "socket(AF_IPX, SOCK_STREAM, NSPROTO_SPXII) succeeds" => sub
		{
			my $output = run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\socket.exe",
				AF_IPX, SOCK_STREAM, NSPROTO_SPXII,
			);
			
			like($output, qr/^socket: \d+$/m);
		};
	};
	
	describe "using Ethernet encapsulation" => sub
	{
		before all => sub
		{
			reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
			reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", 1);
		};
		
		it_should_behave_like "socket initialisation";
		
		it "socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX) fails" => sub
		{
			my $output = run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\socket.exe",
				AF_IPX, SOCK_STREAM, NSPROTO_SPX,
			);
			
			like($output, qr/^socket: -1$/m);
		};
		
		it "socket(AF_IPX, SOCK_STREAM, NSPROTO_SPXII) fails" => sub
		{
			my $output = run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\socket.exe",
				AF_IPX, SOCK_STREAM, NSPROTO_SPXII,
			);
			
			like($output, qr/^socket: -1$/m);
		};
	};
	
	describe "using a DOSBox server" => sub
	{
		my $dosbox_server;
		
		before all => sub
		{
			reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
			reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "use_pcap", ENCAP_TYPE_DOSBOX);
			reg_set_string($remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_addr", $local_ip_a);
			reg_set_dword( $remote_ip_a, "HKCU\\Software\\IPXWrapper", "dosbox_server_port", $dosbox_port);
			
			# $dosbox_server = IPXWrapper::Tool::DOSBoxServer->new($dosbox_port);
		};
		
		after all => sub
		{
			$dosbox_server = undef;
		};
		
		it_should_behave_like "socket initialisation";
		
		it "socket(AF_IPX, SOCK_STREAM, NSPROTO_SPX) fails" => sub
		{
			my $output = run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\socket.exe",
				AF_IPX, SOCK_STREAM, NSPROTO_SPX,
			);
			
			like($output, qr/^socket: -1$/m);
		};
		
		it "socket(AF_IPX, SOCK_STREAM, NSPROTO_SPXII) fails" => sub
		{
			my $output = run_remote_cmd(
				$remote_ip_a, "Z:\\tools\\socket.exe",
				AF_IPX, SOCK_STREAM, NSPROTO_SPXII,
			);
			
			like($output, qr/^socket: -1$/m);
		};
	};
};

runtests unless caller;
