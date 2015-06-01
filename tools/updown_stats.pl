#!/usr/bin/perl -w                                                                 

my $STEP = 60 * 60;
my $feature_file = shift;
my $ignore_file = shift;
my %ignore = ();
#my $begin = 1193486400;                                                           
my $begin = shift;
#my $end = 1317232800;                                                             
my $end = shift;

my $total_up = 0;
my $total_down = 0;
my $total_avg = 0;

open(FD, "$ignore_file")
    or die "Cannot open file $ignore_file\n";
while(<FD>) {
    chomp;
    my ($start, $end) = split(/ /);
    for(my $i = $start; $i <= $end; $i+=$STEP) {
	$ignore{$i} = 1;
    }
}
close(FD);

open(FD, "$feature_file")
    or die "Cannot open file $feature_file\n";
while(<FD>) {
    chomp;
    my ($identity, @times) = split(/ /);
    my %times = map { $_ => $_ } @times;
    print "$identity ";
    for(my $i = $begin; $i <= $end; $i += $STEP) {
	if(exists($ignore{$i})) {
#      print STDERR "Ignoring $i\n";                                               
	} elsif(exists($times{$i})) {
#      print "1";                                                                  
	    $total_up = $total_up + 1;
	} else {
#      print "0";                                                                  
	    $total_down = $total_down + 1;
	}
    }
    $total_avg = $total_up / ($total_up + $total_down);
#  print "\n"                                                                      
    print "stats: up=$total_up down=$total_down avg=$total_avg ";
    print "\n";
    $total_avg = 0;
    $total_up = 0;
    $total_down = 0;
}
close(FD);
