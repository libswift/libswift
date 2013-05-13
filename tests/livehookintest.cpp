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


using namespace swift;


TEST(LiveHookinTest,HookinNoSource2Peers)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	for (int i=0; i<4; i++)
	{
	     lpp->StartAddPeerPos(1, bin_t(0,100+i), false);
	}

	lpp->EndAddPeerPos(1);
	ASSERT_TRUE(lpp->GetSearch4Hookin());

	for (int i=0; i<4; i++)
	{
	     lpp->StartAddPeerPos(2, bin_t(0,101+i), false);
	}

	lpp->EndAddPeerPos(2);
	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(lpp->GetHookinPos().toUInt(),bin_t(0,103).toUInt());
}


TEST(LiveHookinTest,HookinNoSource2PeersSame)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	for (int i=0; i<4; i++)
	{
	     lpp->StartAddPeerPos(1, bin_t(0,100+i), false);
	     lpp->StartAddPeerPos(2, bin_t(0,100+i), false);
	}

	lpp->EndAddPeerPos(2);
	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(lpp->GetHookinPos().toUInt(),bin_t(0,103).toUInt());
}


TEST(LiveHookinTest,HookinNoSource8Peers)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	for (int c=1; c<8; c++)
	{
   	    for (int i=0; i<4; i++)
	    {
	        lpp->StartAddPeerPos(c, bin_t(0,100+c+i), false);
	    }
	}
	lpp->EndAddPeerPos(1);

	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(lpp->GetHookinPos().toUInt(),bin_t(0,109).toUInt());
}



TEST(LiveHookinTest,HookinNoSource8Good1Bad)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,1024);

	SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

	int c=0;
	for (c=1; c<8; c++)
	{
   	    for (int i=0; i<4; i++)
	    {
	        lpp->StartAddPeerPos(c, bin_t(0,100+c+i), false);
	    }
	}
	for (int i=0; i<4; i++)
	{
	    lpp->StartAddPeerPos(c+1, bin_t(0,200+c+i), false);
	}

	lpp->EndAddPeerPos(1);

	ASSERT_FALSE(lpp->GetSearch4Hookin());
	ASSERT_EQ(lpp->GetHookinPos().toUInt(),bin_t(0,110).toUInt());
}




int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
