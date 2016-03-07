#!/usr/bin/python
import csv
import matplotlib
import matplotlib.pyplot as plt

matplotlib.rcParams.update({'font.size': 22}) 

def parse_log(filename):
  points = []
  with open(filename) as fo:
    filesize = int(fo.readline());
    csv_f = csv.reader(fo)
    for row in csv_f:
      delay = int(row[0])
      duration = float(row[1])
      points.append((delay, duration))
  return filesize, points

def main():
  scp_log = "scp-delay-test.log"
  filesize,scp_points = parse_log(scp_log)

  tor_log = "tor-delay-test.log"
  filesize,tor_points = parse_log(tor_log)

  plt.plot(*zip(*scp_points), ls='--', marker='o', ms=15, mew=3, \
      fillstyle='none',linewidth=3, label='scp')
  plt.plot(*zip(*tor_points), ls='--', marker='^', ms=15, mew=3, \
      fillstyle='none',linewidth=3, label='tornado')
  plt.xlabel('Delay (milliseconds)')
  plt.ylabel('Completion time (seconds)')
  plt.title('Filesize : %d MBytes' % filesize, fontsize=22)
  plt.legend(loc='best')
  plt.tight_layout()
  plt.savefig('delay-plot.pdf')
  

if __name__=="__main__":
  main()
