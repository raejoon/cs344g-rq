#!/bin/bash

FILENAME=$1
PASSWORD_SSH=$2

TIMEFORMAT=%R

TIME=$( { time sshpass -p $PASSWORD_SSH scp \
  $USER@$MAHIMAHI_BASE:/tmp/sent/$FILENAME /tmp/received/ ; } 2>& 1)

echo -e "${TIME}"
