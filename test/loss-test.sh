#!/bin/bash

# kill running receivers
pkill receiver

# make directories
rm -rf ./scp-loss-test.log
rm -rf ./tor-loss-test.log
rm -rf /tmp/received
rm -rf /tmp/sent
mkdir /tmp/received
mkdir /tmp/sent

# get scp password
read -s -p "Enter ssh password: " PASSWORD_SSH;
echo ""

FILESIZE="10"
DELAY="20"
COUNT="10"
echo -e "${FILESIZE}\n${DELAY}" >> scp-loss-test.log
echo -e "${FILESIZE}\n${DELAY}" >> tor-loss-test.log

for LOSS in `seq 0 0.0002 0.002`;
do
  echo "==== delay: $DELAY ms loss: $LOSS, size: $FILESIZE MBytes ===="

  # make file
  FILENAME="test.${FILESIZE}M"
  dd if=/dev/urandom of=/tmp/sent/$FILENAME bs=1000000 count=$FILESIZE \
    > /dev/null 2>&1

  for IND in `seq 1 1 $COUNT`;
  do
    # run single instance of scp
    COMMAND="./scp-mahimahi.sh"
    ARGS="$FILENAME $PASSWORD_SSH"
    TIME=`mm-delay $DELAY mm-loss uplink $LOSS sh -c "$COMMAND $ARGS"`

    # log loss and time
    echo -e "SCP\t${LOSS}\t${TIME}"
    echo -e "${LOSS},${TIME}" >> scp-loss-test.log

    diff /tmp/sent/$FILENAME /tmp/received/$FILENAME
    rm /tmp/received/$FILENAME

    # run single instance of tornado
    ../build/receiver > /dev/null &

    COMMAND="./tornado-mahimahi.sh"
    ARGS="$FILENAME"
    TIME=`mm-delay $DELAY mm-loss uplink $LOSS sh -c "$COMMAND $ARGS"`
    
    # log loss and time
    echo -e "TORNADO\t${LOSS}\t${TIME}"
    echo -e "${LOSS},${TIME}" >> tor-loss-test.log

    # cleanup
    #for job in `jobs -p`
    #do 
    #  wait $job
    #done
    pkill receiver &> /dev/null 

    diff /tmp/sent/$FILENAME ./$FILENAME
    rm ./$FILENAME
  done

  rm /tmp/sent/$FILENAME
done

# clean up
rm -rf /tmp/received
rm -rf /tmp/sent

