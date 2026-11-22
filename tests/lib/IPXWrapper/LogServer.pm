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

package IPXWrapper::LogServer;

use POSIX qw(:signal_h);

use IO::Select;
use IO::Socket::INET;

sub new
{
	my ($class) = @_;
	
	my $listener = IO::Socket::INET->new(
		Listen    => 10,
		LocalAddr => "0.0.0.0",
		LocalPort => 0,
		Proto     => "tcp");
	
	my $pid = fork() // die "fork: $!";
	
	if($pid == 0)
	{
		my $logbuf  = "";
		my @clients = ();
		
		$SIG{TERM} = sub
		{
			exit(0);
		};
		
		$SIG{USR1} = sub
		{
			print STDERR $logbuf;
			$logbuf = "";
			
			exit(0);
		};
		
		my $select = IO::Select->new();
		$select->add($listener);
		
		while(1)
		{
			my @ready = $select->can_read();
			
			foreach my $sock(@ready)
			{
				if($sock eq $listener)
				{
					my $new_sock = $listener->accept();
					
					push(@clients, { sock => $new_sock, buf => "" });
					$select->add($new_sock);
				}
				else{
					my ($client) = grep { $_->{sock} eq $sock } @clients;
					
					my $buf = "";
					if(defined($sock->recv($buf, 1024)) && length($buf) > 0)
					{
						$client->{buf} .= $buf;
						
						if($client->{buf} =~ m/\n/)
						{
							my ($buf1, $buf2) = ($client->{buf} =~ m/^(.*\n)([^\n]*)$/s);
							
							# print STDERR "> $buf1";
							
							$logbuf .= $buf1;
							$client->{buf} = $buf2;
						}
					}
					else{
						$select->remove($sock);
						@clients = grep { $_->{sock} ne $sock } @clients;
					}
				}
			}
		}
	}
	
	return bless({ listener => $listener, pid => $pid }, $class);
}

sub port
{
	my ($self) = @_;
	return $self->{listener}->sockport();
}

sub discard
{
	my ($self) = @_;
	
	if(defined $self->{pid})
	{
		kill(SIGTERM, $self->{pid});
		waitpid($self->{pid}, 0);
		
		delete $self->{pid};
	}
}

sub DESTROY
{
	my ($self) = @_;
	
	if(defined $self->{pid})
	{
		kill(SIGUSR1, $self->{pid});
		waitpid($self->{pid}, 0);
		
		delete $self->{pid};
	}
}

1;
