echo eth0 > .netem-on

sudo tc qdisc add dev eth0 root netem delay 100ms loss 5.0%
