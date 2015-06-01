#!/usr/bin/perl -w                                                                                                                                                                            
while(<>) {
    chomp;
    my ($id, $string) = split(/ /);
    $string =~ s/^0+//;
    $string =~ s/0+$//;
    if($string ne "") {
	#print "$id: $string\n"."$id: ".length($string)."\n";
	print "$id: $string: ".length($string)."\n";
	#print STDERR "".length($string)."\n";
    }
}
