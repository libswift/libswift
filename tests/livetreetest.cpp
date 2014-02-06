/*
 *  livetreetest.cpp
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include "bin_utils.h"
#include <gtest/gtest.h>
#include <algorithm>    // std::reverse
#include <ctime>        // std::time
#include <cstdlib>      // std::rand, std::srand


#define DUMMY_DEFAULT_SIG_LENGTH	20


using namespace swift;

/** List of chunks */
typedef std::vector<char *>	  clist_t;

/** Hash tree of a clist, used as ground truth to set peak and test other
 * hashes in LiveHashTree. */
typedef std::map<bin_t,Sha1Hash>  hmap_t;

/* Simple simulated PiecePicker */
typedef enum {
    PICK_INORDER,
    PICK_REVERSE,
    PICK_RANDOM,
} pickpolicy_t;

typedef std::vector<int>  	pickorder_t;


/*
 * Live source tests
 */

LiveHashTree *CreateSourceTree()
{
    KeyPair *kp = KeyPair::Generate(POPT_LIVE_SIG_ALG_RSASHA1);
    // Source: privkey
    return new LiveHashTree(NULL, *kp, SWIFT_DEFAULT_CHUNK_SIZE, SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN);
}

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
    LiveHashTree *umt = CreateSourceTree();

    do_add_data(umt,10);

    // asserts
    ASSERT_EQ(umt->peak_count(), 2);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(1,4));

}


TEST(LiveTreeTest,AddData10Prune3)
{
    LiveHashTree *umt = CreateSourceTree();

    do_add_data(umt,10);

    umt->PruneTree(bin_t(1,0));
    umt->sane_tree();
    umt->PruneTree(bin_t(0,2));
    umt->sane_tree();

    // asserts
    ASSERT_EQ(umt->peak_count(), 2);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(1,4));

}

TEST(LiveTreeTest,AddData10Prune9)
{
    LiveHashTree *umt = CreateSourceTree();

    do_add_data(umt,10);

    umt->PruneTree(bin_t(3,0));
    umt->sane_tree();
    umt->PruneTree(bin_t(0,8));
    umt->sane_tree();

    // asserts
    ASSERT_EQ(umt->peak_count(), 2);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(1,4));

}


/*
 * Live client tests
 */


/** Pretend we're downloading from a source with nchunks available using a
 * piece picking policy that resulted in pickorder.
 */
void do_download(LiveHashTree *umt, int nchunks, int nchunkspersig, hmap_t &truthhashmap, pickorder_t pickorder)
{
    bin_t peak_bins_[64];
    int peak_count = gen_peaks(nchunks,peak_bins_);
    fprintf(stderr,"test: peak count %d\n", peak_count );

    // Send munros
    int nmunros = (nchunks + nchunkspersig-1)/nchunkspersig;
    int nchunkspersiglayer = (int)log2((double)nchunkspersig);
    fprintf(stderr,"test: nchunks %d munro count %d layer %d\n", nchunks, nmunros, nchunkspersiglayer );

    fprintf(stderr,"test: Sending Munros\n");
    for (int i=0; i<nmunros; i++)
    {
	bin_t munrobin(nchunkspersiglayer,i);
	Sha1Hash munrohash = truthhashmap[munrobin];
	umt->OfferHash(munrobin,munrohash);

	// Sign on the fly
	Signature *sig = umt->GetKeyPair()->Sign(munrohash.bytes(),Sha1Hash::SIZE);
	SigTintTuple sigtint(*sig, NOW);

	fprintf(stderr,"test: Sending munro %s\n", munrobin.str().c_str() );

	bool verified = umt->OfferSignedMunroHash(munrobin,sigtint);
	ASSERT_TRUE(verified);
	umt->sane_tree();
    }

    // Verify munros in tree
    for (int i=0; i<nmunros; i++)
    {
	bin_t munrobin(nchunkspersiglayer,i);
	Node *n = umt->FindNode(munrobin);
	Sha1Hash munrohash = truthhashmap[munrobin];
	ASSERT_EQ(n->GetHash(),munrohash);
	ASSERT_TRUE(n->GetVerified());
    }

    // Simulate download of chunks in pickorder
    pickorder_t::iterator citer;
    for (citer=pickorder.begin(); citer != pickorder.end(); citer++)
    {
	int r = *citer;
	fprintf(stderr,"test: \nAdd %" PRIu32 "\n", r);

	bin_t orig(0,r);
	bin_t pos = orig;

	bin_t munro = umt->GetMunro(pos);
	fprintf(stderr,"test: Add: %" PRIu32 " munro %s\n", r, munro.str().c_str() );

	// Sending uncles
	binvector bv;
	while (pos!=munro)
	{
	    bin_t uncle = pos.sibling();
	    bv.push_back(uncle);
	    pos = pos.parent();
	}
	binvector::reverse_iterator iter;
	for (iter=bv.rbegin(); iter != bv.rend(); iter++)
	{
	    bin_t uncle = *iter;
	    fprintf(stderr,"test: Add %" PRIu32 " uncle %s\n", r, uncle.str().c_str() );
	    umt->OfferHash(uncle,truthhashmap[uncle]);
	    umt->sane_tree();
	}

	// Sending actual chunk
	fprintf(stderr,"test: Add %" PRIu32 " orig %s\n", r, orig.str().c_str() );
	ASSERT_TRUE(umt->OfferHash(orig,truthhashmap[orig]));
	umt->sane_tree();

	// Verify uncles in tree
	for (iter=bv.rbegin(); iter != bv.rend(); iter++)
	{
	    bin_t uncle = *iter;
	    fprintf(stderr,"test: Add %" PRIu32 " check verified %s\n", r, uncle.str().c_str() );
	    Node *n = umt->FindNode(uncle);
	    ASSERT_EQ(n->GetHash(),truthhashmap[uncle]);
	    ASSERT_TRUE(n->GetVerified());
	}

	Node *n = umt->FindNode(orig);
	ASSERT_EQ(n->GetHash(),truthhashmap[orig]);
	ASSERT_TRUE(n->GetVerified());
    }
}


