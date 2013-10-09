/*
 *  availtest.cpp
 *  serp++
 *
 *  Created by Riccardo Petrocco on 09/01/13.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 */

#include "avail.h"

#include <time.h>
#include <set>
#include <gtest/gtest.h>


using namespace swift;


TEST(BinsTest,Avail) {

    Availability a = Availability(20);
    binmap_t p1, p2, p3;
    binmap_t offer;

    offer.set(bin_t(63));

    bin_t b = bin_t(2,0);

    a.set(1, p1 ,b);
    p1.set(b);

    binmap_t *rare = a.get(1);
    bin_t x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t(2,0),x);

    b = bin_t(2,2);
    a.set(1, p1 ,b);
    p1.set(b);

    b = bin_t(3,0);
    a.set(2, p2 ,b);
    p2.set(b);

    rare = a.get(0);
    x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t::NONE,x);

    rare = a.get(1);
    x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t(2,1),x);

    x = binmap_t::find_match(*rare, offer, bin_t(3,1), 0);
    EXPECT_EQ(bin_t(2,2),x);

    rare = a.get(2);
    x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t(2,0),x);

    b = bin_t(4,0);
    a.set(2, p2 ,b);
    p2.set(b);

    rare = a.get(1);
    EXPECT_EQ(1,rare->is_filled(bin_t(2,1)));
    x = binmap_t::find_match(*rare, offer, bin_t(3,1), 0);
    EXPECT_EQ(bin_t(2,3),x);

    x = binmap_t::find_match(*rare, offer, bin_t(3,0), 0);
    EXPECT_EQ(bin_t(2,1),x);

    b = bin_t(1,3);
    a.set(3, p3 ,b);
    p3.set(b);

    x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t(1,2),x);

    a.removeBinmap(2, p2);
    x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t(2,0),x);

    x = binmap_t::find_match(*rare, offer, bin_t(3,1), 0);
    EXPECT_EQ(bin_t(2,2),x);

    rare = a.get(2);
    x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t::NONE,x);

    b = bin_t(3,0);
    a.set(1, p1 ,b);
    p1.set(b);

    x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
    EXPECT_EQ(bin_t(1,3),x);

    a.removeBinmap(3, p3);
    a.removeBinmap(1, p1);

    for (int i=0; i<20; i++) {
        x = binmap_t::find_match(*rare, offer, bin_t::ALL, 0);
        EXPECT_EQ(bin_t::NONE,x);
    }
}


int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
