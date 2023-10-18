# IPXWrapper test suite
# Copyright (C) 2023 Daniel Collins <solemnwarning@solemnwarning.net>
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

package IPXWrapper::DOSBoxServer;

use File::Temp;
use IPC::Open3;
use POSIX qw(:signal_h);
use Proc::ProcessTable;
use Test::Spec;

sub new
{
	my ($class, $port) = @_;
	
	my $self = bless({
		xvfb_run_pid => undef,
		dosbox_pid => undef,
	}, $class);
	
	# For some reason DOSBox sometimes fails to start under Xvfb... so
	# retry a few times if that happens.
	
	for(my $i = 0; $i < 5; ++$i)
	{
		eval {
			$self->_start($port);
		};
		
		if($@ eq "")
		{
			return $self;
		}
		elsif($@ !~ m/Couldn't open X11 display/)
		{
			last;
		}
		
		note("DOSBox startup failed, trying again.");
		note("Error was:\n$@");
		
		$self->_stop();
	}
	
	die $@;
}

sub _start
{
	my ($self, $port) = @_;
	
	my $dosbox_conf = File::Temp->new();
	print {$dosbox_conf} <<EOF;
[ipx]
ipx=true

[autoexec]
ipxnet startserver $port
EOF
	
	my @command = ("xvfb-run", "-a", "-e" => "/dev/stdout",
		"stdbuf", "-i0", "-o0", "-e0",
		"dosbox", "-conf" => "$dosbox_conf");
	
	note(join(" ", @command));
	
	# No need for error checking here - open3 throws on failure.
	$self->{xvfb_run_pid} = open3(my $in, my $out, undef, @command);
	
	my $output = "";
	
	# Read from child until we see what we expected or the pipe gets closed.
	while($output !~ m/^IPX: Connected to server\./m && defined(my $line = <$out>))
	{
		$output .= $line;
	}
	
	# We can't kill DOSBox by sending a signal to xvfb-run since it doesn't
	# catch any signals and just dies, leaving orphan Xvfb and dosbox processes
	# running.
	#
	# So we have to walk the process table and find the dosbox process owned by
	# our xvfb-run process, which we can then directly kill and xvfb-run will
	# clean up the Xvfb instance.
	
	my @procs = @{ Proc::ProcessTable->new()->table() };
	
	my $find_dosbox = sub
	{
		my ($find_dosbox, $ppid) = @_;
		
		foreach my $child(grep { $_->ppid() == $ppid } @procs)
		{
			# if($child->cmdline->[0] eq "dosbox")
			if($child->cmndline =~ m/^dosbox /)
			{
				return $child->pid;
			}
			
			my $dosbox_pid = $find_dosbox->($find_dosbox, $child->pid);
			return $dosbox_pid if(defined $dosbox_pid);
		}
		
		return;
	};
	
	$self->{dosbox_pid} = $find_dosbox->($find_dosbox, $self->{xvfb_run_pid});
	
	if($output =~ m/^IPX: Connected to server\./m)
	{
		die "Couldn't find DOSBox PID - did it crash?" unless(defined $self->{dosbox_pid});
		return;
	}
	
	die "Didn't get expected output from xvfb-run/dosbox:\n$output";
}

sub _stop
{
	my ($self) = @_;
	
	# Kill DOSBox, then wait for xvfb-run to clean up.
	
	kill(SIGTERM, $self->{dosbox_pid}) if(defined $self->{dosbox_pid});
	waitpid($self->{xvfb_run_pid}, 0) if(defined $self->{xvfb_run_pid});
	
	$self->{dosbox_pid} = undef;
	$self->{xvfb_run_pid} = undef;
}

sub DESTROY
{
	my ($self) = @_;
	
	$self->_stop();
}

1;
