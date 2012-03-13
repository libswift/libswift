#!/bin/bash
# This script runs a leecher at some server;
# env variables are set in env.default.sh

export LD_LIBRARY_PATH=$HOME/lib

ulimit -c 1024000
cd swift || exit 1
rm -f core
rm -f $HOST-chunk
sleep $(( $RANDOM % 5 ))
bin/swift-o2 -w -h $HASH -f $HOST-chunk -t $SEEDER:$SEEDERPORT \
    -l 0.0.0.0:$SWFTPORT -p -D 2>$HOST-lerr | gzip > $HOST-lout.gz || exit 2
