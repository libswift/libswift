/*
 *  addrtest.cpp
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <gtest/gtest.h>
#include "swift.h"


using namespace swift;


TEST(TAddress,IPv6Loopback) {

    Address a("::1",8093);
    ASSERT_TRUE(IN6_IS_ADDR_LOOPBACK(&a.ipv6()) );
    ASSERT_EQ(8093,a.port());
}

TEST(TAddress,IPv6Any) {

    Address a("::0",8093);
    ASSERT_TRUE(!memcmp(&in6addr_any,&a.ipv6(),sizeof(struct in6_addr) ));
    ASSERT_EQ(8093,a.port());
}


TEST(TAddress,IPv6Global) {

    Address a("2001:610:110:6e1:7578:776f:e141:d2bb",8093);

    unsigned char bytes[16] = { 0x20, 0x01, 0x06, 0x10, 0x01, 0x10, 0x06, 0xe1, 0x75, 0x78, 0x77, 0x6f, 0xe1, 0x41, 0xd2,0xbb };
    for (int i=0; i<16; i++)
	ASSERT_EQ(bytes[i],a.ipv6().s6_addr[i] );
    ASSERT_EQ(8093,a.port());
}

TEST(TAddress,IPv6GlobalRFC2732) {

    Address a("[2001:610:110:6e1:7578:776f:e141:d2bb]:8093");

    unsigned char bytes[16] = { 0x20, 0x01, 0x06, 0x10, 0x01, 0x10, 0x06, 0xe1, 0x75, 0x78, 0x77, 0x6f, 0xe1, 0x41, 0xd2,0xbb };
    for (int i=0; i<16; i++)
	ASSERT_EQ(bytes[i],a.ipv6().s6_addr[i] );
    ASSERT_EQ(8093,a.port());
}

TEST(TAddress,IPv4IPPortString) {

    Address a("130.37.193.65:8093");

    uint32_t al = 0x8225c141;
    ASSERT_EQ(al, a.ipv4() );
    ASSERT_EQ(8093, a.port());
}


TEST(TAddress,IPv4JustAddr) {

    Address a("130.37.193.65");

    uint32_t al = 0x8225c141;
    ASSERT_EQ(al, a.ipv4() );
    ASSERT_EQ(0, a.port());
}


TEST(TAddress,IPv4JustPort) {

    Address a("1300");

    ASSERT_EQ(INADDR_ANY, a.ipv4() );
    ASSERT_EQ(1300, a.port());
}


TEST(TAddress,IPv432Bit) {

    Address a(0x8225c141,8093);
    ASSERT_EQ("130.37.193.65", a.ipstr() );
    ASSERT_EQ(8093, a.port());
}


TEST(TAddress,IPv6SockAddr) {

    struct sockaddr_storage addr;
    addr.ss_family = AF_INET6;
    struct sockaddr_in6 *addr6ptr = (struct sockaddr_in6 *)&addr;
    addr6ptr->sin6_port = htons(8093);
    unsigned char bytes[16] = { 0x20, 0x01, 0x06, 0x10, 0x01, 0x10, 0x06, 0xe1, 0x75, 0x78, 0x77, 0x6f, 0xe1, 0x41, 0xd2,0xbb };
    memcpy(&addr6ptr->sin6_addr.s6_addr,&bytes,16);

    Address a(addr);
    ASSERT_EQ("2001:610:110:6e1:7578:776f:e141:d2bb",a.ipstr() );
    ASSERT_EQ(8093, a.port());
}


TEST(TAddress,IPv4SockAddr) {

    struct sockaddr_storage addr;
    addr.ss_family = AF_INET;
    struct sockaddr_in *addr4ptr = (struct sockaddr_in *)&addr;
    addr4ptr->sin_port = htons(8093);
    uint32_t al = ntohl(0x8225c141);
    memcpy(&addr4ptr->sin_addr.s_addr,&al,4);

    Address a(addr);
    ASSERT_EQ("130.37.193.65",a.ipstr() );
    ASSERT_EQ(8093, a.port());
}


TEST(TAddress,IPv4MappedIPv6EqualDot) {

    Address a("130.37.193.65",8093);
    Address b("::ffff:130.37.193.65",8093);
    ASSERT_TRUE(IN6_IS_ADDR_V4MAPPED(&b.ipv6()) );
    ASSERT_TRUE( a == b );
    ASSERT_TRUE( b == a );
}

TEST(TAddress,IPv4MappedIPv6EqualSemi) {

    Address a("130.37.193.65",8093);
    Address b("::ffff:8225:c141",8093);
    ASSERT_TRUE(IN6_IS_ADDR_V4MAPPED(&b.ipv6()) );
    ASSERT_TRUE( a == b );
    ASSERT_TRUE( b == a );
}



int main (int argc, char** argv) {

    swift::LibraryInit();
    testing::InitGoogleTest(&argc, argv);
    Channel::debug_file = stdout;
    int ret = RUN_ALL_TESTS();
    return ret;

}
