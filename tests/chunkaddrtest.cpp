/*
 *  chunkaddrtest.cpp
 *
 *  Test for chunk32 (start,end) to bin32 (b) conversion
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"

#include <gtest/gtest.h>


using namespace swift;


void compare_binmaps(binmap_t &chunkmap, binmap_t &binmap, uint32_t s, uint32_t e)
{
    // s must be the first filled
    bin_t bf = binmap.find_filled();
    bin_t cf = chunkmap.find_filled();
    ASSERT_EQ(cf,bf);
    ASSERT_EQ(cf.base_left(),bin_t(0,s));

    // e must be the first empty from s+1. Unless s==e in which case
    // find_empty() should still return the same for both
    bin_t splus = bin_t(0,s+1);
    bin_t be = binmap.find_empty(splus);
    bin_t ce = chunkmap.find_empty(splus);
    ASSERT_EQ(ce,be);

    // Implementation of binmap_t fix:
    // binmap_t has a default height of 6. If the tree stays smaller than that
    // and e is the right-most chunk in a balanced tree, the next empty returned
    // will be e+1. If the tree has grown above 6, the next empty returns NONE.
    // Not quite so deterministic, so hard to pin down exactly.
    double x = log2((double)(e+1));
    double xint = floor(x);
    x -= xint;
    if (x == 0.0)
    {
	// e is end of balanced tree
	ASSERT_TRUE(ce == bin_t::NONE || ce == bin_t(0,e+1));
    }
    else
    {
	ASSERT_EQ(ce,bin_t(0,e+1));
    }

    ASSERT_TRUE(chunkmap.is_filled(bin_t(0,e)));
    ASSERT_TRUE(binmap.is_filled(bin_t(0,e)));
}


TEST(ChunkAddrTest,Chunk32ToBin32a)
{
    uint32_t s = 5;
    uint32_t e = 25;
    binvector bv;
    binvector expbv;
    expbv.push_back(bin_t(0,5));
    expbv.push_back(bin_t(1,3));
    expbv.push_back(bin_t(3,1));
    expbv.push_back(bin_t(3,2));
    expbv.push_back(bin_t(1,12));

    swift::chunk32_to_bin32(s,e,&bv);

    EXPECT_EQ(expbv,bv);

    binvector::iterator iter;
    binmap_t binmap;
    for (iter=bv.begin(); iter != bv.end(); iter++)
    {
	bin_t b = *iter;
	//fprintf(stderr,"%s\n", b.str().c_str() );
	binmap.set(b);
    }

    binmap_t chunkmap;
    for (uint32_t i=s; i<=e; i++)
    {
	chunkmap.set(bin_t(0,i));
    }

    compare_binmaps(chunkmap, binmap, s, e);
}


TEST(ChunkAddrTest,Chunk32ToBin32b)
{
    uint32_t sm = 269;
    uint32_t em = 312;
    for (uint32_t s=0; s<sm; s++)
    {
	for (uint32_t e=s; e<s+em; e++)
	{
	    //fprintf(stderr,"\ns %u e %u\n", s, e );
	    binvector bv;

	    swift::chunk32_to_bin32(s,e,&bv);

	    binvector::iterator iter;
	    binmap_t binmap;
	    for (iter=bv.begin(); iter != bv.end(); iter++)
	    {
		bin_t b = *iter;
		//fprintf(stderr,"%s\n", b.str().c_str() );
		binmap.set(b);
	    }

	    binmap_t chunkmap;
	    for (uint32_t i=s; i<=e; i++)
	    {
		chunkmap.set(bin_t(0,i));
	    }

	    compare_binmaps(chunkmap, binmap, s, e);
	}
	fprintf(stderr,"." );
    }
}


TEST(ChunkAddrTest,Bin32)
{
    bin_t want(1,843);
    uint32_t s = want.base_offset();
    uint32_t e = (want.base_offset()+want.base_length()-1);

    fprintf(stderr,"want start %u end %u\n", s, e );
    
    binvector bv;
    swift::chunk32_to_bin32(s,e,&bv);

    binvector::iterator iter;
    for (iter=bv.begin(); iter != bv.end(); iter++)
    {
	bin_t b = *iter;
        fprintf(stderr,"got %s\n", b.str().c_str() );
    }
}



int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
