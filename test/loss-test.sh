#!/bin/bash

trap "trap - SIGTERM && kill -- -$$" SIGINT

TIMEFORMAT=%R
FILESIZE="10"
DELAY="0"
COUNT="10"

# make directories
rm scp-loss-test.log
rm tor-loss-test.log
rm -rf /tmp/received
rm -rf /tmp/sent
mkdir /tmp/received
mkdir /tmp/sent

# get scp password
read -s -p "Enter ssh password: " PASSWORD_SSH;
echo ""

echo -e "${FILESIZE}\n${DELAY}" >> scp-loss-test.log
echo -e "${FILESIZE}\n${DELAY}" >> tor-loss-test.log

for LOSS in `seq 0.01 0.01 0.05`;
do
  echo "==== loss: $LOSS, $FILESIZE MBytes ===="

  #make file
  FILENAME="test.${FILESIZE}M"
  dd if=/dev/urandom of=/tmp/sent/${FILENAME} bs=1000000 count=${FILESIZE} > /dev/null 2>&1

  for IND in `seq 1 1 $COUNT`;
  do
    # run single instance of scp
    COMMAND="./scp-mahimahi.sh"
    ARGS="$FILENAME $PASSWORD_SSH"
    TIME=`mm-loss downlink $LOSS mm-link 12Mbps_trace 12Mbps_trace -- sh -c "$COMMAND $ARGS"`
    echo -e "scp\t${FILESIZE}\t${TIME}"
    echo -e "${FILESIZE},${TIME}" >> scp-loss-test.log

    diff /tmp/sent/$FILENAME /tmp/received/$FILENAME
    rm /tmp/received/$FILENAME

    # run receiver in mahimahi shell
    COMMAND="../build/receiver"
    mm-loss downlink $LOSS mm-link 12Mbps_trace 12Mbps_trace -- sh -c "$COMMAND > /dev/null" &
    sleep 1

    # get device name for outer mahimahi container
    DEV=$(ifconfig | grep -oE "loss-\b[0-9]+")

    # add ip table entry
    sudo ip route add 100.64.0.4/32 dev $DEV

    # run sender
    TIME=$( { time ../build/sender 100.64.0.4 /tmp/sent/$FILENAME > /dev/null ; } 2>&1 )
    echo -e "tornado\t${FILESIZE}\t${TIME}"
    echo -e "${FILESIZE},${TIME}" >> scp-loss-test.log

    sleep 1
    diff /tmp/sent/$FILENAME ./$FILENAME
    rm ./$FILENAME
  done

  rm /tmp/sent/$FILENAME
done