/**
 * Create hashtree for nchunks, then emulate that a client is downloading
 * these chunks using the piecepickpolicy and see of the right LiveHashTree
 * gets built.
 */
LiveHashTree *prepare_do_download(int nchunks,pickpolicy_t piecepickpolicy,int startchunk=0, int nchunkspersig=2)
{
    fprintf(stderr,"\ntest: prepare_do_download(%d)\n", nchunks);

    // Create chunks
    clist_t	clist;
    for (int i=0; i<nchunks; i++)
    {
	char *data = new char[1024];
	memset(data,i%255,1024);
	clist.push_back(data);
    }

    // Create leaves
    hmap_t truthhashmap;
    for (int i=0; i<nchunks; i++)
    {
	truthhashmap[bin_t(0,i)] = Sha1Hash(clist[i],1024);
	fprintf(stderr,"test: Hash leaf %s\n", bin_t(0,i).str().c_str() );

    }

    // Pad with zero hashes
    int height = ceil(log2((double)nchunks));

    fprintf(stderr,"test: Hash height %d\n", height );

    int width = pow(2.0,height);
    for (int i=nchunks; i<width; i++)
    {
	truthhashmap[bin_t(0,i)] = Sha1Hash::ZERO;
	fprintf(stderr,"test: Hash empty %s\n", bin_t(0,i).str().c_str() );
    }

    // Calc hashtree
    for (int h=1; h<height+1; h++)
    {
	int step = pow(2.0,h);
	int npairs = width/step;
	for (int i=0; i<npairs; i++)
	{
	    bin_t b(h,i);
	    Sha1Hash parhash(truthhashmap[b.left()],truthhashmap[b.right()]);
	    truthhashmap[b] = parhash;

	    fprintf(stderr,"test: Hash parent %s\n", b.str().c_str() );
	}
    }


    // Client: pubkey
    KeyPair *pubonlykp = KeyPair::Generate(POPT_LIVE_SIG_ALG_RSASHA1);
    LiveHashTree *umt = new LiveHashTree(NULL, *pubonlykp, SWIFT_DEFAULT_CHUNK_SIZE);

    /*
     * Create PiecePicker
     */
    pickorder_t pickvector;

    for (int i=startchunk; i<nchunks; ++i)
        pickvector.push_back(i);

    if (piecepickpolicy == PICK_REVERSE)
	std::reverse(pickvector.begin(),pickvector.end());
    else if (piecepickpolicy == PICK_RANDOM)
    {
	std::srand ( unsigned ( std::time(0) ) );
	std::random_shuffle( pickvector.begin(), pickvector.end() );
    }

    do_download(umt,nchunks,nchunkspersig,truthhashmap,pickvector);

    for (int i=0; i<nchunks; i++)
    {
	delete clist[i];
    }

    return umt;
}

TEST(LiveTreeTest,Download8)
{
    LiveHashTree *umt = prepare_do_download(8,PICK_INORDER);

    // asserts
    ASSERT_EQ(umt->peak_count(), 1);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
}

TEST(LiveTreeTest,Download10)
{
    LiveHashTree *umt = prepare_do_download(10,PICK_INORDER);

    // asserts
    ASSERT_EQ(umt->peak_count(), 2);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(1,4));

}

TEST(LiveTreeTest,Download14)
{
    LiveHashTree *umt = prepare_do_download(14,PICK_INORDER);

    // asserts
    ASSERT_EQ(umt->peak_count(), 3);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(2,2));
    ASSERT_EQ(umt->peak(2), bin_t(1,6));
}


TEST(LiveTreeTest,DownloadIter)
{
    for (int i=0; i<18; i+=2)
    {
	fprintf(stderr,"test: test: DownloadIter %d\n", i );
	LiveHashTree *umt = prepare_do_download(i,PICK_INORDER);
	delete umt;
    }
}

TEST(LiveTreeTest,DownloadIterReverse)
{
    for (int i=0; i<18; i+=2)
    {
	LiveHashTree *umt = prepare_do_download(i,PICK_REVERSE);
	delete umt;
    }
}


TEST(LiveTreeTest,DownloadIterRandom)
{
    for (int i=0; i<18; i+=2)
    {
	LiveHashTree *umt = prepare_do_download(i,PICK_RANDOM);
	delete umt;
    }
}


TEST(LiveTreeTest,Download14Start5)
{
    LiveHashTree *umt = prepare_do_download(14,PICK_INORDER,5);

    // asserts
    ASSERT_EQ(umt->peak_count(), 3);
    ASSERT_EQ(umt->peak(0), bin_t(3,0));
    ASSERT_EQ(umt->peak(1), bin_t(2,2));
    ASSERT_EQ(umt->peak(2), bin_t(1,6));
}


TEST(LiveTreeTest,Download138Start85)
{
    LiveHashTree *umt = prepare_do_download(138,PICK_INORDER,85);
}


TEST(LiveTreeTest,Download600Start489)
{
    LiveHashTree *umt = prepare_do_download(600,PICK_INORDER,489);
}

TEST(LiveTreeTest,Download138Start85SigPer32)
{
    LiveHashTree *umt = prepare_do_download(138,PICK_INORDER,85,32);
}


TEST(LiveTreeTest,Download600Start489SigPer32)
{
    LiveHashTree *umt = prepare_do_download(600,PICK_INORDER,489,32);
}

int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
