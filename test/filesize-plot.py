#!/usr/bin/python
import csv
import matplotlib
import matplotlib.pyplot as plt
import common

matplotlib.rcParams.update({'font.size': 18}) 

def main():
  scp_log = "scp-filesize-test.log"
  delay, loss, scp_points = common.parse_log(scp_log)
  scp_x, scp_y, scp_yerr = common.get_statistics(scp_points)

  tor_log = "tor-filesize-test.log"
  delay, loss, tor_points = common.parse_log(tor_log)
  tor_x, tor_y, tor_yerr = common.get_statistics(tor_points)

  plt.errorbar(scp_x, scp_y, yerr=scp_yerr, linewidth=3, label='scp')
  plt.errorbar(tor_x, tor_y, yerr=tor_yerr, linewidth=3, label='tornado')
  #plt.plot(*zip(*scp_points), ls='--', marker='o', ms=15, mew=3, \
  #    fillstyle='none',linewidth=3, label='scp')
  #plt.plot(*zip(*tor_points), ls='--', marker='^', ms=15, mew=3, \
  #    fillstyle='none',linewidth=3, label='tornado')
  plt.xlabel('File size (MBytes)')
  plt.ylabel('Completion time (s)')
  plt.xlim(0, 60)
  plt.title('Link delay %d ms\nLink loss %.2f%%, bandwidth 12 Mbits/s' % (delay,loss*100), fontsize=20)
  plt.legend(loc='best')
  plt.tight_layout()
  plt.savefig('filesize-plot.pdf')
  

if __name__=="__main__":
  main()
