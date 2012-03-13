#!/bin/bash

if [ ! $EMIF ] ; then
    exit
fi

TC="sudo tc "

# echo cleanup
# $TC qdisc del dev $EMIF root
# $TC qdisc del dev $EMIF ingress
# $TC qdisc del dev ifb0 root

echo ifb0 up
sudo modprobe ifb
sudo ip link set dev ifb0 up

echo set lo mtu to 1500
sudo ifconfig lo mtu 1500 || exit 1
 
# Should return OK, when using multiple peers in same host
echo adding ingress
$TC qdisc add dev $EMIF ingress || exit 0

echo redirecting to ifb
$TC filter add dev $EMIF parent ffff: protocol ip prio 1 u32 \
	match u32 0 0 flowid 1:1 action mirred egress redirect dev ifb0 || exit 3

echo adding ifb0 root htb
$TC qdisc add dev ifb0 handle 1: root htb || exit 4

echo adding $EMIF root htb
$TC qdisc add dev $EMIF handle 1: root htb || exit 5

echo adding lo root htb
$TC qdisc add dev lo handle 1: root htb || exit 6
