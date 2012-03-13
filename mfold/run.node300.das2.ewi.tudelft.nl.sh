#!/bin/bash

ulimit -c 1024000
cd swift || exit 2
#wget -c http://video.ted.com/talks/podcast/ScottKim_2008P.mp4 || exit 1

./exec/seeder ScottKim_2008P.mp4 0.0.0.0:20000 >lout 2> lerr
