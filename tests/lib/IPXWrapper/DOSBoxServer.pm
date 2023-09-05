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
	my $pid = open3(my $in, my $out, undef, @command);
	
	my $self = bless({
		xvfb_run_pid => $pid,
	}, $class);
	
	my $output = "";
	
	while(defined(my $line = <$out>))
	{
		$output .= $line;
		
		$line =~ s/[\r\n]//g;
		
		if($line =~ m/^IPX: Connected to server\./)
		{
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
			
			$self->{dosbox_pid} = $find_dosbox->($find_dosbox, $pid)
				// die "Couldn't find DOSBox PID";
			
			return $self;
		}
	}
	
	die("Didn't get expected output from xvfb-run/dosbox:\n$output");
}

sub DESTROY
{
	my ($self) = @_;
	
	# Kill DOSBox, then wait for xvfb-run to clean up.
	kill(SIGTERM, $self->{dosbox_pid});
	waitpid($self->{xvfb_run_pid}, 0);
}

1;
