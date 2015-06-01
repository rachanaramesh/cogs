#!/usr/bin/perl -w                                                                

while(<>) {
    chomp;
    my ($id, $string) = split(/ /);
    $string =~ s/^0+//;
    $string =~ s/0+$//;
    if($string ne "") {
        print "$id: ";
        my @uptime = split(/0/, $string);
        foreach my $u (@uptime) {
            #print "$d ";                                                         
            if(length($u) > 0) {
		print "".length($u)." ";                                          
                print STDERR "".length($u)."\n";
            }
        }
        print "\n";
    }
}
