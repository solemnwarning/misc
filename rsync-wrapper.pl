#!/usr/bin/env perl
# This script can be used to wrap rsync, providing a write-only backup service
# where the client cannot delete or modify any existing backup.
#
# The script should be invoked using the command="..." feature in your
# authorized_keys file, it takes a single argument which is the directory to
# store any backups under.
#
# The destination directory given to the sender must match the
# $ALLOW_DEST_NAMES_LIKE regex, the backup will be stored under that directory
# under the one passed on the command line.
#
# Example directory structure:
#
# /mnt/backups/host_a/
#     ./filesystem_a/
#         ./YYYY-MM-DD/
#         ./last
#     ./filesystem_b/
#
# .ssh/authorized_keys should contain a line something like this:
#
# command="rsync-wrapper.pl /mnt/backups/host_a/",no-port-forwarding,
#     no-pty,no-X11-forwarding ssh-rsa ...
#
# Host A should create backups with a command something like this:
#
# rsync -a --numeric-ids -e ssh /mnt/fs_a/ backuphost:filesystem_a

use strict;
use warnings;

use File::Path qw(make_path);
use File::Spec;
use POSIX qw(strftime WEXITSTATUS);
use Readonly;

Readonly my $ALLOW_DEST_NAMES_LIKE => qr/^[a-z0-9][a-z0-9_-]*$/;

Readonly my @ALLOWED_SHORT_OPTS =>
(
	"c", # checksum
	"a", # archive
	"r", # recursive
	"l", # links
	"H", # hard-links
	"p", # perms
	"E", # executability
	"A", # acls
	"X", # xattrs
	"o", # owner
	"g", # group
	"D", # devices + specials
	"t", # times
	"O", # omit-dir-times
	"x", # one-file-system
	"S", # sparse files
	"q", # quiet
);

Readonly my @ALLOWED_LONG_OPTS =>
(
	"server",
	
	"checksum",
	"archive",
	"recursive",
	"links",
	"hard-links",
	"perms",
	"executability",
	"acls",
	"xattrs",
	"owner",
	"group",
	"devices",
	"specials",
	"times",
	"omit-dir-times",
	"one-file-system",
	"numeric-ids",
	"size-only",
	"ignore-existing",
	"sparse",
	"quiet",
);

die "Usage: $0 <backups directory>\n"
	unless((scalar @ARGV) == 1);

# Parse original rsync command line

my @rsync_args   = ();
my $rsync_source = undef;
my $rsync_target = undef;

{
	die "SSH_ORIGINAL_COMMAND not in environment\n"
		unless(defined $ENV{SSH_ORIGINAL_COMMAND});
	
	my ($command, @args) = split(m/\s+/, $ENV{SSH_ORIGINAL_COMMAND});
	
	die "Attempted to run $command rather than rsync\n"
		unless($command eq "rsync");
	
	die "First rsync option must be --server\n"
		unless(@args && $args[0] eq "--server");
	
	foreach my $arg(@args)
	{
		if($arg =~ m/^--(.*)/s)
		{
			my $long_opt = $1;
			
			unless(grep { $long_opt eq $_ } @ALLOWED_LONG_OPTS)
			{
				print STDERR "Unexpected option: $arg\n";
				exit(1); # "Syntax or usage error"
			}
			
			push(@rsync_args, $arg);
		}
		elsif($arg =~ m/^-(.*)/s)
		{
			my @short_opts = split(m//, $1);
			
			my $processing_e = 0;
			foreach my $opt(@short_opts)
			{
				if($processing_e)
				{
					unless($opt =~ m/[\.iLsfxCIvu]/)
					{
						print STDERR "Unexpected character after -e: $opt\n";
						exit(1); # "Syntax error or usage error"
					}
				}
				elsif($opt eq "e")
				{
					# When we are being a server, -e is used for the
					# client to throw some flags at us.
					$processing_e = 1;
				}
				else{
					unless(grep { $opt eq $_ } @ALLOWED_SHORT_OPTS)
					{
						print STDERR "Unexpected option: -$opt\n";
						exit(1); # "Syntax error or usage error"
					}
				}
			}
			
			push(@rsync_args, $arg);
		}
		elsif(!defined $rsync_source)
		{
			$rsync_source = $arg;
		}
		elsif(!defined $rsync_target)
		{
			$rsync_target = $arg;
		}
		else{
			print STDERR "Unexpected argument: $arg\n";
			exit(1); # "Syntax error or usage error"
		}
	}
}

# Get the path to the actual backups directory and validate the target from
# the rsync command line.

my ($backup_dir) = @ARGV;

die "'$rsync_target' is not an acceptable destination\n"
	unless($rsync_target =~ $ALLOW_DEST_NAMES_LIKE);

$backup_dir = File::Spec->catdir($backup_dir, $rsync_target);

make_path($backup_dir);

# Pick a unique name for the new backup
my $backup_name = strftime("%Y-%m-%d", localtime);
my $output_dir  = File::Spec->catdir($backup_dir, $backup_name);
for(my $i = 1; -e $output_dir; ++$i)
{
	$backup_name = strftime("%Y-%m-%d", localtime).".$i";
	$output_dir  = File::Spec->catdir($backup_dir, $backup_name);
}

# Do we have a previous backup to link unchanged files from?
my $last_path = File::Spec->catpath("", $backup_dir, "last");
if(-e $last_path)
{
	push(@rsync_args, "--link-dest=../last/");
}

# Run rsync
system("rsync", @rsync_args, ".", "$output_dir/");
my $rsync_status = $?;

if($rsync_status == 0)
{
	# Backup completed successfuly, symlink it to 'last' so the next run
	# can link files from it rather than copying again.
	
	if(-e $last_path)
	{
		# Can't overwrite a symlink. grr.
		unless(unlink($last_path))
		{
			print STDERR "Couldn't remove 'last' symlink: $!\n";
			exit(11); # "Error in file I/O"
		}
	}
	
	unless(symlink($backup_name, $last_path))
	{
		print STDERR "Couldn't make 'last' symlink: $!\n";
		exit(11); # "Error in file I/O"
	}
}

# Exit, returning rsync's exit code to other rsync
exit(WEXITSTATUS($rsync_status));
