#!/bin/bash
# A convenience script for use with net* scripts.

echo --- qdisc info of dev $EMIF ---
sudo tc qdisc show dev $EMIF
echo --- class info of dev $EMIF ---
sudo tc class show dev $EMIF
echo --- filter info of dev $EMIF ---
sudo tc filter show dev $EMIF
echo --- qdisc info of dev ifb0 ---
sudo tc qdisc show dev ifb0
echo --- class info of dev ifb0 ---
sudo tc class show dev ifb0
echo --- filter info of dev ifb0 ---
sudo tc filter show dev ifb0
echo --- qdisc info of dev lo ---
sudo tc qdisc show dev lo
echo --- class info of dev lo ---
sudo tc class show dev lo
echo --- filter info of dev lo ---
sudo tc filter show dev lo
