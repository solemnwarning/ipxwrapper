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

# NOTE: These constants are the values used on Windows, not the host.
use constant {
	SOCK_STREAM    => 1,
	SOCK_DGRAM     => 2,
	SOCK_SEQPACKET => 5,
	NSPROTO_IPX    => 1000,
	NSPROTO_SPX    => 1256,
	NSPROTO_SPXII  => 1257,
	WSAEADDRINUSE  => 10048,
};

my ($bind_type_a, $bind_proto_a);
my ($bind_type_b, $bind_proto_b);

shared_examples_for "bind address selection/reuse for one protocol" => sub
{
	it "allows binding to a specific address" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:01", $remote_mac_a, "1",
			$bind_type_b, $bind_proto_b, "00:00:00:02", $remote_mac_b, "2",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ net => "00:00:00:02", node => $remote_mac_b, sock => 2 },
		]);
	};
	
	it "allows binding to a specific address without specifying network" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			$bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "2",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ net => "00:00:00:02", node => $remote_mac_b, sock => 2 },
		]);
	};
	
	it "binds to the configured primary interface by default" => sub
	{
		reg_set_addr($remote_ip_a, "HKCU\\Software\\IPXWrapper", "primary", $remote_mac_a);
		my @result_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:00", "00:00:00:00:00:00", "1")
			->result();
		
		reg_set_addr($remote_ip_a, "HKCU\\Software\\IPXWrapper", "primary", $remote_mac_b);
		my @result_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_b, $bind_proto_b, "00:00:00:00", "00:00:00:00:00:00", "1")
			->result();
		
		cmp_deeply(\@result_a, [ { net => "00:00:00:01", node => $remote_mac_a, sock => 1 } ]);
		cmp_deeply(\@result_b, [ { net => "00:00:00:02", node => $remote_mac_b, sock => 1 } ]);
	};
	
	it "assigns a unique (within a process) socket number if 0 is given" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:01", $remote_mac_a, "0",
			$bind_type_b, $bind_proto_b, "00:00:00:02", $remote_mac_b, "0")
			->result();
		
		like($result[0]->{sock}, qr/^[1-9]\d*$/);
		like($result[1]->{sock}, qr/^[1-9]\d*$/);
		isnt($result[0]->{sock}, $result[1]->{sock});
	};
	
	it "assigns a unique (between processes) socket number if 0 is given" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:01", $remote_mac_a, "0");
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_b, $bind_proto_b, "00:00:00:02", $remote_mac_b, "0");
		
		my @result_a = $bind_a->result();
		my @result_b = $bind_b->result();
		
		like($result_a[0]->{sock}, qr/^[1-9]\d*$/);
		like($result_b[0]->{sock}, qr/^[1-9]\d*$/);
		isnt($result_a[0]->{sock}, $result_b[0]->{sock});
	};
	
	it "doesn't allow socket reuse (within a process) if neither socket has SO_REUSEADDR" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			$bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ errno => WSAEADDRINUSE },
		]);
	};
	
	it "doesn't allow socket reuse (within a process) if only the first socket has SO_REUSEADDR" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			      $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ errno => WSAEADDRINUSE },
		]);
	};
	
	it "allows socket reuse (within a process) if only the second socket has SO_REUSEADDR" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			      $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ net => "00:00:00:02", node => $remote_mac_b, sock => 1 },
		]);
	};
	
	it "allows address reuse (within a process) if only the second socket has SO_REUSEADDR" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			      $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_a, "1",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
	};
	
	it "allows socket reuse (within a process) if both sockets have SO_REUSEADDR" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ net => "00:00:00:02", node => $remote_mac_b, sock => 1 },
		]);
	};
	
	it "allows address reuse (within a process) if both sockets have SO_REUSEADDR" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_a, "1",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
	};
	
	it "doesn't allow socket reuse (between processes) if neither socket has SO_REUSEADDR" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ errno => WSAEADDRINUSE },
		]);
	};
	
	it "doesn't allow socket reuse (between processes) if only the first socket has SO_REUSEADDR" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ errno => WSAEADDRINUSE },
		]);
	};
	
	it "allows socket reuse (between processes) if only the second socket has SO_REUSEADDR" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ net => "00:00:00:02", node => $remote_mac_b, sock => 1 },
		]);
	};
	
	it "allows address reuse (between processes) if only the second socket has SO_REUSEADDR" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_a, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
	};
	
	it "allows socket reuse (between processes) if both sockets have SO_REUSEADDR" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_b, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ net => "00:00:00:02", node => $remote_mac_b, sock => 1 },
		]);
	};
	
	it "allows address reuse (between processes) if both sockets have SO_REUSEADDR" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-r", $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_a, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
	};
	
	it "allows address reuse (within a process) if first socket is closed" => sub
	{
		my @result = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-c", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			      $bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_a, "1",
		)->result();
		
		cmp_deeply(\@result, [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
	};
	
	it "allows address reuse (between processes) if first socket is closed" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			"-c", $bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_a, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
	};
	
	it "allows address reuse (between processes) if first process exits uncleanly" => sub
	{
		my $bind_a = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_a, $bind_proto_a, "00:00:00:00", $remote_mac_a, "1",
			"-e", # Forces early _exit()
		);
		
		my $bind_b = IPXWrapper::Tool::Bind->new($remote_ip_a,
			$bind_type_b, $bind_proto_b, "00:00:00:00", $remote_mac_a, "1",
		);
		
		cmp_deeply([ $bind_a->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
		
		cmp_deeply([ $bind_b->result() ], [
			{ net => "00:00:00:01", node => $remote_mac_a, sock => 1 },
		]);
	};
};

