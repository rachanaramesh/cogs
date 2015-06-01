#!/usr/bin/perl -w
use POSIX;

$ENV{TZ}="GMT";
my $toplevel_dir = shift;

if(!defined($toplevel_dir)) {
  print STDERR "Usage: ./parse_consensus.pl <consensus dir>\n";
  exit;
}

my @consensus_files =  `ls $toplevel_dir`;

foreach(@consensus_files) {
  chomp;
  my $gmtime = 0;
  open(FD, "$toplevel_dir/$_") 
    or die "Cannot open $_";
  while(<FD>) {
    chomp;
    # valid-after 2011-09-05 03:00:00
    if(m/^valid-after/) {
      my ($tag, $date, $time) = split(/ /);
      my ($year, $month, $day) = split(/-/, $date);
      my ($hour, $minute, $second) = split(/:/, $time);
      #$gmtime = timegm($second, $minute, $hour, $day, $month, $year);
      $gmtime = POSIX::mktime($second,$minute,$hour,$day,$month-1,$year-1900);

    } 
    # r idideditheconfig AMbFI0CO0XPJJKFPlUjIcPKK5KY iv+nbaEwXy5HLO9Jb7a9LnPiQyM 2011-09-05 01:21:05 46.4.231.10 9001 9030
    elsif(m/^r /) {
      @router_line = split(/ /);
    } 
    # process flags ...
    elsif(m/^s /) {
      $g = 0;
      if(m/Guard/) {
        $g = 1;
      }
      print "$gmtime $router_line[1] $router_line[2] $router_line[6] $router_line[7] $g\n";
    }
    # process bandwidths
    elsif(m/^w /){ 
      my ($tag, $bandwidth) = split(/ /);
      $bandwidth =~ s/Bandwidth=//;
      #print "$bandwidth";
    }
  }
  close(FD);
}
