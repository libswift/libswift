LIBEVENT_HOME=/prod/pkgs/libevent-2.0.15-arno-http

CPPFLAGS+=-O2 -I. -Wall -Wno-sign-compare -Wno-unused -g -I${LIBEVENT_HOME}/include -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
LDFLAGS+=-L${LIBEVENT_HOME}/lib -Wl,-rpath,${LIBEVENT_HOME}/lib -levent -lstdc++

all: swift

swift: swift.o sha1.o compat.o sendrecv.o send_control.o hashtree.o bin.o binmap.o binheap.o channel.o transfer.o httpgw.o statsgw.o cmdgw.o avgspeed.o availability.o storage.o
#nat_test.o
	g++ ${CPPFLAGS} -o swift *.o ${LDFLAGS}

clean:
	rm *.o swift 2>/dev/null

.PHONY: all clean