describe "bind" => sub
{
	before all => sub
	{
		reg_delete_key($remote_ip_a, "HKCU\\Software\\IPXWrapper");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_a", "net", "00:00:00:01");
		reg_set_addr(  $remote_ip_a, "HKCU\\Software\\IPXWrapper\\$remote_mac_b", "net", "00:00:00:02");
	};
	
	describe "on IPX sockets" => sub
	{
		before each => sub
		{
			$bind_type_a  = $bind_type_b  = SOCK_DGRAM;
			$bind_proto_a = $bind_proto_b = NSPROTO_IPX;
		};
		
		it_should_behave_like "bind address selection/reuse for one protocol";
	};
	
	describe "on SPX sockets" => sub
	{
		before each => sub
		{
			$bind_type_a  = $bind_type_b  = SOCK_STREAM;
			$bind_proto_a = $bind_proto_b = NSPROTO_SPX;
		};
		
		it_should_behave_like "bind address selection/reuse for one protocol";
	};
	
	describe "on SPXII sockets" => sub
	{
		before each => sub
		{
			$bind_type_a  = $bind_type_b  = SOCK_STREAM;
			$bind_proto_a = $bind_proto_b = NSPROTO_SPXII;
		};
		
		it_should_behave_like "bind address selection/reuse for one protocol";
	};
	
	describe "on an IPX and an SPX socket" => sub
	{
		before each => sub
		{
			$bind_type_a  = SOCK_DGRAM;
			$bind_proto_a = NSPROTO_IPX;
			
			$bind_type_b  = SOCK_STREAM;
			$bind_proto_b = NSPROTO_SPX;
		};
		
		it_should_behave_like "bind address selection/reuse for one protocol";
	};
	
	describe "on an IPX and an SPXII socket" => sub
	{
		before each => sub
		{
			$bind_type_a  = SOCK_DGRAM;
			$bind_proto_a = NSPROTO_IPX;
			
			$bind_type_b  = SOCK_STREAM;
			$bind_proto_b = NSPROTO_SPXII;
		};
		
		it_should_behave_like "bind address selection/reuse for one protocol";
	};
	
	describe "on an SPX and an SPXII socket" => sub
	{
		before each => sub
		{
			$bind_type_a  = SOCK_STREAM;
			$bind_proto_a = NSPROTO_SPX;
			
			$bind_type_b  = SOCK_STREAM;
			$bind_proto_b = NSPROTO_SPXII;
		};
		
		it_should_behave_like "bind address selection/reuse for one protocol";
	};
};

runtests unless caller;
