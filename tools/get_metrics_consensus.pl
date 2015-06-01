#!/usr/bin/perl -w

my $start_month = 10;
my $start_year = 2007;

my $end_month = 9;
my $end_year = 2011;

for(my $year = $start_year; ; $year++) {
  for(my $month = 1; $month <= 12; $month++) {
    if($month < 10) {
      $month_str = "0".$month;
    } else {
      $month_str = $month;
    }
    my $url = "https://collector.torproject.org-$year-$month_str.tar.bz2";
    print STDERR "Fetching $url\n";
    system("wget $url");
    exit if ($year == $end_year and $month == $end_month);
  }$month=$start_month;
}

