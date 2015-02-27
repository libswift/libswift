
# Remove NDEBUG define to trigger asserts
CPPFLAGS+=-O2 -std=gnu++11 -I. -DNDEBUG -Wall -Wno-sign-compare -Wno-unused -g -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DOPENSSL
LDFLAGS+=-levent -lstdc++ -lssl -lcrypto

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),FreeBSD)
  CXX=clang++
  LIBEVENT_HOME=/usr/local
  CPPFLAGS+=-I${LIBEVENT_HOME}/include
  LDFLAGS+=-L${LIBEVENT_HOME}/lib
else
  CXX?=g++
endif

all: swift-dynamic

swift: swift.o sha1.o compat.o sendrecv.o send_control.o hashtree.o bin.o binmap.o channel.o transfer.o httpgw.o statsgw.o cmdgw.o avgspeed.o avail.o storage.o zerostate.o zerohashtree.o livehashtree.o live.o api.o content.o swarmmanager.o address.o livesig.o exttrack.o

swift-static: swift
	${CXX} ${CPPFLAGS} -o swift *.o ${LDFLAGS} -static -lrt
	strip swift
	touch swift-static

swift-dynamic: swift
	${CXX} ${CPPFLAGS} -o swift *.o ${LDFLAGS}
	touch swift-dynamic

clean:
	rm -f *.o swift swift-static swift-dynamic 2>/dev/null

.PHONY: all clean swift swift-static swift-dynamic
