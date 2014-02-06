
# Remove NDEBUG define to trigger asserts
CPPFLAGS+=-O2 -I. -DNDEBUG -Wall -Wno-sign-compare -Wno-unused -g -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -DOPENSSL
LDFLAGS+=-levent -lstdc++ -lssl -lcrypto

all: swift-dynamic

swift: swift.o sha1.o compat.o sendrecv.o send_control.o hashtree.o bin.o binmap.o channel.o transfer.o httpgw.o statsgw.o cmdgw.o avgspeed.o avail.o storage.o zerostate.o zerohashtree.o livehashtree.o live.o api.o content.o swarmmanager.o address.o livesig.o exttrack.o

swift-static: swift
	g++ ${CPPFLAGS} -o swift *.o ${LDFLAGS} -static -lrt
	strip swift
	touch swift-static

swift-dynamic: swift
	g++ ${CPPFLAGS} -o swift *.o ${LDFLAGS} 
	touch swift-dynamic

clean:
	rm -f *.o swift swift-static swift-dynamic 2>/dev/null

.PHONY: all clean swift swift-static swift-dynamic
