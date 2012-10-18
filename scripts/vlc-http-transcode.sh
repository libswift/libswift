vlc v4l2:// --sout '#transcode{vcodec=h264,vb=800,scale=1,acodec=none}:std{access=http,mux=ts,dst=0.0.0.0:8080/source}' --no-sout-rtp-sap --no-sout-standard-sap --sout-keep
