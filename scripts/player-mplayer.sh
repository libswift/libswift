#!/bin/sh
/usr/bin/mplayer -nocache -vf pp=lb http://127.0.0.1:8192/`cat swarm.url | sed -e 's/tswift..//g'`.-1
