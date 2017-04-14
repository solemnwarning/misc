#!/usr/bin/perl
# Tool for extracting "MAD" archives used by the Hogs at War games.
# Copyright (C) 2017 Daniel Collins <solemnwarning@solemnwarning.net>
#
# The format of these files (as far as I can tell) is a list of file headers
# followed by file data. Nothing says how many headers are in the file or which
# is the last one, so we keep reading headers until we stray into an area used
# for data by an already-read header.
#
# Each file header is 24 bytes long, structured as follows:
#
# Offset | Length | Description
# -------+--------+------------
#      0 |     12 | File name, 8 bit characters, nul padded
#     12 |      4 | Unknown, could be more filename
#     16 |      4 | File data offset, little endian uint32
#     20 |      4 | File data length, little endian uint32
#
# Data chunks appear to be aligned on 4 byte boundaries, previous data sections
# are padded with zeroes where necessary.

use strict;
use warnings;

use Fcntl qw(SEEK_SET);

# For unit test loading, one day.
main() unless(caller);

sub main
{
	my ($command, $madfile, @extra_args) = @ARGV;
	
	unless(    ((scalar @ARGV) == 2 && $command eq "list")
		|| ((scalar @ARGV) == 3 && $command eq "cat")
		|| ((scalar @ARGV) == 2 && $command eq "extract")
		|| ((scalar @ARGV) == 3 && $command eq "extract"))
	{
		print "Usage: $0 list    archive.mad\n";
		print "       $0 cat     archive.mad innerfile.tim\n";
		print "       $0 extract archive.mad [output directory]\n";
	
		exit(42); # EX_USAGE
	}
	
	open(my $fh, "<", $madfile) or die "$madfile: $!\n";
	binmode($fh, ":raw");
	
	my @files = mad_load_index($fh);
	
	if($command eq "list")
	{
		print $_->{name}, "\n" foreach(@files);
	}
	elsif($command eq "cat")
	{
		my ($innerfile) = @extra_args;
		
		my ($file) = grep { $_->{name} eq $innerfile }
			@files;
		
		die "$innerfile not found in archive\n"
			unless(defined $file);
		
		my $data = mad_load_data($fh, $file);
		
		binmode(STDOUT, ":raw");
		print $data;
	}
	elsif($command eq "extract")
	{
		my ($outdir) = @extra_args;
		
		if(defined $outdir)
		{
			chdir($outdir) or die "chdir: $!\n";
		}
		
		foreach my $file(@files)
		{
			my $data = mad_load_data($fh, $file);
			
			open(my $ofh, ">", $file->{name}) or die $file->{name}.": $!\n";
			binmode($ofh, ":raw");
			
			print {$ofh} $data;
		}
	}
}

sub mad_load_index
{
	my ($fh) = @_;
	
	my @files   = ();
	my $min_off = undef;
	
	do {
		my $hdr;
		die "read: $!\n"
			unless(defined(read($fh, $hdr, 24)));
		
		die "Couldn't read full header - truncated MAD file?\n"
			unless(length($hdr) == 24);
		
		# Header structure:
		#
		# Offset | Length | Description
		# -------+--------+------------
		#      0 |     12 | File name, 8 bit characters, nul padded
		#     12 |      4 | Unknown, could be more filename
		#     16 |      4 | File data offset, little endian uint32
		#     20 |      4 | File data length, little endian uint32
		my ($name, undef, $offset, $length) = unpack("Z12VVV", $hdr);
		
		# Limit filenames to a small set of characters to avoid corrupt
		# or badly-parsed files extracting outside of the CWD.
		die "Unexpected characters in filename ($name) - corrupt MAD file?\n"
			unless($name =~ m/^[A-Za-z0-9 !#\$%&'\(\)\-\@^_`\{\}~\.]+$/s);
		
		push(@files, {
			name   => $name,
			offset => $offset,
			length => $length,
		});
		
		$min_off = $offset
			if(!defined($min_off) || $min_off > $offset);
	} while(tell($fh) < $min_off);
	
	return @files;
}

sub mad_load_data
{
	my ($fh, $file) = @_;
	
	seek($fh, $file->{offset}, SEEK_SET)
		or die "Couldn't seek to data for '".$file->{name}."', corrupt MAD file?\n";
	
	my $data;
	die "read: $!"
		unless(defined read($fh, $data, $file->{length}));
	
	die "Unexpected EOF reading data for '".$file->{name}."', corrupt MAD file?\n"
		unless(length($data) == $file->{length});
	
	return $data;
}
