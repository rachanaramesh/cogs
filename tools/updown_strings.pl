#!/usr/bin/perl -w                                                                 

my $STEP = 60 * 60;
my $feature_file = shift;
my $ignore_file = shift;
my %ignore = ();
my $begin = shift;
my $end = shift;

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
	    print "1";

	} else {
	    print "0";

	}
    }
    print "\n";
}
close(FD);
