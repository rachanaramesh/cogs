#!/usr/bin/perl -w
use POSIX ":sys_wait_h";
use Getopt::Std;

our $tor = "/<install dir>/cogs/src/tor/src/or/tor";
our @EVIL = (1);
our %proc_table = ();
our $opt_j;
our $opt_s;
our $opt_t;
our $opt_c;
our $opt_p;
our $opt_m;
our $opt_b;
our $opt_n;

sub int_cleanup {
  print STDERR "SIGINT killing pids: ";
  foreach my $pid (keys %proc_table) {
    print STDERR "$pid ";
    kill(9, $pid);
  }
  print STDERR "\n";
  exit(0);
}

sub launch_jobs {
  my $num_evil = shift;
  my $clients_per_run = int($opt_c / ($opt_j));
  my $MBW = int($opt_b * 1024 * 1024);
  $clients_per_run += $opt_c % $opt_j;
  my $fudge = ($clients_per_run * $opt_j) - $opt_c; # how much over are we?
  for(my $i = 0; $i < $opt_j; $i++) {
    my $num_clients;

    if($i < $opt_j - 1) {
      $num_clients = $clients_per_run;
    } else {
      $num_clients = $clients_per_run - $fudge; # subtract our excess
    }

    my $cmd = "$tor --Simulate 1 --ConsensusDir /<install dir>/cogs/metrics/consensus ";
    $cmd .=   "--DescriptorDir /<install dir>/cogs/metrics/descriptors --SocksPort ".(9000 + $i)." ";
    $cmd .=   "--StartConsensus $opt_s --StopConsensus $opt_t --DataDirectory ~/tmp/dir.$i "; 
    $cmd .=   "--NumPaths $opt_p --NumClients $num_clients --NumMaliciousNodes $num_evil "; 
    $cmd .=   "--Log err --MaliciousExit 0 --MaliciousGuard 1 ";
    $cmd .=   "--NoChurn 1 --NumEntryGuards $opt_n --MaliciousIntroDate $opt_m --MaliciousBandwidth $MBW > out.$i.$num_evil.txt";

    if(!defined($pid = fork())) {
      die "Cannot fork(): $!";
    } elsif($pid == 0) { # child process
      exec("ulimit -c unlimited ; $cmd");
    } else { # parent process
      my $now = time();
      $proc_table{$pid} = $now;
      print "Starting tor sim process at ".localtime($now)." ";
      print "pid=$pid num_clients=$num_clients num_evil=$num_evil\n";
    }
  }
}

sub usage {
  print STDERR "Usage: ./batch_simulation.pl -j <num_jobs> -s <start_time> -t <stop_time> -c <num_clients> -p <num_paths> -m <Malicious gaurds introduction date> -b <Malicious Bandwidth> -n <number of entry guards>\n";
  exit -2;
}

$SIG{'INT'} = 'int_cleanup';

getopts("j:s:t:c:p:m:b:n:");

usage if (!(defined($opt_j) and defined($opt_s) and 
            defined($opt_t) and defined($opt_c) and
            defined($opt_p) and defined($opt_m) and
	    defined($opt_b) and defined($opt_n)));

foreach my $num_evil (@EVIL) {
  launch_jobs($num_evil);
  while(%proc_table) {
    my $pid = waitpid(-1, 0);
    print "pid=$pid finished. Run time: ".(time() - $proc_table{$pid})." seconds\n";
    delete $proc_table{$pid};
  }
}
