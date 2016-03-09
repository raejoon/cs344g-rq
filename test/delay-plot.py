#!/usr/bin/python
import csv
import matplotlib
import matplotlib.pyplot as plt
import common

matplotlib.rcParams.update({'font.size': 22}) 

def main():
  scp_log = "scp-delay-test.log"
  filesize, loss, scp_points = common.parse_log(scp_log)
  scp_x, scp_y, scp_yerr = common.get_statistics(scp_points)

  tor_log = "tor-delay-test.log"
  filesize, loss, tor_points = common.parse_log(tor_log)
  tor_x, tor_y, tor_yerr = common.get_statistics(tor_points)

  plt.errorbar(scp_x, scp_y, yerr=scp_yerr, linewidth=3, label='scp')
  plt.errorbar(tor_x, tor_y, yerr=tor_yerr, linewidth=3, label='tornado')
  #plt.plot(*zip(*scp_points), ls='--', marker='o', ms=15, mew=3, \
  #    fillstyle='none',linewidth=3, label='scp')
  #plt.plot(*zip(*tor_points), ls='--', marker='^', ms=15, mew=3, \
  #    fillstyle='none',linewidth=3, label='tornado')
  plt.xlabel('Link delay (ms)')
  plt.ylabel('Completion time (s)')
  plt.title('Filesize %d MBytes, Link loss %.2f%%, bandwidth 12 Mbits/s' % (int(filesize), loss*100), fontsize=22)
  plt.legend(loc='best')
  plt.tight_layout()
  plt.savefig('delay-plot.pdf')
  

if __name__=="__main__":
  main()
