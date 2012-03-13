/*
 *  dgramtest.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 3/13/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <gtest/gtest.h>
//#include <glog/logging.h>
#include "swift.h" // Arno: for LibraryInit

using namespace swift;

struct event_base *evbase;
struct event evrecv;

void ReceiveCallback(int fd, short event, void *arg) {
}

TEST(Datagram, AddressTest) {
    Address addr("127.0.0.1:1000");
    EXPECT_EQ(INADDR_LOOPBACK,addr.ipv4());
    EXPECT_EQ(1000,addr.port());
    Address das2("node300.das2.ewi.tudelft.nl:20000");
    Address das2b("130.161.211.200:20000");
    EXPECT_EQ(das2.ipv4(),das2b.ipv4());
    EXPECT_EQ(20000,das2.port());
}


TEST(Datagram, BinaryTest) {
	evutil_socket_t socket = Channel::Bind(7001);
	ASSERT_TRUE(socket>0);
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(7001);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	const char * text = "text";
	const uint8_t num8 = 0xab;
	const uint16_t num16 = 0xabcd;
	const uint32_t num32 = 0xabcdef01;
	const uint64_t num64 = 0xabcdefabcdeffULL;
	char buf[1024];
	int i;
	struct evbuffer *snd = evbuffer_new();
	evbuffer_add(snd, text, strlen(text));
	evbuffer_add_8(snd, num8);
	evbuffer_add_16be(snd, num16);
	evbuffer_add_32be(snd, num32);
	evbuffer_add_64be(snd, num64);
	int datalen = evbuffer_get_length(snd);
	unsigned char *data = evbuffer_pullup(snd, datalen);
	for(i=0; i<datalen; i++)
	    sprintf(buf+i*2,"%02x",*(data+i));
	buf[i*2] = 0;
	EXPECT_STREQ("74657874ababcdabcdef01000abcdefabcdeff",buf);
	ASSERT_EQ(datalen,Channel::SendTo(socket, addr, snd));
	evbuffer_free(snd);
	event_assign(&evrecv, evbase, socket, EV_READ, ReceiveCallback, NULL);
	event_add(&evrecv, NULL);
	event_base_dispatch(evbase);
	struct evbuffer *rcv = evbuffer_new();
	Address address;
	ASSERT_EQ(datalen,Channel::RecvFrom(socket, address, rcv));
	evbuffer_remove(rcv, buf, strlen(text));
	buf[strlen(text)] = 0;
	uint8_t rnum8 = evbuffer_remove_8(rcv);
	uint16_t rnum16 = evbuffer_remove_16be(rcv);
	uint32_t rnum32 = evbuffer_remove_32be(rcv);
	uint64_t rnum64 = evbuffer_remove_64be(rcv);
	EXPECT_STREQ("text",buf);
	EXPECT_EQ(0xab,rnum8);
	EXPECT_EQ(0xabcd,rnum16);
	EXPECT_EQ(0xabcdef01,rnum32);
	EXPECT_EQ(0xabcdefabcdeffULL,rnum64);
	evbuffer_free(rcv);
	Channel::CloseSocket(socket);
}

TEST(Datagram,TwoPortTest) {
	int sock1 = Channel::Bind("0.0.0.0:10001");
	int sock2 = Channel::Bind("0.0.0.0:10002");
	ASSERT_TRUE(sock1>0);
	ASSERT_TRUE(sock2>0);
	/*struct sockaddr_in addr1, addr2;
	addr1.sin_family = AF_INET;
	addr1.sin_port = htons(10001);
	addr1.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr2.sin_family = AF_INET;
	addr2.sin_port = htons(10002);
	addr2.sin_addr.s_addr = htonl(INADDR_LOOPBACK);*/
	struct evbuffer *snd = evbuffer_new();
	evbuffer_add_32be(snd, 1234);
	Channel::SendTo(sock1,Address("127.0.0.1:10002"),snd);
	evbuffer_free(snd);
	event_assign(&evrecv, evbase, sock2, EV_READ, ReceiveCallback, NULL);
	event_add(&evrecv, NULL);
	event_base_dispatch(evbase);
	struct evbuffer *rcv = evbuffer_new();
	Address address;
	Channel::RecvFrom(sock2, address, rcv);
	uint32_t test = evbuffer_remove_32be(rcv);
	ASSERT_EQ(1234,test);
	evbuffer_free(rcv);
	Channel::CloseSocket(sock1);
	Channel::CloseSocket(sock2);
}

int main (int argc, char** argv) {
	swift::LibraryInit();
	evbase = event_base_new();
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
