#!/bin/bash
# This script executes a chain of commands
# on all the member servers, in parallel.
# Commands are defined in .sh files (see
# docmd.sh); all failed executions are
# put to the FAILURES file
rm -f FAILURES

if [ -z "$SERVERS" ]; then
    SERVERS="das2.txt"
fi
HOSTS=`cat $SERVERS | awk '{print $1}'`

for srv in $HOSTS; do
    ( for cmd in $@; do
        if ! ./docmd.sh $srv $cmd; then
            echo $srv >> FAILURES
            echo $src FAILED
            break
        fi
    done ) &
done

wait
echo DONE
