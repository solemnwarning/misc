#!/usr/bin/perl
# Shuts down machine when not in use for a period of time.
# By Daniel Collins. Released to public domain.

use strict;
use warnings;

# --- START OF CONFIGURATION --- #

# Connections to any of the ports listed here will delay the shutdown.
use constant SERVICE_PORTS  => (22);

# Number of seconds to wait before shutting down.
use constant SHUTDOWN_DELAY => 300;

# --- END OF CONFIGURATION --- #

use Sys::Utmp;

my $last_used = time();

while(1)
{
	# Check for logged in users
	
	{
		my $utmp = Sys::Utmp->new();
		
		while(my $utent = $utmp->getutent()) 
		{
			if($utent->user_process())
			{
				$last_used = time();
				last;
			}
		}
		
		$utmp->endutent();
	}
	
	# Check for open TCP connections
	
	if(open(my $tcp, "<", "/proc/net/tcp"))
	{
		process_tcp_file($tcp);
	}
	else{
		die "Couldn't open /proc/net/tcp: $!";
	}
	
	if(open(my $tcp6, "<", "/proc/net/tcp6"))
	{
		process_tcp_file($tcp6);
	}
	
	# Shut down?
	
	if(($last_used + SHUTDOWN_DELAY) < time())
	{
		# Time's up!
		system("shutdown", "-h", "now", "Automatic shutdown - system idle");
	}
	
	sleep(5);
}

sub process_tcp_file
{
	my ($fh) = @_;
	
	while(defined(my $line = <$fh>))
	{
		my $H = qr/[A-Z0-9]+/i;
		my ($local_port, $st) = ($line =~ m/^\s*\S+\s+$H:($H)\s+$H:$H\s+($H)/i);
		
		# Ignore lines we can't parse - should just be the headings and
		# trailing blank line.
		next unless(defined $local_port);
		
		# Ignore listen sockets.
		next if(hex($st) == 0x0A);
		
		if(grep { $local_port == $_ } SERVICE_PORTS)
		{
			$last_used = time();
		}
	}
}
