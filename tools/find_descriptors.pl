#!/usr/bin/perl -w
use MIME::Base64;

my $start_month = "04";
my $start_year = "2015";
my $output_dir = "/home/rachana/Downloads/cogs-0.2/tools/output";

if(!(defined($start_year) and defined($start_month) and defined($output_dir))) {
  print STDERR "Usage: ./find_descriptors_v2.pl <month> <year> <output_dir>\n";
  exit;
}

my $start_exec_time = time;

system("mkdir -p $output_dir");

my $descriptor_base_path = "/home/rachana/Downloads/cogs-0.2/tools";
my $consensus_base_path = "/home/rachana/Downloads/cogs-0.2/tools";
my $base_desc_path = "$descriptor_base_path/server-descriptors-$start_year-$start_month";

sub build_descriptor_table {
  print STDERR "Finding descriptor files...";
  my $start_find_desc_time = time;
  my $cmd = "find $base_desc_path -type f\n";
  my @files = `$cmd`;
  print STDERR "Done! (took ".(time - $start_find_desc_time)." seconds)\n";

  print STDERR "Building descriptor hash table...";
  my $start_hash_time = time;

  foreach (@files) {
    chomp;
    open(FD, "$_") or die "Cannot open $_\n";
    my $str = "";
    my $fingerprint = "";
    my $publish_date = 0;
    while(<FD>) {
      $str .= "$_";
      # opt fingerprint E6C9 BCB6 C21D 6758 64D6 F96F 46D4 7B84 7183 EA7A
      if(m/^opt fingerprint/) {
        chomp;
        my @fields = split(/ /);
        for(my $i = 2; $i < scalar(@fields); $i++) {
          $fingerprint .= lc($fields[$i]);
        }
      } elsif(m/^published /) {
        # published 2010-02-01 13:30:25
        chomp;
        my @cols = split(/ /);
        my $timestamp = $cols[2];
        my $datestamp = $cols[1];
        my ($h, $m, $s) = split(/:/, $timestamp);
        my ($y, $mo, $d) = split(/-/, $datestamp);
        $publish_date = $d + ($h/24.0) + (($m/60.0)/24.0);
      }
    }
    close(FD);
    $desc_table{$fingerprint}{$publish_date} = $str;
  }

  print STDERR "Done! (took ".(time - $start_hash_time)." seconds)\n";
}

sub process_consensus {
  my $consensus_file = shift;
  print $consensus_file;
  # /home/rachana/Downloads/cogs-0.2/tools/consensuses-2015-04/25/2015-04-25-21-00-00-consensus
  
  my @tmp = split(/\//, $consensus_file);

  print @tmp[-1];
  my @fields = split(/-/, @tmp[-1]);
  my $day = $fields[2];
  my $hour = $fields[3];

  my $descriptor_file = "$output_dir/$start_year-$start_month-$day-$hour-00-00-descriptors";

  $hour =~ s/^0//;
  $day =~ s/^0//;
  my $min = 0;

  print STDERR "Processing $consensus_file\n";

  open(FD, "$consensus_file")
    or die "Cannot open file \"$consensus_file\"\n";
  open(OUT, ">$descriptor_file")
    or die "Cannot open file \"$descriptor_file\"\n";
  while(<FD>) {
    chomp;
    if(!m/^r /) {
      next;
    }
    my(@hexarr) = ();
    my @fields = split(/ /);
    my $bytes = MIME::Base64::decode_base64($fields[2]."=");
    $hexarr = unpack 'H*', $bytes;
    $hex = join '', $hexarr =~ /../g;
    my $first = substr($hex, 0, 1);
    my $second = substr($hex, 1, 1);
    if(exists($desc_table{$hex})) {
      my $closest_time = 0;
      my $closest_descriptor = "";
      foreach my $pub_date (keys %{$desc_table{$hex}}) {
        my $time = $pub_date;
        if(!$closest_time or
           (abs($time - ($day + ($hour/24.0) + (($min/60.0)/24.0))) < $closest_time)) {
          $closest_time = $time;
          $closest_descriptor = $desc_table{$hex}{$pub_date};
        }       
      }
      print OUT "$closest_descriptor\n";
    } else {
      print STDERR "Warning: cannot find router $hex\n";
    }
  }
  close(FD);
  close(OUT);
}

# 2008-10-05-10-00-00-consensus
my $consensus_path = "$consensus_base_path/consensuses-$start_year-$start_month";
print $consensus_path;
my $cmd = "find $consensus_path -type f";
my @files = `$cmd`;

build_descriptor_table();

foreach (@files) {
  chomp $_;
  process_consensus($_);
}

print STDERR "Total run time: ".(time - $start_exec_time)." seconds\n";
