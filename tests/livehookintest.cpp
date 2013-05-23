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

#define SMALL_DISC_WINDOW	20
#define FROM_PEAKS	true

using namespace swift;


/* Set */

void TemplHookinNoSource2Peers(uint64_t disc_wnd)
{
	std::string filename="bla.dat";
	pubkey_t pubkey;
	LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,disc_wnd,1024);

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
	ASSERT_EQ(bin_t(0,103).toUInt(),lpp->GetHookinPos().toUInt());
}


TEST(LiveHookinTest,HookinNoSource2PeersST)
{
    TemplHookinNoSource2Peers(SMALL_DISC_WINDOW);
}

TEST(LiveHookinTest,HookinNoSource2PeersFT)
{
    TemplHookinNoSource2Peers(POPT_LIVE_DISC_WND_ALL);
}



/* Set */

void TemplHookinNoSource2PeersSame(uint64_t disc_wnd)
{
    std::string filename="bla.dat";
    pubkey_t pubkey;
    LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,disc_wnd,1024);

    SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

    for (int i=0; i<4; i++)
    {
	 lpp->StartAddPeerPos(1, bin_t(0,100+i), false);
	 lpp->StartAddPeerPos(2, bin_t(0,100+i), false);
    }

    lpp->EndAddPeerPos(2);
    ASSERT_FALSE(lpp->GetSearch4Hookin());
    ASSERT_EQ(bin_t(0,103).toUInt(),lpp->GetHookinPos().toUInt());
}

TEST(LiveHookinTest,HookinNoSource2PeersSameST)
{
    TemplHookinNoSource2PeersSame(SMALL_DISC_WINDOW);
}


TEST(LiveHookinTest,HookinNoSource2PeersSameFT)
{
    TemplHookinNoSource2PeersSame(POPT_LIVE_DISC_WND_ALL);
}


/* Set */


void TemplHookinNoSource8Peers(uint64_t disc_wnd)
{
    std::string filename="bla.dat";
    pubkey_t pubkey;
    LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,disc_wnd,1024);

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
    ASSERT_EQ(bin_t(0,109).toUInt(),lpp->GetHookinPos().toUInt());
}


TEST(LiveHookinTest,HookinNoSource8PeersST)
{
    TemplHookinNoSource8Peers(SMALL_DISC_WINDOW);
}

TEST(LiveHookinTest,HookinNoSource8PeersFT)
{
    TemplHookinNoSource8Peers(POPT_LIVE_DISC_WND_ALL);
}



/* Set */

void TemplHookinNoSource8Good1Bad(uint64_t disc_wnd)
{
    std::string filename="bla.dat";
    pubkey_t pubkey;
    LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,disc_wnd,1024);

    SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

    int c=0;
    for (c=1; c<8; c++)
    {
	for (int i=0; i<4; i++)
	{
	    lpp->StartAddPeerPos(c, bin_t(0,100+c+i), false);
	}
    }
    // Add 1 ahead
    for (int i=0; i<4; i++)
    {
	lpp->StartAddPeerPos(c+1, bin_t(0,200+c+i), false);
    }

    lpp->EndAddPeerPos(1);

    ASSERT_FALSE(lpp->GetSearch4Hookin());
    if (disc_wnd == SMALL_DISC_WINDOW)
	ASSERT_EQ(bin_t(0,109).toUInt(),lpp->GetHookinPos().toUInt());
    else
	ASSERT_EQ(bin_t(0,110).toUInt(),lpp->GetHookinPos().toUInt());
}

TEST(LiveHookinTest,HookinNoSource8Good1BadST)
{
    TemplHookinNoSource8Good1Bad(SMALL_DISC_WINDOW);
}

TEST(LiveHookinTest,HookinNoSource8Good1BadFT)
{
    TemplHookinNoSource8Good1Bad(POPT_LIVE_DISC_WND_ALL);
}


/* Set */

void TemplHookinNoSource8Old2Good(uint64_t disc_wnd)
{
    std::string filename="bla.dat";
    pubkey_t pubkey;
    LiveTransfer *lt = new LiveTransfer(filename,pubkey,false,disc_wnd,1024);

    SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

    int c=0;
    // 8 old
    for (c=1; c<8; c++)
    {
	for (int i=0; i<4; i++)
	{
	    lpp->StartAddPeerPos(c, bin_t(0,100+c+i), false);
	}
    }
    // 1 new
    for (int i=0; i<4; i++)
    {
	lpp->StartAddPeerPos(c+1, bin_t(0,200+c+i), false);
    }
    // 2 new
    for (int i=0; i<4; i++)
    {
	lpp->StartAddPeerPos(c+2, bin_t(0,201+c+i), false);
    }


    lpp->EndAddPeerPos(1);

    ASSERT_FALSE(lpp->GetSearch4Hookin());
    ASSERT_EQ(bin_t(0,211).toUInt(),lpp->GetHookinPos().toUInt());
}


TEST(LiveHookinTest,HookinNoSource8Old2GoodST)
{
    TemplHookinNoSource8Old2Good(SMALL_DISC_WINDOW);
}

TEST(LiveHookinTest,HookinNoSource8Old2GoodFT)
{
    TemplHookinNoSource8Old2Good(POPT_LIVE_DISC_WND_ALL);
}


int main (int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
