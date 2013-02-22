/*
 *  livetreetest.cpp
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "livetree.h"
#include "bin_utils.h"
#include <gtest/gtest.h>


using namespace swift;

void do_add_data(LiveHashTree *umt, int nchunks)
{
    for (int i=0; i<nchunks; i++)
    {
	char data[1024];
	memset(data,i%255,1024);

	umt->AddData(data,1024);
	umt->sane_tree();
    }
}


TEST(LiveTreeTest,AddData10)
{
    LiveHashTree *umt = new LiveHashTree();
    do_add_data(umt,10);

    // asserts
    ASSERT_EQ(umt->peak_count(), 2);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(1,4));

}


void do_download(LiveHashTree *umt, int nchunks)
{
    bin_t peak_bins_[64];
    int peak_count = gen_peaks(nchunks,peak_bins_);
    fprintf(stderr,"peak count %d\n", peak_count );

    // Send peaks
    fprintf(stderr,"Sending Peaks\n");
    for (int i=0; i<peak_count; i++)
    {
	//umt->AddData(data,1024);
	umt->OfferHash(peak_bins_[i],Sha1Hash::ZERO);
	umt->sane_tree();
    }

    for (int i=0; i<nchunks; i++)
    {
	//int r=nchunks-1-i;
	int r=i;
	fprintf(stderr,"\nAdd %u\n", r);

	bin_t orig(0,r);
	bin_t pos = orig;
	bin_t peak = umt->peak_for(pos);
	fprintf(stderr,"Add: %u peak %s\n", r, peak.str().c_str() );

	// Sending uncles
	binvector bv;
	while (pos!=peak)
	{
	    bin_t uncle = pos.sibling();
	    bv.push_back(uncle);
	    pos = pos.parent();
	}
	binvector::reverse_iterator iter;
	for (iter=bv.rbegin(); iter != bv.rend(); iter++)
	{
	    bin_t uncle = *iter;
	    fprintf(stderr,"Add %u uncle %s\n", r, uncle.str().c_str() );
	    umt->OfferHash(uncle,Sha1Hash::ZERO);
	    umt->sane_tree();
	}

	// Sending actual
	fprintf(stderr,"Add %u orig %s\n", r, orig.str().c_str() );
	umt->OfferHash(orig,Sha1Hash::ZERO);
	umt->sane_tree();
    }
}

TEST(LiveTreeTest,Download10)
{
    LiveHashTree *umt = new LiveHashTree();
    do_download(umt,10);

    // asserts
    ASSERT_EQ(umt->peak_count(), 2);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(1,4));
}


int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
