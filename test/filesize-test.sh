#!/bin/bash

# kill running receivers
pkill receiver

# make directories
rm -rf ./scp-filesize-test.log
rm -rf ./tor-filesize-test.log
rm -rf /tmp/received
rm -rf /tmp/sent
mkdir /tmp/received
mkdir /tmp/sent

# get scp password
read -s -p "Enter ssh password: " PASSWORD_SSH;
echo ""

DELAY="20"
echo -e "${DELAY}" >> scp-filesize-test.log
echo -e "${DELAY}" >> tor-filesize-test.log

for FILESIZE in `seq 10 10 50`;
  do

    echo "==== Test size: $FILESIZE MBytes ====="

    # make file
    FILENAME="test.${FILESIZE}M"
    dd if=/dev/urandom of=/tmp/sent/$FILENAME bs=1000000 count=$FILESIZE \
      > /dev/null 2>&1

    # run single instance of scp
    COMMAND="./scp-mahimahi.sh"
    ARGS="$FILENAME $PASSWORD_SSH"
    TIME=`mm-delay $DELAY sh -c "$COMMAND $ARGS"`

    # log filesize and time
    echo -e "${FILESIZE}\t${TIME}"
    echo -e "${FILESIZE},${TIME}" >> scp-filesize-test.log

    diff /tmp/sent/$FILENAME /tmp/received/$FILENAME
    
    # run single instance of tornado
    ../build/receiver > /dev/null &

    COMMAND="./tornado-mahimahi.sh"
    ARGS="$FILENAME"
    TIME=`mm-delay $DELAY sh -c "$COMMAND $ARGS"`

    # log filesize and time
    echo -e "${FILESIZE}\t${TIME}"
    echo -e "${FILESIZE},${TIME}" >> tor-filesize-test.log

    #for job in `jobs -p`
    #do
    #  wait $job
    #done
    pkill receiver

    diff /tmp/sent/$FILENAME ./$FILENAME

    # cleanup
    rm ./$FILENAME
    rm /tmp/received/$FILENAME
    rm /tmp/sent/$FILENAME
  done

# clean up
rm -rf /tmp/received
rm -rf /tmp/sent

