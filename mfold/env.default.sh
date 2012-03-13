# This script sets up shared environment variables
# at the servers and during harvesting
export SEEDER=130.161.211.198
# SEEDERPORT must match with the port number on seeder line in
# $SERVERS
export SEEDERPORT=10004
export HASH=66b9644bb01eaad09269354df00172c8a924773b
export BRANCH=master
export ORIGIN=git://github.com/gritzko/swift.git
# Temporary directory for sort (run by dohrv)
export TMPDIR=/home/jori/tmp
# Maximum number of peers to be parsed in parallel by dohrv
export MAXHARVEST=200
# Maximum number of gnuplots to be run in parallel by loggraphs
export MAXGNUPLOTS=50
# General HTB and Netem parameters. Overdriven by env.<hostname>.sh
EMIF=eth0
EMBW=10Mbit
EMDELAY=10ms
