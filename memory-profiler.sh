#!/usr/bin/perl

$process = $ARGV[0];

while (true) {
  my $pid = `pidof $process`;
  chomp($pid);
  if ($pid ne undef) {
    my $vm_info = `cat /proc/$pid/status | grep VmSize:`;
    my $ram_info = `cat /proc/$pid/status | grep VmRSS:`;

    print "==== [$process] memory usage ====\n";
    print "$vm_info";
    print "$ram_info";
    
    sleep(1);
  }
  else {
    last;
  }
}
