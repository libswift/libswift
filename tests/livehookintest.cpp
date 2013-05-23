/*
 *  livehookintest.cpp
 *
 *  Test for SimpleLivePiecePicker
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include "../ext/live_picker.cpp"

#include <gtest/gtest.h>

#define DISC_WINDOW	20
#define FROM_PEAKS	true

using namespace swift;


TEST(LiveHookinTest,HookinNoSource2Peers)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,DISC_WINDOW,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	for (int i=0; i<4; i++)
	{
	     lpp->StartAddPeerPos(1, bin_t(0,100+i), false, FROM_PEAKS);
	}

	lpp->EndAddPeerPos(1);
	ASSERT_TRUE(lpp->GetSearch4Hookin());

	for (int i=0; i<4; i++)
	{
	     lpp->StartAddPeerPos(2, bin_t(0,101+i), false, FROM_PEAKS);
	}

	lpp->EndAddPeerPos(2);
	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(bin_t(0,103).toUInt(),lpp->GetHookinPos().toUInt());
}


TEST(LiveHookinTest,HookinNoSource2PeersSame)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,DISC_WINDOW,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	for (int i=0; i<4; i++)
	{
	     lpp->StartAddPeerPos(1, bin_t(0,100+i), false, FROM_PEAKS);
	     lpp->StartAddPeerPos(2, bin_t(0,100+i), false, FROM_PEAKS);
	}

	lpp->EndAddPeerPos(2);
	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(bin_t(0,103).toUInt(),lpp->GetHookinPos().toUInt());
}


TEST(LiveHookinTest,HookinNoSource8Peers)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,DISC_WINDOW,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	for (int c=1; c<8; c++)
	{
   	    for (int i=0; i<4; i++)
	    {
	        lpp->StartAddPeerPos(c, bin_t(0,100+c+i), false, FROM_PEAKS);
	    }
	}
	lpp->EndAddPeerPos(1);

	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(bin_t(0,109).toUInt(),lpp->GetHookinPos().toUInt());
}



TEST(LiveHookinTest,HookinNoSource8Good1Bad)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,DISC_WINDOW,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	int c=0;
	for (c=1; c<8; c++)
	{
   	    for (int i=0; i<4; i++)
	    {
	        lpp->StartAddPeerPos(c, bin_t(0,100+c+i), false, FROM_PEAKS);
	    }
	}
	for (int i=0; i<4; i++)
	{
	    lpp->StartAddPeerPos(c+1, bin_t(0,200+c+i), false, FROM_PEAKS);
	}

	lpp->EndAddPeerPos(1);

	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(bin_t(0,109).toUInt(),lpp->GetHookinPos().toUInt());
}


TEST(LiveHookinTest,HookinNoSource8Old2Good)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,DISC_WINDOW,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	int c=0;
	// 8 old
	for (c=1; c<8; c++)
	{
   	    for (int i=0; i<4; i++)
	    {
	        lpp->StartAddPeerPos(c, bin_t(0,100+c+i), false, FROM_PEAKS);
	    }
	}
	// 1 new
	for (int i=0; i<4; i++)
	{
	    lpp->StartAddPeerPos(c+1, bin_t(0,200+c+i), false, FROM_PEAKS);
	}
	// 2 new
	for (int i=0; i<4; i++)
	{
	    lpp->StartAddPeerPos(c+2, bin_t(0,201+c+i), false, FROM_PEAKS);
	}


	lpp->EndAddPeerPos(1);

	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(bin_t(0,211).toUInt(),lpp->GetHookinPos().toUInt());
}




int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
