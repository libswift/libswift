LIBEVENT_HOME=/prod/pkgs/libevent-2.0.17

CPPFLAGS+=-O2 -I. -Wall -Wno-sign-compare -Wno-unused -g -I${LIBEVENT_HOME}/include -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
LDFLAGS+=-levent -lstdc++

all: swift-dynamic

swift: swift.o sha1.o compat.o sendrecv.o send_control.o hashtree.o bin.o binmap.o channel.o transfer.o httpgw.o statsgw.o cmdgw.o avgspeed.o avail.o storage.o zerostate.o zerohashtree.o
	#nat_test.o

swift-static: swift
	g++ ${CPPFLAGS} -o swift *.o ${LDFLAGS} -static -lrt
	strip swift
	touch swift-static

swift-dynamic: swift
	LDFLAGS+=
	g++ ${CPPFLAGS} -o swift *.o ${LDFLAGS} -L${LIBEVENT_HOME}/lib -Wl,-rpath,${LIBEVENT_HOME}/lib
	touch swift-dynamic

clean:
	rm *.o swift swift-static swift-dynamic2>/dev/null

.PHONY: all clean swift swift-static swift-dynamic
