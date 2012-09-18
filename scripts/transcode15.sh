# Read from DVB-T, write to stdout as H.264
ffmpeg -i /dev/dvb/adapter0/dvr0 -vcodec libx264 -deinterlace -vb 1024000 -an -g 18 -f mpegts - 2>logtrans.log

