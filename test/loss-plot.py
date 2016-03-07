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
      loss = float(row[0])
      duration = float(row[1])
      points.append((loss, duration))
  return filesize, points

def main():
  scp_log = "scp-loss-test.log"
  filesize,scp_points = parse_log(scp_log)

  tor_log = "tor-loss-test.log"
  filesize,tor_points = parse_log(tor_log)

  plt.plot(*zip(*scp_points), ls='--', marker='o', ms=15, mew=3, \
      fillstyle='none',linewidth=3, label='scp')
  plt.plot(*zip(*tor_points), ls='--', marker='^', ms=15, mew=3, \
      fillstyle='none',linewidth=3, label='tornado')
  plt.xlabel('Link Loss [0, 1)')
  plt.ylabel('Completion time (seconds)')
  plt.title('Filesize : %d MBytes' % filesize, fontsize=22)
  plt.legend(loc='best')
  plt.tight_layout()
  plt.savefig('loss-plot.pdf')
  

if __name__=="__main__":
  main()
