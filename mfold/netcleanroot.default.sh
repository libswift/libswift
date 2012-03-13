#!/bin/bash
# Cleans configurations made with netroot and netem scripts.

if [ ! $EMIF ] ; then
    exit
fi

TC="sudo tc "

echo cleanup
$TC qdisc del dev $EMIF root
$TC qdisc del dev $EMIF ingress
$TC qdisc del dev ifb0 root
$TC qdisc del dev lo root

exit 0
