/*
 *  connecttest.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 3/19/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <sys/stat.h>
#include <time.h>
#include <gtest/gtest.h>
#include "swift.h"


using namespace swift;

struct event evcompl;
int size, copy;

void IsCompleteCallback(int fd, short event, void *arg) {
    if (swift::SeqComplete(copy)!=size)
    	evtimer_add(&evcompl, tint2tv(TINT_SEC));
    else
    	event_base_loopexit(Channel::evbase, NULL);
}

TEST(Connection,CwndTest) {

    Channel::evbase = event_base_new();

    srand ( time(NULL) );

    unlink("test_file0-copy.dat");
    struct stat st;
    int ret = stat("test_file0.dat",&st);

    ASSERT_EQ(0,ret);
    size = st.st_size;//, sizek = (st.st_size>>10) + (st.st_size%1024?1:0) ;
    Channel::SELF_CONN_OK = true;

    int sock1 = swift::Listen(7001);
    ASSERT_TRUE(sock1>=0);

    int file = swift::Open("test_file0.dat");
    FileTransfer fileobj = FileTransfer(file, "test_file0.dat");
    //FileTransfer::instance++;

    swift::SetTracker(Address("127.0.0.1",7001));

    copy = swift::Open("test_file0-copy.dat",fileobj.swarm_id());

    evtimer_assign(&evcompl, Channel::evbase, IsCompleteCallback, NULL);
    evtimer_add(&evcompl, tint2tv(TINT_SEC));

    //swift::Loop(TINT_SEC);
    event_base_dispatch(Channel::evbase);

    //int count = 0;
    //while (swift::SeqComplete(copy)!=size && count++<600)
    //    swift::Loop(TINT_SEC);
    ASSERT_EQ(size,swift::SeqComplete(copy));

    swift::Close(file);
    swift::Close(copy);

    swift::Shutdown();

}


int main (int argc, char** argv) {

    swift::LibraryInit();
    testing::InitGoogleTest(&argc, argv);
    Channel::debug_file = stdout;
    int ret = RUN_ALL_TESTS();
    return ret;

}
