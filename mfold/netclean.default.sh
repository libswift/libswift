#!/bin/bash
# Cleans configuration made with netem script.
 
if [ ! $EMIF ] ; then
    exit
fi

if [ ! $SWFTPORT ]; then
    exit
fi

TC="sudo tc "
CLASSID=$(($SWFTPORT - 9900))

echo cleaning filter and class id 1:$CLASSID from ifb0
$TC filter del dev ifb0 protocol ip prio 1 handle 800::$CLASSID u32 \
    flowid 1:$CLASSID
$TC class del dev ifb0 classid 1:$CLASSID

echo cleaning filter and class id 1:$CLASSID from lo
$TC filter del dev lo protocol ip prio 1 handle 800::$CLASSID u32 \
    flowid 1:$CLASSID
$TC class del dev lo classid 1:$CLASSID

echo cleaning filter and class id 1:$CLASSID from $EMIF
$TC filter del dev $EMIF protocol ip prio 1 handle 800::$CLASSID u32 \
    flowid 1:$CLASSID
$TC class del dev $EMIF classid 1:$CLASSID
