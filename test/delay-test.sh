#!/bin/bash

# kill running receivers
pkill receiver

# make directories
rm -rf ./scp-delay-test.log
rm -rf ./tor-delay-test.log
rm -rf /tmp/received
rm -rf /tmp/sent
mkdir /tmp/received
mkdir /tmp/sent

# get scp password
read -s -p "Enter ssh password: " PASSWORD_SSH;
echo ""

FILESIZE="10"
echo -e "${FILESIZE}" >> scp-delay-test.log
echo -e "${FILESIZE}" >> tor-delay-test.log

for DELAY in `seq 20 20 200`;
  do

    echo "==== Test delay: $DELAY ms ====="

    # make file
    FILENAME="test.${FILESIZE}M"
    dd if=/dev/urandom of=/tmp/sent/$FILENAME bs=1000000 count=$FILESIZE \
      > /dev/null 2>&1

    # run single instance of scp
    COMMAND="./scp-mahimahi.sh"
    ARGS="$FILENAME $PASSWORD_SSH"
    TIME=`mm-delay $DELAY sh -c "$COMMAND $ARGS"`

    # log delay and time
    echo -e "${DELAY}\t${TIME}"
    echo -e "${DELAY},${TIME}" >> scp-delay-test.log

    diff /tmp/sent/$FILENAME /tmp/received/$FILENAME
    
    # run single instance of tornado
    ../build/receiver > /dev/null &

    COMMAND="./tornado-mahimahi.sh"
    ARGS="$FILENAME"
    TIME=`mm-delay $DELAY sh -c "$COMMAND $ARGS"`

    # log delay and time
    echo -e "${DELAY}\t${TIME}"
    echo -e "${DELAY},${TIME}" >> tor-delay-test.log
    
    # cleanup
    #for job in `jobs -p`
    #do 
    #  wait $job
    #done
    pkill receiver

    diff /tmp/sent/$FILENAME ./$FILENAME

    rm ./$FILENAME
    rm /tmp/received/$FILENAME
    rm /tmp/sent/$FILENAME
  done

# clean up
rm -rf /tmp/received
rm -rf /tmp/sent

