#!/bin/bash
# Sets HTB/Netem parameters for the server interfaces. netroot script
# must be run before this.

if [ ! $EMIF ] ; then
    exit
fi

if [ ! $SWFTPORT ]; then
    echo No swift port defined!
    exit 1
fi

if [ ! $EMLOSS ]; then
    EMLOSS=0%
fi

if [ ! $EMDELAY ]; then
    EMDELAY=10ms
fi

if [ ! $EMBW ]; then
    EMBW=10mbit
fi

if [ ! $EMJTTR ]; then
    EMJTTR=0ms
fi

# ingress params
if [ ! $EMLOSS_IN ]; then
    EMLOSS_IN=$EMLOSS
fi

if [ ! $EMDELAY_IN ]; then
    EMDELAY_IN=$EMDELAY
fi

# zero delay in lo may affect htb performance accuracy (?)
if [ $EMDELAY_IN == 0ms ]; then
    EMDELAY_LO_IN=0.1ms
else
    EMDELAY_LO_IN=$EMDELAY_IN
fi

if [ ! $EMBW_IN ]; then
    EMBW_IN=$EMBW
fi

if [ ! $EMJTTR_IN ]; then
    EMJTTR_IN=$EMJTTR
fi

# egress params
if [ ! $EMLOSS_OUT ]; then
    EMLOSS_OUT=$EMLOSS
fi

if [ ! $EMDELAY_OUT ]; then
    EMDELAY_OUT=$EMDELAY
fi

if [ ! $EMBW_OUT ]; then
    EMBW_OUT=$EMBW
fi

if [ ! $EMJTTR_OUT ]; then
    EMJTTR_OUT=$EMJTTR
fi

TC="sudo tc "

CLASSID=$(($SWFTPORT - 9900))
HANDLEID=1$CLASSID

# ingress config
echo adding htb class 1:$CLASSID with rate $EMBW_IN to ifb0
$TC class add dev ifb0 parent 1: classid 1:$CLASSID htb rate $EMBW_IN || exit 2
echo adding filter for destination port $SWFTPORT for to ifb0
$TC filter add dev ifb0 protocol ip prio 1 handle ::$CLASSID u32 \
    match ip dport $SWFTPORT 0xffff flowid 1:$CLASSID || exit 3
echo adding downlink netem handle $HANDLEID for $EMDELAY_IN, $EMLOSS_IN to ifb0
$TC qdisc add dev ifb0 parent 1:$CLASSID handle $HANDLEID \
    netem delay $EMDELAY_IN $EMJTTR_IN 25% loss $EMLOSS_IN || exit 4

echo adding htb class 1:$CLASSID with rate $EMBW_IN to lo
$TC class add dev lo parent 1: classid 1:$CLASSID htb rate $EMBW_IN || exit 5
echo adding filter for destination port $SWFTPORT for to lo
$TC filter add dev lo protocol ip prio 1 handle ::$CLASSID u32 \
    match ip dport $SWFTPORT 0xffff flowid 1:$CLASSID || exit 6
echo adding downlink netem handle $HANDLEID for $EMDELAY_LO_IN, $EMLOSS_IN to lo
$TC qdisc add dev lo parent 1:$CLASSID handle $HANDLEID \
    netem delay $EMDELAY_LO_IN $EMJTTR_IN 25% loss $EMLOSS_IN || exit 7

#egress config
echo adding htb class 1:$CLASSID with rate $EMBW_OUT to $EMIF
$TC class add dev $EMIF parent 1: classid 1:$CLASSID htb rate $EMBW_OUT || exit 8
echo adding filter for source port $SWFTPORT for to $EMIF
$TC filter add dev $EMIF protocol ip prio 1 handle ::$CLASSID u32 \
    match ip sport $SWFTPORT 0xffff flowid 1:$CLASSID || exit 9
echo adding uplink netem handle $HANDLEID for $EMDELAY_OUT, $EMLOSS_OUT to $EMIF
$TC qdisc add dev $EMIF parent 1:$CLASSID handle $HANDLEID \
    netem delay $EMDELAY_OUT $EMJTTR_OUT 25% loss $EMLOSS_OUT || exit 10
