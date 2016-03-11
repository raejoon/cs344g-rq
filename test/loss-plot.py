#!/usr/bin/python
import csv
import matplotlib
import matplotlib.pyplot as plt
import common

matplotlib.rcParams.update({'font.size': 18}) 

def main():
  scp_log = "scp-loss-test.log"
  filesize, delay, scp_points = common.parse_log(scp_log)
  scp_x, scp_y, scp_yerr = common.get_statistics(scp_points)
  print "got statistics for scp"

  tor_log = "tor-loss-test.log"
  filesize, delay, tor_points = common.parse_log(tor_log)
  tor_x, tor_y, tor_yerr = common.get_statistics(tor_points)
  print "got statistics for tornado"

  scp_x = [100*x for x in scp_x]
  tor_x = [100*x for x in tor_x]
  print scp_yerr
  print tor_yerr

  plt.errorbar(scp_x, scp_y, yerr=scp_yerr, linewidth=3, label='scp')
  print "plot scp"
  plt.errorbar(tor_x, tor_y, yerr=tor_yerr, linewidth=3, label='tornado')
  print "plot tor"
  #plt.plot(*zip(*scp_points), ls='--', marker='o', ms=15, mew=3, \
  #    fillstyle='none',linewidth=3, label='scp')
  #plt.plot(*zip(*tor_points), ls='--', marker='^', ms=15, mew=3, \
  #    fillstyle='none',linewidth=3, label='tornado')
  plt.xlabel('Link loss %')
  plt.ylabel('Completion time (seconds)')
  plt.title('Filesize : %d MBytes\nLink delay %d ms, bandwidth 12 Mbits/s'
      % (int(filesize), int(delay)), fontsize=20)
  plt.legend(loc='best')
  plt.xlim(0.5, 5.5)
  plt.tight_layout()
  plt.savefig('loss-plot.pdf')
  

if __name__=="__main__":
  main()
