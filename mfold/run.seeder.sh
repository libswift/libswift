#!/bin/bash

export LD_LIBRARY_PATH=$HOME/lib

ulimit -c 1024000
cd swift || exit 2
if [ ! -e ScottKim_2008P.mp4 ]; then
    wget -c http://video.ted.com/talks/podcast/ScottKim_2008P.mp4 || exit 1
fi

bin/swift-o2 -w -f ScottKim_2008P.mp4 -p -D \
    -l 0.0.0.0:$SEEDERPORT 2>$HOST-lerr | gzip > $HOST-lout.gz || exit 2
exit
