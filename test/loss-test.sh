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
echo -e "${FILESIZE}" >> scp-loss-test.log
echo -e "${FILESIZE}" >> tor-loss-test.log

for LOSS in `seq 0 0.0002 0.002`;
  do

    echo "==== Test delay: $DELAY ms loss: $LOSS, filesize: $FILESIZE MBytes ===="

    # make file
    FILENAME="test.${FILESIZE}M"
    dd if=/dev/urandom of=/tmp/sent/$FILENAME bs=1000000 count=$FILESIZE \
      > /dev/null 2>&1

    TIME="0"
    COUNT="20"
    for IND in `seq 1 1 $COUNT`;
    do
      # run single instance of scp
      COMMAND="./scp-mahimahi.sh"
      ARGS="$FILENAME $PASSWORD_SSH"
      SINGLE=`mm-delay $DELAY mm-loss uplink $LOSS sh -c "$COMMAND $ARGS"`

      TIME=`echo $TIME + $SINGLE | bc -l`
      diff /tmp/sent/$FILENAME /tmp/received/$FILENAME
      rm /tmp/received/$FILENAME
    done
    TIME=`echo $TIME / $COUNT | bc -l`

    # log loss and time
    echo -e "SCP\t${LOSS}\t${TIME}"
    echo -e "${LOSS},${TIME}" >> scp-loss-test.log
   
    TIME="0"
    for IND in `seq 1 1 $COUNT`;
    do
      # run single instance of tornado
      ../build/receiver > /dev/null &

      COMMAND="./tornado-mahimahi.sh"
      ARGS="$FILENAME"
      SINGLE=`mm-delay $DELAY mm-loss uplink $LOSS sh -c "$COMMAND $ARGS"`
      
      # cleanup
      #for job in `jobs -p`
      #do 
      #  wait $job
      #done
      pkill receiver &> /dev/null 

      TIME=`echo $TIME + $SINGLE | bc -l`
      diff /tmp/sent/$FILENAME ./$FILENAME
      rm ./$FILENAME
    done
    TIME=`echo $TIME / $COUNT | bc -l`

    # log loss and time
    echo -e "TORNADO\t${LOSS}\t${TIME}"
    echo -e "${LOSS},${TIME}" >> tor-loss-test.log

    rm /tmp/sent/$FILENAME
  done

# clean up
rm -rf /tmp/received
rm -rf /tmp/sent

