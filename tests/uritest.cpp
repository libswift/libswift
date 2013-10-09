/*
 *  uritest.cpp
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 Vrije Universiteit Amsterdam. All rights reserved.
 */
#include "swift.h"
#include <gtest/gtest.h>


using namespace swift;


TEST(URITest,All)
{

    std::string uri = "tswift://435529e0c615fde3e4179fc64cfb89efb48ec5f0/filename";
    uri += "?v=100";
    uri += "&cp=3";
    uri += "&hf=4";
    uri += "&ca=3";
    uri += "&ld=481";
    uri += "&cs=576";
    uri += "&cl=8192";
    uri += "&cd=-1";
    uri += "&et=http://www.cs.vu.nl/track";
    uri += "&mt=text/html";
    uri += "&ia=130.37.193.64:6778";
    uri += "&dr=500-600";

    // bool swift::ParseURI(std::string uri,parseduri_t &map)
    parseduri_t map;
    bool ret = swift::ParseURI(uri,map);
    ASSERT_TRUE(ret);
    SwarmMeta sm;
    std::string errorstr = URIToSwarmMeta(map,&sm);

    ASSERT_EQ("",errorstr);

    ASSERT_EQ(100,sm.version_);
    ASSERT_EQ(3,sm.cont_int_prot_);
    ASSERT_EQ(4,sm.merkle_func_);
    ASSERT_EQ(3,sm.chunk_addr_);
    ASSERT_EQ(481,sm.live_disc_wnd_);
    ASSERT_EQ(576,sm.chunk_size_);
    ASSERT_EQ(8192,sm.cont_len_);
    ASSERT_EQ(-1,sm.cont_dur_);
    ASSERT_EQ("http://www.cs.vu.nl/track",sm.ext_tracker_url_);
    ASSERT_EQ("text/html",sm.mime_type_);
    ASSERT_TRUE(Address("130.37.193.64",6778) ==  sm.injector_addr_);
    // ASSERT_EQ(500,sm.dash_range_end_)
    // ASSERT_EQ(600,sm.dash_range_end_)
}


int main (int argc, char** argv) {

    // Arno: required
    LibraryInit();
    Channel::evbase = event_base_new();


    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
