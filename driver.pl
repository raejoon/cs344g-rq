#!/usr/bin/perl -w

use strict;

my $receiver_pid = fork;

if ( $receiver_pid < 0 ) {
  die qq{$!};
} elsif ( $receiver_pid == 0 ) {
  # child
  exec q{./build/receiver > receiver.log} or die qq{$!};
}

chomp( my $prefix = qx{dirname `which mm-link`} );
my $tracedir = $prefix . q{/../share/mahimahi/traces};

# run sender inside a link, delay shell
my @command = qw{mm-delay 20 mm-link UPLINK DOWNLINK --meter-uplink mm-loss uplink 0.2 -- sh -c};

push @command, q{./build/sender $MAHIMAHI_BASE demo.20M};

die unless $command[ 3 ] eq "UPLINK";
$command[ 3 ] = qq{$tracedir/Verizon-LTE-short.down};

die unless $command[ 4 ] eq "DOWNLINK";
$command[ 4 ] = qq{$tracedir/Verizon-LTE-short.up};


system @command
