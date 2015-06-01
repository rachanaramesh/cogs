#!/usr/bin/perl                                                                                                                                                      u
use 5.006;
use strict;
use warnings;

my $referenceTable;
my $client;

while(<>) {
    chomp;
    if(m/^Client /){
        my @fields = split(/ /);
        $client = $fields[1];
        chomp;
    }elsif(m/^g /){
        my @guards = split(/;/);
        foreach my $guard (@guards){
            my @guardstats = split(/,/, $guard);
            undef $referenceTable->{$client}->{$guardstats[1]};
        }
    }
}

my ($k, $v);
while (($k, $v) = each(%{$referenceTable})){
    my $guard_count = 0;
    print "Client $k guards: ";
    foreach (keys %{$v}){
        $guard_count++;
    }
    print $guard_count."\n";
}
