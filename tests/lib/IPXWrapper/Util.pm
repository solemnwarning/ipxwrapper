# IPXWrapper test suite
# Copyright (C) 2014-2017 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::Util;

use Exporter qw(import);

our @EXPORT = qw(
	run_remote_cmd
	
	reg_set_dword
	reg_set_addr
	reg_delete_key
	reg_delete_value
	
	send_ipx_over_udp
	send_ipx_packet_ethernet
	send_ipx_packet_novell
	send_ipx_packet_llc
	
	cmp_hashes_partial
	
	getsockopt_interfaces
);

use Test::Spec;
use Data::Dumper;
use IO::Socket::INET;
use IPC::Run;
use Net::Libdnet::Eth;
use NetPacket::IPX;
use NetPacket::IPXWrapper;

sub run_remote_cmd
{
	my ($host_ip, $exe_name, @exe_args) = @_;
	
	my @command = ("ssh", $host_ip, $exe_name, @exe_args);
	note(join(" ", @command));
	
	my $output = "";
	my $ok = IPC::Run::run(\@command, ">&" => \$output);
	
	# Oh line endings, how do I hate thee? Let me count the ways.
	$output =~ s/\r//g;
	
	die("Failure running $exe_name:\n$output")
		unless($ok);
	
	return $output;
}

sub reg_set_dword
{
	my ($host_ip, $key, $value, $data) = @_;
	
	run_remote_cmd($host_ip, "REG", "ADD", $key, "/v", $value, "/t", "REG_DWORD", "/d", $data, "/f");
}

sub reg_set_addr
{
	my ($host_ip, $key, $value, $data) = @_;
	
	$data =~ s/://g;
	run_remote_cmd($host_ip, "REG", "ADD", $key, "/v", $value, "/t", "REG_BINARY", "/d", $data, "/f");
}

sub reg_delete_key
{
	my ($host_ip, $key) = @_;
	
	# Attempting to delete a key which doesn't exist is considered to be an
	# error, so we touch the key beforehand.
	
	run_remote_cmd($host_ip, "REG", "ADD",    $key, "/f");
	run_remote_cmd($host_ip, "REG", "DELETE", $key, "/f");
}

sub reg_delete_value
{
	my ($host_ip, $key, $value) = @_;
	
	run_remote_cmd($host_ip, "REG", "ADD",    $key, "/v", $value, "/t", "REG_SZ", "/f");
	run_remote_cmd($host_ip, "REG", "DELETE", $key, "/v", $value, "/f");
}

sub send_ipx_over_udp
{
	my (%options) = @_;
	
	my $packet = NetPacket::IPXWrapper->new(%options);
	
	my $sock = IO::Socket::INET->new(
		Proto     => "udp",
		ReuseAddr => 1,
		Broadcast => 1,
		LocalAddr => $options{src_ip},
		(defined($options{src_port})
			? (LocalPort => $options{src_port})
			: ()),
		PeerAddr  => $options{dest_ip},
		PeerPort  => $options{dest_port},
	) or die("Can't create socket: $!");
	
	$sock->send($packet->encode())
		or die("Can't send data: $!");
}

sub _send_ethernet_frame
{
	my ($dev, $dest_mac, $src_mac, $type, $data) = @_;
	
	my $frame = pack("C6 C6 n",
		(map { hex($_) } split(m/:/, $dest_mac)),
		(map { hex($_) } split(m/:/, $src_mac)),
		$type).$data;
	
	my $eth = Net::Libdnet::Eth->new(device => $dev)
		or die("Couldn't open device $dev");
	
	$eth->send($frame)
		or die("Couldn't transmit frame on device $dev");
}

sub send_ipx_packet_ethernet
{
	my ($dev, %options) = @_;
	
	my $packet = NetPacket::IPX->new(%options);
	
	_send_ethernet_frame($dev,
		$packet->{dest_node}, $packet->{src_node}, 0x8137,
		$packet->encode());
}

sub send_ipx_packet_novell
{
	my ($dev, %options) = @_;
	
	my $packet     = NetPacket::IPX->new(%options);
	my $enc_packet = $packet->encode();
	
	_send_ethernet_frame($dev,
		$packet->{dest_node}, $packet->{src_node}, length($enc_packet),
		$enc_packet);
}

sub send_ipx_packet_llc
{
	my ($dev, %options) = @_;
	
	my $packet     = NetPacket::IPX->new(%options);
	my $enc_packet = $packet->encode();
	
	# Prefix IPX packet with LLC header
	$enc_packet = pack("C3", 0xE0, 0xE0, 0x03).$enc_packet;
	
	_send_ethernet_frame($dev,
		$packet->{dest_node}, $packet->{src_node}, length($enc_packet),
		$enc_packet);
}

sub cmp_hashes_partial
{
	my ($got, $expect) = @_;
	
	my %missing = map { $_ => $expect->[$_] } (0 .. $#{$expect});
	my @extra   = ();
	
	HASH: foreach my $hash(@$got)
	{
		foreach my $key(keys(%missing))
		{
			next if(grep { $hash->{$_} ne $missing{$key}->{$_} }
				keys(%{ $missing{$key} }));
			
			delete $missing{$key};
			next HASH;
		}
		
		push(@extra, $hash);
	}
	
	my $ok = ok(!@extra && !%missing);
	unless($ok)
	{
		diag("Got: ".Dumper($got));
		diag("Expect: ".Dumper($expect));
	}
	
	return $ok;
}

sub getsockopt_interfaces
{
	my ($host_ip) = @_;
	
	my $output = run_remote_cmd($host_ip, "Z:\\tools\\list-interfaces.exe");
	
	my @addrs = ();
	foreach my $line(split(m/[\r\n]+/, $output))
	{
		if($line =~ m/^netnum = (.+), nodenum = (.+), maxpkt = (\d+)$/)
		{
			push(@addrs, { net => $1, node => $2, maxpkt => $3 });
		}
	}
	
	return @addrs;
}

1;
