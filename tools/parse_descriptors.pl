#!/usr/bin/perl -w
use POSIX;

$ENV{TZ}="GMT";

#router DMMD 75.164.4.12 9001 0 0
#platform Tor 0.2.0.4-alpha on Windows XP Service Pack 2 [workstation] {terminal services, single user}
#published 2007-08-21 14:10:55
#opt fingerprint 69A9 F2E6 57D1 57B5 9C72 343D AA54 AE58 A7F7 3796
#uptime 143166
#bandwidth 30720 30720 65653


while(<>) {
  chomp;
  if(m/^router/) {
   our ($marker, $nickname, $ip, $or_port) = split(/ /);
  } elsif(/^published/) {
    my ($marker, $date, $time) = split(/ /);
    my ($year, $month, $day) = split(/-/, $date);
    my ($hour, $minute, $second) = split(/:/, $time);
    $gmtime = POSIX::mktime($second,$minute,$hour,$day,$month-1,$year-1900);
  } elsif(/^opt fingerprint/) {
    our ($marker1, $marker2, @fingerprint) = split(/ /);
    $fingerprint_hex_str = "";
    foreach (@fingerprint) {
      chomp;
      $fingerprint_hex_str .= $_;
    }
  } elsif(/^bandwidth/) {
    our ($marker, $bw_avg, $bw_burst, $bw_observed) = split(/ /);
    print "$gmtime $nickname $ip $or_port $fingerprint_hex_str $bw_observed\n";
  }
}
