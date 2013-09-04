/*
 *  bttracktest.cpp
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 Vrije Universiteit Amsterdam. All rights reserved.
 *
 */
#include <gtest/gtest.h>
#include "swift.h"


using namespace swift;

#define ROOTHASH_PLAINTEXT	"ArnosFileSwarm"


void tracker_callback(std::string status, uint32_t interval, peeraddrs_t peerlist)
{
    if (status == "")
	fprintf(stderr,"test: Status OK");
    else
	fprintf(stderr,"test: Status failed: %s\n", status.c_str() );
}

TEST(TBTTrack,Encode) {

    Sha1Hash roothash(ROOTHASH_PLAINTEXT,strlen(ROOTHASH_PLAINTEXT));

    fprintf(stderr,"test: Roothash %s\n", roothash.hex().c_str() );

    FileTransfer ft( 481, "storage.dat", roothash, true, POPT_CONT_INT_PROT_NONE, 1024, false );


    BTTrackerClient bt("http://127.0.0.1:5578/announce");
    bt.Contact(ft,"started",tracker_callback);

    fprintf(stderr,"swift: Mainloop\n");
    // Enter libevent mainloop
    event_base_dispatch(Channel::evbase);
}


int main (int argc, char** argv) {

    swift::LibraryInit();
    Channel::evbase = event_base_new();
    Address bindaddr("0.0.0.0:6234");
    if (swift::Listen(bindaddr)<=0)
	fprintf(stderr,"test: Failed swift::Listen\n");

    testing::InitGoogleTest(&argc, argv);
    Channel::debug_file = stdout;
    int ret = RUN_ALL_TESTS();
    return ret;

}
