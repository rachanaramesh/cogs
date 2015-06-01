#!/usr/bin/perl -w

#use strict;
use warnings;

my %table = ();
my $start = 0;
my $old_date= 0;
my $counter = 0;
my $j = 0;
my $i = 0;

my $numguards = shift;

while(<>) {
  chomp;
  # Using consensus file /<install dir>/consensus/2010-11-02-06-00-00-consensus and descriptor file /<install dir>/descriptors/2010-11-02-06-00-00-descriptors (time 1288677600)
  if(m/^Using consensus/) {
    my @fields = split(/ /);
    $time = $fields[scalar(@fields)-1];
    $time =~ s/\)//;
    $start = $time if(!$start);
    $counter = 0;
    print $time."\n";
#    $old_date = $time if(!$old_date);
    
#    if($time != $old_date) {
#	print $old_date." had ".$counter."compromised clients";
#    }

  } elsif(m/^Client/) {
    my @fields = split(/ /);
    $client = $fields[1];
  } elsif(m/^g /) {
    my @guards = split(/;/);
    $i = 0;


    foreach my $guard (@guards) {
#	    print $guard."\n";
	if($guard =~ m/evilguy/ && $i<$numguards) {
	    $counter++;
	    print "Client $client $time ".(($time - $start) / (3600.0 * 24))." days. Counter:".$counter."\n";
	    last;
	} elsif($guard =~ m/,up$/) {
#		print "Not evil.\n";
	    $i++;
	}
    } 
    
  }
}
