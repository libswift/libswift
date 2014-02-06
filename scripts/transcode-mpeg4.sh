ffmpeg -f mpegts -i /dev/dvb/adapter0/dvr0 -vcodec mpeg4 -vb 428288 -g 16 -s 320x240 -an -f mpegts - 2>logtrans.log
