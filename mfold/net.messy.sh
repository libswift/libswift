echo eth0 > .netem-on

sudo tc qdisc add dev eth0 root netem delay 100ms 20ms 25%
