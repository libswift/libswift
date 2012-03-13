#!/bin/bash

EXEC=build/Debug/
PEERCOUNT=2
STORE=_pextest 
TOKILL=
rm -rf $STORE
mkdir $STORE

#$EXEC/seeder doc/sofi.jpg 7001

for i in `seq 1 $PEERCOUNT`; do
    ( $EXEC/leecher 282a863d5567695161721686a59f0c667250a35d \
        $STORE/sofi$i.jpg 7001 710$i > $STORE/leecher$i.log ) &
    TOKILL="$TOKILL $!"
    sleep 4;
done

sleep 10

for p in $TOKILL; do
    kill -9 $p
done

for i in `seq 1 $PEERCOUNT`; do
    cat $STORE/leecher$i.log | grep sent | awk '{print $5}' | \
        sort | uniq -c > $STORE/peers$i.txt
    peers=`wc -l < $STORE/peers$i.txt`
    if [ $peers -ne $PEERCOUNT ]; then
        echo Peer $i has $peers peers
    fi
done
