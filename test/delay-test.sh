#!/bin/bash

trap "trap - SIGTERM && kill -- -$$" SIGINT

TIMEFORMAT=%R
FILESIZE="10"
LOSS="0"
COUNT="10"

# make directories
rm scp-delay-test.log
rm tor-delay-test.log
rm -rf /tmp/received
rm -rf /tmp/sent
mkdir /tmp/received
mkdir /tmp/sent

# get scp password
read -s -p "Enter ssh password: " PASSWORD_SSH;
echo ""

echo -e "${FILESIZE}\n${LOSS}" >> scp-delay-test.log
echo -e "${FILESIZE}\n${LOSS}" >> tor-delay-test.log

for DELAY in `seq 0 20 100`;
do
  echo "==== delay: $DELAY ms, $FILESIZE MBytes ===="

  #make file
  FILENAME="test.${FILESIZE}M"
  dd if=/dev/urandom of=/tmp/sent/${FILENAME} bs=1000000 count=${FILESIZE} > /dev/null 2>&1

  for IND in `seq 1 1 $COUNT`;
  do
    # run single instance of scp
    COMMAND="./scp-mahimahi.sh"
    ARGS="$FILENAME $PASSWORD_SSH"
    TIME=`mm-delay $DELAY mm-link 12Mbps_trace 12Mbps_trace -- sh -c "$COMMAND $ARGS"`
    echo -e "scp\t${DELAY}\t${TIME}"
    echo -e "${DELAY},${TIME}" >> scp-delay-test.log

    diff /tmp/sent/$FILENAME /tmp/received/$FILENAME
    rm /tmp/received/$FILENAME

    # run receiver in mahimahi shell
    COMMAND="../build/receiver"
    mm-delay $DELAY mm-link 12Mbps_trace 12Mbps_trace -- sh -c "$COMMAND > /dev/null" &
    sleep 1

    # get device name for outer mahimahi container
    DEV=$(ifconfig | grep -oE "delay-\b[0-9]+")

    # add ip table entry
    sudo ip route add 100.64.0.4/32 dev $DEV

    # run sender
    TIME=$( { time ../build/sender 100.64.0.4 /tmp/sent/$FILENAME > /dev/null ; } 2>&1 )
    echo -e "tornado\t${DELAY}\t${TIME}"
    echo -e "${DELAY},${TIME}" >> tor-delay-test.log

    sleep 1
    diff /tmp/sent/$FILENAME ./$FILENAME
    rm ./$FILENAME
  done

  rm /tmp/sent/$FILENAME
done
