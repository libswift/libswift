/*
 *  apitest.cpp
 *
 *  Simple swift API test.
 *
 *  TODO:
 *  - tests of API calls for live swarm
 *  - AddPeer via Python such that we can test connect back
 *  - Add/RemoveProgressCallback
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include "compat.h"
#include <gtest/gtest.h>

using namespace swift;


#define TESTFILE     "rw.dat"

int RemoveTestFile()
{
    unlink(TESTFILE);
    unlink((std::string(TESTFILE)+".mhash").c_str());
    unlink((std::string(TESTFILE)+".mbinmap").c_str());
    return 0;
}

int CreateTestFile(uint64_t size)
{
    RemoveTestFile();

    int f = open(TESTFILE,O_RDWR|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (f < 0) {
        eprintf("Error opening %s\n",TESTFILE);
        return -1;
    }

    char *buf = new char[size];
    memset(buf,'A',size);
    int ret = write(f,buf,size);
    close(f);
    delete buf;

    return ret;
}

TEST(SimpleAPITest,WriteRead)
{

    RemoveTestFile();

    Sha1Hash fakeroot(true,"a8fdc205a9f19cc1c7507a60c4f01b13d11d7fd0");
    SwarmID swarmid(fakeroot);
    int td = swift::Open(TESTFILE,swarmid);
    ASSERT_NE(td,-1);

    char expblock[1024];
    memset(expblock,'A',512);
    memset(expblock+512,'B',512);
    int ret = swift::Write(td,expblock,1024,0);
    ASSERT_EQ(ret,1024);

    char gotblock[512];
    ret = swift::Read(td,gotblock,512,0);
    ASSERT_EQ(ret,512);
    for (int i=0; i<512; i++)
        ASSERT_EQ(expblock[i],gotblock[i]);

    ret = swift::Read(td,gotblock,512,512);
    ASSERT_EQ(ret,512);
    for (int i=0; i<512; i++)
        ASSERT_EQ(expblock[512+i],gotblock[i]);
}

TEST(SimpleAPITest,SizeFailUnknownTD)
{

    uint64_t ret = swift::Size(567);
    ASSERT_EQ(ret,0);
}


TEST(SimpleAPITest,IsCompleteFailUnknownTD)
{

    bool ret = swift::IsComplete(567);
    ASSERT_EQ(ret,false);
}


TEST(SimpleAPITest,CompleteFailUnknownTD)
{

    uint64_t ret = swift::Complete(567);
    ASSERT_EQ(ret,0);
}


TEST(SimpleAPITest,SeqCompleteFailUnknownTD)
{

    uint64_t ret = swift::SeqComplete(567);
    ASSERT_EQ(ret,0);
}


TEST(SimpleAPITest,SwarmIDFailUnknownTD)
{

    SwarmID gotswarmid = swift::GetSwarmID(567);
    SwarmID expswarmid = SwarmID::NOSWARMID;
    ASSERT_EQ(gotswarmid,expswarmid);
}



TEST(SimpleAPITest,ChunkSizeSuccess1024)
{
    ASSERT_EQ(CreateTestFile(1024),1024);

    SwarmID swarmid = SwarmID::NOSWARMID;
    int td = swift::Open(TESTFILE,swarmid);
    uint32_t cs = swift::ChunkSize(td);
    ASSERT_EQ(cs,1024);

    swift::Close(td,true,true);
}


TEST(SimpleAPITest,ChunkSizeSuccess8192)
{
    ASSERT_EQ(CreateTestFile(1024),1024);

    SwarmID swarmid = SwarmID::NOSWARMID;
    int td = swift::Open(TESTFILE,swarmid,"",false,POPT_CONT_INT_PROT_NONE,false,true,8192);
    uint32_t cs = swift::ChunkSize(td);
    ASSERT_EQ(cs,8192);

    swift::Close(td,true,true);
}

TEST(SimpleAPITest,ChunkSizeFailUnknownTD)
{
    uint32_t cs = swift::ChunkSize(567);
    ASSERT_EQ(cs,0);
}



TEST(SimpleAPITest,GetOSPathNameSuccess)
{
    ASSERT_EQ(CreateTestFile(1024),1024);

    SwarmID swarmid = SwarmID::NOSWARMID;
    int td = swift::Open(TESTFILE,swarmid);
    std::string gotpath = swift::GetOSPathName(td);
    ASSERT_EQ(TESTFILE,gotpath);

    swift::Close(td,true,true);
}


TEST(SimpleAPITest,GetOSPathNameFailUnknownTD)
{
    std::string gotpath = swift::GetOSPathName(567);
    ASSERT_EQ("",gotpath);
}


TEST(SimpleAPITest,IsOperationalFailUnknownTD)
{
    bool ret = swift::IsOperational(567);
    ASSERT_EQ(ret,false);
}


TEST(SimpleAPITest,IsZeroStateSuccess)
{
    ASSERT_EQ(CreateTestFile(4100),4100);

    // Create file and checkpoint
    SwarmID noswarmid = SwarmID::NOSWARMID;
    int td = swift::Open(TESTFILE,noswarmid);
    int ret = swift::Checkpoint(td);
    ASSERT_EQ(ret,0);
    SwarmID expswarmid = swift::GetSwarmID(td);
    swift::Close(td,false,false);

    td = swift::Open(TESTFILE,expswarmid,"",false,POPT_CONT_INT_PROT_NONE,true,true,1024);
    bool retb = swift::IsZeroState(td);
    ASSERT_EQ(retb,true);

    swift::Close(td,true,true); // unlinks content too
}


TEST(SimpleAPITest,IsZeroStateFailUnknownTD)
{
    bool retb = swift::IsZeroState(567);
    ASSERT_EQ(retb,false);
}




TEST(SimpleAPITest,CheckpointFailUnknownTD)
{
    int ret = swift::Checkpoint(567);
    ASSERT_EQ(ret,-1);
}


// TODO: Checkpoint: check files on disk


TEST(SimpleAPITest,SeekSuccess)
{
    ASSERT_EQ(CreateTestFile(4100),4100);

    SwarmID swarmid = SwarmID::NOSWARMID;
    int td = swift::Open(TESTFILE,swarmid);
    int ret = swift::Seek(td,1032,SEEK_SET);
    ASSERT_EQ(ret,0);

    swift::Close(td,true,true);
}


TEST(SimpleAPITest,SeekFailUnknownTD)
{
    int ret = swift::Seek(567,1032,SEEK_SET);
    ASSERT_EQ(ret,-1);
}


TEST(SimpleAPITest,TouchFailUnknownTD)
{
    swift::Touch(567);
}


int main(int argc, char** argv)
{

    // Arno: required
    LibraryInit();
    Channel::evbase = event_base_new();

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
