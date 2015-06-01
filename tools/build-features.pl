#!/usr/bin/perl -w

my %table = ();
my %ignore = ();
my $missing_data_file = shift;

if(!defined($missing_data_file)) {
  print STDERR "Usage: ./build-features <missing_data_file>\n";
  exit 1;
}

open(FD, "$missing_data_file")
  or die "Cannot open $missing_data_file\n";
while(<FD>) {
  chomp;
  my ($start, $end) = split(/ /);
  $ignore{$end} = 1;
}
close(FD);

# 1193486400 Lucna AY0jLEmr95Lx+GygEgHB1rSg6wU 216.110.217.24 9001 0
while(<>) {
  chomp;
  my ($time, $nick, $identity, $ip, $or_port, $guard) = split(/ /);
  $table{$identity}{$time} = 1 if !exists($ignore{$time});
 if(exists($ignore{$time})) {
    print STDERR "ignoring $time\n";
 }
}

foreach my $identity (keys %table) {
  print "$identity ";
  foreach my $time (sort {$a <=> $b} keys %{$table{$identity}}) {
    print "$time ";
  }
  print "\n";
}
