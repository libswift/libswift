/*
 *  binstest4.cpp
 *
 *  Tests for binmap_t::find_empty(offset) which finds the next empty bin
 *  from a given offset. Used in VOD seeking to determine whether chunks
 *  are available at the sought-to position.
 *
 *  Created by Victor Grishchenko, Arno Bakker
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "binmap.h"

#include <time.h>
#include <set>
#include <gtest/gtest.h>


using namespace swift;


TEST(BinsTest,FindEmptyStart1)
{
    binmap_t hole;

    for (int s=0; s<8; s++)
    {
	for (int i=s; i<8; i++)
	{
	    hole.set(bin_t(3,0));
	    hole.reset(bin_t(0,i));
	    fprintf(stderr,"\ntest: from %llu want %llu\n", bin_t(0,s).toUInt(),  bin_t(0,i).toUInt() );
	    bin_t f = hole.find_empty(bin_t(0,s));
	    EXPECT_EQ(bin_t(0,i),f);
	}
    }
}


uint64_t seqcomp(binmap_t *ack_out_,uint32_t chunk_size_,uint64_t size_, int64_t offset)
{
    bin_t binoff = bin_t(0,(offset - (offset % chunk_size_)) / chunk_size_);

    fprintf(stderr,"seqcomp: binoff is %llu\n", binoff.toUInt() );

    bin_t nextempty = ack_out_->find_empty(binoff);

    fprintf(stderr,"seqcomp: nextempty is %llu\n", nextempty.toUInt() );

    if (nextempty == bin_t::NONE || nextempty.base_offset() * chunk_size_ > size_)
	return size_-offset; // All filled from offset

    bin_t::uint_t diffc = nextempty.layer_offset() - binoff.layer_offset();
    uint64_t diffb = diffc * chunk_size_;
    if (diffb > 0)
	    diffb -= (offset % chunk_size_);

    return diffb;
}


TEST(BinsTest,FindEmptyStart2){

    binmap_t hole;

    uint32_t chunk_size = 1024;
    uint64_t size = 7*1024 + 15;
    uint64_t incr = 237;

    // ARNOTODO: try with below for loop
    //for (int64_t offset=0; offset<size; offset+=incr)
    for (int64_t offset=0; offset<=incr; offset+=incr)
    {
	for (int i=0; i<9; i++)
	{
	    hole.set(bin_t(3,0));
	    if (i < 8)
		hole.reset(bin_t(0,i));

	    uint64_t want=0;
	    if (i==0)
		want = 0;
	    else if (i==8)
		want = size-offset;
	    else
		want = ((uint64_t)i*(uint64_t)chunk_size) - (offset % chunk_size);
	    fprintf(stderr,"\ntest: from %llu want %llu\n", offset, want );

	    uint64_t got = seqcomp(&hole,chunk_size,size,offset);

	    EXPECT_EQ(want,got);
	}
    }
}

TEST(BinsTest,FindEmptyStart3)
{
    binmap_t hole;

    hole.set(bin_t(0,1));
    hole.set(bin_t(0,2));
    hole.set(bin_t(0,7));
    bin_t f = hole.find_empty(bin_t(0,1));
    EXPECT_EQ(bin_t(0,3),f);
}

TEST(BinsTest,FindEmptyStart4)
{
    binmap_t hole;

    hole.set(bin_t(3,0));
    hole.reset(bin_t(0,0));
    bin_t f = hole.find_empty(bin_t(0,2));
    //EXPECT_EQ(bin_t::NONE,f);
    EXPECT_EQ(bin_t(0,8),f); // binmap_t has minimal tree height of 6, take into account.
}



int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
