#!/usr/bin/perl -w

use MIME::Base64 qw(decode_base64);

my %bandwidths = ();
my $fp = "";
my $time = "";
my $medianBW = 0;

my $numguards = shift;

sub median {
    @_ == 1 or die ('Sub usage: $median = median(\@array);');
    my ($array_ref) = @_;
    my $count = scalar @$array_ref;
    my @array = sort { $a <=> $b } @$array_ref;
    if ($count % 2) {
        return $array[int($count/2)];
    } else {
        return ($array[$count/2] + $array[$count/2 - 1]) / 2;
    }
}

while (<>) {
    chomp;
    my @fields = split(/ /);
    if (m/^Using/) {
        my @RBW;
        $medianBW = 0;
        my $consensus_file = $fields[3];
        $time = $fields[scalar(@fields)-1];
        $time =~ s/\)//;
        %bandwidths = ();
        open(FD, "$consensus_file")
        or die "Cannot open file $consensus_file";
        while (<FD>) {
        my @hexArr = ();
            chomp;
            if (m/^r /) {
                my @router_stats = split(/ /);
                my $fp_pad_length = ((length($router_stats[2])) % 3);
                if (!$fp_pad_length) {
                    $router_stats[2] = $router_stats[2]."=";
                }
                $fp = decode_base64($router_stats[2]);
                $hexArr = unpack 'H*', $fp;
                $fp = join '', $hexArr =~ /../g;
                $fp = uc $fp;
            }
            elsif(m/^w /) {
                my @vals = split(/=/);
                $bandwidths {$fp} = $vals[1];
                push(@RBW, $vals[1]);
            }
        }
        $medianBW = median(\@RBW);
# add our evilguy into the list of routers
        $bandwidths {"0100000000000000000000000000000000000000"} = $medianBW;
        print "$time: Network median bandwdith was: $medianBW\n";
        close(FD);
    }
    elsif(m/^Client /) {
        print $_." ";
    }
    elsif(m/^g /) {
        my @guards = split(/;/, $fields[1]);
        my $sum = 0;
        my $active = 0;
        foreach my $guard (@guards) {
            my ($nick, $fingerprint, $status) = split(/,/, $guard);
            if ($status eq "up" && $active<$numguards) {
                $sum += $bandwidths {$fingerprint};
                $active++;
	    }
        }
	if($active){
	    if (($sum/$active) >= $medianBW) {
		print "$time, Y, ".$sum." =".$active."= ".$sum/$active."\n";
	    } else {
		print "$time, N, ".$sum." =".$active."= ".$sum/$active."\n";
	    }
	}else {
	    print "no guards contactable - ignore\n";
	}
    }
}

