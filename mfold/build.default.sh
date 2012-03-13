#!/bin/bash

if [ -e ~/.building_swift ]; then
    exit 0
fi

touch ~/.building_swift

if ! which git || ! which g++ || ! which scons || ! which make ; then
    sudo apt-get -y install make g++ scons git-core || exit 1
fi

if [ ! -e ~/include/event.h ]; then
    echo installing libevent
    mkdir tmp
    cd tmp || exit 2
    wget -c http://monkey.org/~provos/libevent-2.0.7-rc.tar.gz || exit 3
    rm -rf libevent-2.0.7-rc
    tar -xzf libevent-2.0.7-rc.tar.gz || exit 4
    cd libevent-2.0.7-rc/ || exit 5
    ./configure --prefix=$HOME || exit 6
    make || exit 7
    make install || exit 8
    cd ~/
    echo done libevent
fi

if [ ! -e ~/include/gtest/gtest.h ]; then
    echo installing gtest
    mkdir tmp
    cd tmp || exit 9
    wget -c http://googletest.googlecode.com/files/gtest-1.4.0.tar.bz2 || exit 10 
    rm -rf gtest-1.4.0
    tar -xjf gtest-1.4.0.tar.bz2 || exit 11
    cd gtest-1.4.0 || exit 12
    ./configure --prefix=$HOME || exit 13
    make || exit 14
    make install || exit 15
    cd ~/
    echo done gtest
fi

#if ! which pcregrep ; then
#    echo installing pcregrep
#    mkdir tmp
#    cd tmp
#    wget ftp://ftp.csx.cam.ac.uk/pub/software/programming/pcre/pcre-8.01.tar.gz || exit 5
#    tar -xzf pcre-8.01.tar.gz 
#    cd pcre-8.01
#    ./configure --prefix=$HOME || exit 6
#    make -j4 || exit 7
#    make install || exit 8
#    echo done pcregrep
#fi

if [ ! -e swift ]; then
    echo clone the repo
    git clone $ORIGIN || exit 16
fi
cd swift
echo switching the branch
git checkout $BRANCH || exit 17
echo pulling updates
git pull origin $BRANCH:$BRANCH || exit 18

echo building
INCL=~/include LIB=~/lib
CPPPATH=$INCL LIBPATH=$LIB scons -j4 || exit 19
echo testing
LD_LIBRARY_PATH=$LIB tests/connecttest || exit 20

# TODO: one method
mv bingrep.cpp ext/
if [ ! -e bin ]; then mkdir bin; fi
g++ -I. -I$INCL *.cpp ext/seq_picker.cpp -pg -o bin/swift-pg -L$LIB -levent &
g++ -I. -I$INCL *.cpp ext/seq_picker.cpp -g -o bin/swift-dbg -L$LIB -levent &
g++ -I. -I$INCL *.cpp ext/seq_picker.cpp -O2 -o bin/swift-o2 -L$LIB -levent &
wait

rm ~/.building_swift

echo done
