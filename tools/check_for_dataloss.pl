#!/usr/bin/perl -w

my $prev = 0;
my $HOUR = 60*60;
while(<>) {
  chomp;
  if($prev) {
    my $gap = $_ - $prev;
    if($gap > $HOUR) {
      print "".gmtime($prev)." and ".gmtime($_)." (".($gap/$HOUR)." hours)\n";
      print STDERR "$prev $_\n"
    }
  }
  $prev = $_;
}
