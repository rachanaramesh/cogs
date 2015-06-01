#!/usr/bin/perl -w                                                                

while(<>) {
    chomp;
    my ($id, $string) = split(/ /);
    $string =~ s/^0+//;
    $string =~ s/0+$//;
    if($string ne "") {
	print "$id: ";
	my @downtime = split(/1/, $string);
	foreach my $d (@downtime) {
	    #print "$d ";                                                         
	    if(length($d) > 0) {
		print "".length($d)." ";
		print STDERR "".length($d)."\n";
	    }
	}
	print "\n";
    }
}
