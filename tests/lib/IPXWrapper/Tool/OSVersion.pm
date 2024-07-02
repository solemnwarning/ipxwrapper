# IPXWrapper test suite
# Copyright (C) 2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::Tool::OSVersion;

use IPC::Open3;
use Test::Spec;

my %os_version_cache = ();

sub get
{
	my ($class, $host) = @_;
	
	if(defined $os_version_cache{$host})
	{
		return $os_version_cache{$host};
	}
	
	my @command = ("ssh", $host, "Z:\\tools\\osversion.exe");
	
	note(join(" ", @command));
	
	# No need for error checking here - open3 throws on failure.
	my $pid = open3(my $in, my $out, undef, @command);
	
	my $output = do {
		local $/;
		<$out>;
	};
	
	if($output =~ m/^(\d+).(\d+)\r?\n(\d+)\r?\n(\w+)\r?\n(.*)\r?\n$/)
	{
		my $self = bless({
			major => $1,
			minor => $2,
			build => $3,
			platform => $4,
			extra => $5,
		}, $class);
		
		$os_version_cache{$host} = $self;
		
		return $self;
	}
	else{
		die("Didn't get expected output from osversion.exe:\n$output");
	}
}

sub major
{
	my ($self) = @_;
	return $self->{major};
}

sub minor
{
	my ($self) = @_;
	return $self->{minor};
}

sub build
{
	my ($self) = @_;
	return $self->{build};
}

sub platform
{
	my ($self) = @_;
	return $self->{platform};
}

sub platform_is_win9x
{
	my ($self) = @_;
	return $self->{platform} eq "VER_PLATFORM_WIN32_WINDOWS";
}

sub platform_is_winnt
{
	my ($self) = @_;
	return $self->{platform} eq "VER_PLATFORM_WIN32_NT";
}

1;
