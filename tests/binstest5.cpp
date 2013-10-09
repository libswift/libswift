/*
 *  binstest5.cpp
 *  serp++
 *
 *  Created by Riccardo Petrocco on 09/01/13.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "binmap.h"

#include <time.h>
#include <set>
#include <gtest/gtest.h>


using namespace swift;



TEST(BinsTest,within) {

    binmap_t avail, source;
    avail.set(bin_t(23));
    source.set(bin_t(31));

    bin_t s = bin_t(63);
    //fprintf(stderr,"Searching 0,12 from %s ", s.base_left().str().c_str() );
    //fprintf(stderr,"to %s\n", s.base_right().str().c_str() );

    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin_t(23),x);

}


TEST(BinsTest,filledDest) {

    binmap_t avail, source;
    avail.set(bin_t(23));
    source.set(bin_t(63));

    bin_t s = bin_t(63);
    //fprintf(stderr,"Searching 0,12 from %s ", s.base_left().str().c_str() );
    //fprintf(stderr,"to %s\n", s.base_right().str().c_str() );

    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin_t(23),x);

}


TEST(BinsTest,filledRange) {

    binmap_t avail, source;
    avail.set(bin_t(63));
    source.set(bin_t(63));

    bin_t s = bin_t::ALL;
    //fprintf(stderr,"Searching 0,12 from %s ", s.base_left().str().c_str() );
    //fprintf(stderr,"to %s\n", s.base_right().str().c_str() );
    bin_t bin = bin_t(63);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    avail.reset(bin_t(31));

    bin = bin_t(95);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    avail.set(bin_t(47));

    bin = bin_t(47);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    s = bin_t(15);
    bin = bin_t::NONE;
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin_t::NONE,x);

    s = bin_t(31);
    bin = bin_t(47);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);
}


TEST(BinsTest,outOfBinmap) {

    binmap_t avail, source;
    avail.set(bin_t(63));
    source.set(bin_t(143));

    bin_t s = bin_t::ALL;
    //fprintf(stderr,"Searching 0,12 from %s ", s.base_left().str().c_str() );
    //fprintf(stderr,"to %s\n", s.base_right().str().c_str() );
    bin_t bin = bin_t::NONE;
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    avail.reset(bin_t(31));

    bin = bin_t::NONE;
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    avail.set(bin_t(159));

    bin = bin_t(143);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    source.set(bin_t(191));

    s = bin_t(159);

    bin = bin_t(159);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    s = bin_t(127);
    bin = bin_t(159);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    source.set(bin_t(95));

    s = bin_t(63);
    bin = bin_t(95);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    s = bin_t(83);
    bin = bin_t(83);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    source.set(bin_t(63));

    s = bin_t(127);
    bin = bin_t(95);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    source.reset(bin_t(63));
    source.set(bin_t(73));

    bin = bin_t(73);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);
}


TEST(BinsTest,diffRoot1) {

    binmap_t avail, source;
    avail.set(bin_t(95));
    source.set(bin_t(127));

    bin_t s = bin_t::ALL;

    bin_t bin = bin_t(95);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

}


TEST(BinsTest,diffRoot2) {

    binmap_t avail, source;
    avail.set(bin_t(127));
    source.set(bin_t(95));

    bin_t s = bin_t::ALL;

    bin_t bin = bin_t(95);
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

}

TEST(BinsTest,diffRoot3) {

    binmap_t avail, source;
    source.set(bin_t(319));

    bin_t s = bin_t::ALL;

    bin_t bin = bin_t::NONE;
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    avail.set(bin_t(175));
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

}


TEST(BinsTest,diffRoot4) {

    binmap_t avail, source;
    source.set(bin_t(14,1));

    bin_t s = bin_t::ALL;

    bin_t bin = bin_t::NONE;
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    bin_t x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

    avail.set(bin_t(5,1));
    fprintf(stderr,"search for %s\n", bin.str().c_str() );
    x = binmap_t::find_match(avail, source, s, 0);
    EXPECT_EQ(bin,x);

}

int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
