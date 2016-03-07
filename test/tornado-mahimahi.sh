#!/bin/bash

FILENAME=$1

TIMEFORMAT=%R

TIME=$( { time ../build/sender $MAHIMAHI_BASE /tmp/sent/$FILENAME > /dev/null 2>&1;} 2>& 1)

echo -e "${TIME}"
