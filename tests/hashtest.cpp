/*
 *  hashtest.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 3/12/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <fcntl.h>
#include "bin.h"
#include <gtest/gtest.h>
#include "hashtree.h"
#include "swift.h"

using namespace swift;

char hash123[] = "a8fdc205a9f19cc1c7507a60c4f01b13d11d7fd0";
char rooth123[] = "a8fdc205a9f19cc1c7507a60c4f01b13d11d7fd0";

char hash456a[] = "4d38c7459a659d769bb956c2d758d266008199a4";
char hash456b[] = "a923e4b60d2a2a2a5ede87479e0314b028e3ae60";
char rooth456[] = "5b53677d3a695f29f1b4e18ab6d705312ef7f8c3";


TEST(Sha1HashTest,Trivial) {
	Sha1Hash hash("123\n");
	EXPECT_STREQ(hash123,hash.hex().c_str());
}


TEST(Sha1HashTest,OfferDataTest) {
	Sha1Hash roothash123(true,hash123);
	//for(bin_t pos(0,0); !pos.is_all(); pos=pos.parent())
	//	roothash123 = Sha1Hash(roothash123,Sha1Hash::ZERO);
    unlink("123");
    EXPECT_STREQ(rooth123,roothash123.hex().c_str());
    Storage storage("123");
	HashTree tree(&storage,roothash123);
    tree.OfferHash(bin_t(0,0),Sha1Hash(true,hash123));
	ASSERT_EQ(1,tree.size_in_chunks());
    ASSERT_TRUE(tree.OfferData(bin_t(0,0), "123\n", 4));
    unlink("123");
	ASSERT_EQ(4,tree.size());
}


TEST(Sha1HashTest,SubmitTest) {
    FILE* f123 = fopen("123","wb+");
    fprintf(f123, "123\n");
    fclose(f123);
    Storage storage("123");
    HashTree ht123(&storage);
    EXPECT_STREQ(hash123,ht123.hash(bin_t(0,0)).hex().c_str());
    EXPECT_STREQ(rooth123,ht123.root_hash().hex().c_str());
    EXPECT_EQ(4,ht123.size());
}


TEST(Sha1HashTest,OfferDataTest2) {
	char data456a[1024]; // 2 chunks with cs 1024, 3 nodes in tree
	for (int i=0; i<1024; i++)
		data456a[i] = '$';
	char data456b[4];
	for (int i=0; i<4; i++)
		data456b[i] = '$';

    FILE* f456 = fopen("456","wb");
    fwrite(data456a,1,1024,f456);
    fwrite(data456b,1,4,f456);
    fclose(f456);

	Sha1Hash roothash456(Sha1Hash(true,hash456a),Sha1Hash(true,hash456b));
    unlink("456");
    EXPECT_STREQ(rooth456,roothash456.hex().c_str());
    Storage storage("456");
	HashTree tree(&storage,roothash456);
	tree.OfferHash(bin_t(1,0),roothash456);
    tree.OfferHash(bin_t(0,0),Sha1Hash(true,hash456a));
    tree.OfferHash(bin_t(0,1),Sha1Hash(true,hash456b));
	ASSERT_EQ(2,tree.size_in_chunks());
    ASSERT_TRUE(tree.OfferData(bin_t(0,0), data456a, 1024));
    ASSERT_TRUE(tree.OfferData(bin_t(0,1), data456b, 4));
    unlink("456");
	ASSERT_EQ(1028,tree.size());
}


/*TEST(Sha1HashTest,HashFileTest) {
	uint8_t a [1024], b[1024], c[1024];
	memset(a,'a',1024);
	memset(b,'b',1024);
	memset(c,'c',1024);
	Sha1Hash aaahash(a,1024), bbbhash(b,1024), ccchash(c,1024);
	Sha1Hash abhash(aaahash,bbbhash), c0hash(ccchash,Sha1Hash::ZERO);
	Sha1Hash aabbccroot(abhash,c0hash);
	for(bin pos=bin(7); pos<bin::ALL; pos=pos.parent())
		aabbccroot = Sha1Hash(aabbccroot,Sha1Hash::ZERO);
	int f = open("testfile",O_RDWR|O_CREAT|O_TRUNC,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	write(f,a,1024);
	write(f,b,1024);
	write(f,c,1024);
	HashTree filetree(f);
	close(f);
	ASSERT_TRUE(aabbccroot==filetree.root);
	EXPECT_EQ(2,filetree.peaks.size());
	EXPECT_TRUE(aaahash==filetree[1]);
	HashTree bootstree(filetree.root);
	EXPECT_EQ( HashTree::DUNNO, bootstree.offer(filetree.peaks[0].first,filetree.peaks[0].second) );
	EXPECT_EQ( HashTree::PEAK_ACCEPT, bootstree.offer(filetree.peaks[1].first,filetree.peaks[1].second) );
	EXPECT_EQ( 3, bootstree.length );
	EXPECT_EQ( 4, bootstree.mass );
	EXPECT_EQ( HashTree::DUNNO, bootstree.offer(1,aaahash) );
	EXPECT_EQ( HashTree::ACCEPT, bootstree.offer(2,bbbhash) );
	EXPECT_TRUE ( bootstree.bits[3]==abhash );
	EXPECT_TRUE ( bootstree.bits[1]==aaahash );
	EXPECT_TRUE ( bootstree.bits[2]==bbbhash );
	EXPECT_FALSE ( bootstree.bits[2]==aaahash );
}*/


int main (int argc, char** argv) {
	//bin::init();

	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();

}
