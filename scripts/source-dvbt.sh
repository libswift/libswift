# Prevent swift blocking waiting for DVB input
cat /dev/dvb/adapter0/dvr0 | ./swift -i - -f storage.dat -l 0.0.0.0:6778

