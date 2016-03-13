#!/usr/bin/python
import csv

def parse_log(filename):
  points = {}
  with open(filename) as fo:
    param1 = float(fo.readline())
    param2 = float(fo.readline())
    csv_f = csv.reader(fo)
    for row in csv_f:
      x = float(row[0])
      y = float(row[1])
      if x in points:
        points[x].append(y)
      else:
        points[x] = [y] 

  return param1, param2, points

def get_statistics(points):
  x_list = [];
  y_list = [];
  yerr_list = [];
  for x in sorted(points):
    print "calculating statisctics for ", x
    x_list.append(x);
    y = sum(points[x])/len(points[x]);
    y_list.append(y);
    e = (sum([(t-y)**2 for t in points[x]])/len(points[x]))**0.5;
    yerr_list.append(e);
  return x_list, y_list, yerr_list 
