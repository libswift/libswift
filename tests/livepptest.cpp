/*
 * livepptest.cpp
 *
 * Assumes SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN = 32
 *
 * Created by Arno Bakker
 * Copyright 2009-2016 Vrije Universiteit Amsterdam. All rights reserved.
 *
 */
#include <gtest/gtest.h>
#include "swift.h"

#include "ext/live_picker.cpp"

using namespace swift;

LiveTransfer *create_lt()
{
    std::string filename = "test.dat";
    SwarmID swarmid(std::string("05676767676767676767676767676767676767676767"));
    popt_cont_int_prot_t cipm = POPT_CONT_INT_PROT_NONE;
    uint64_t disc_wnd=POPT_LIVE_DISC_WND_ALL;
    uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE;
    Address addr = Address();
    LiveTransfer *lt = new LiveTransfer(filename,swarmid,addr,cipm,disc_wnd,chunk_size);
    return lt;
}

TEST(TLivePiecePicker,HookinFirst) {

    LiveTransfer *lt = create_lt();
    ASSERT_NE((LiveTransfer *)NULL,lt);
    SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

    bin_t munro_bin(5,481);
    tint  munro_tint(NOW + TINT_SEC);

    lpp->AddPeerMunro(munro_bin,munro_tint);

    bin_t hookin_bin = lpp->GetHookinPos();
    ASSERT_EQ(munro_bin.base_left(),hookin_bin);
}


TEST(TLivePiecePicker,ReHookinBitRateTooShort) {

    LiveTransfer *lt = create_lt();
    ASSERT_NE((LiveTransfer *)NULL,lt);
    SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

    bin_t munro_bin(5,481);
    tint  munro_tint(NOW + TINT_SEC);

    lpp->AddPeerMunro(munro_bin,munro_tint);

    bin_t hookin_bin = lpp->GetHookinPos();
    ASSERT_EQ(munro_bin.base_left(),hookin_bin);

    munro_bin = bin_t(5,481+1000);
    munro_tint = NOW + 5*TINT_SEC;   // must be smaller than LIVE_PP_MIN_BITRATE_MEASUREMENT_INTERVAL

    lpp->AddPeerMunro(munro_bin,munro_tint);
    tint current_tint = lpp->CalculateCurrentPosInTime(munro_bin);

    fprintf(stderr,"test: current time %s\n", tintstr(current_tint) );
}



TEST(TLivePiecePicker,ReHookinBitRateGood) {

    LiveTransfer *lt = create_lt();
    ASSERT_NE((LiveTransfer *)NULL,lt);
    SimpleLivePiecePicker *lpp = new SimpleLivePiecePicker(lt);

    bin_t munro_bin(5,481);
    tint  munro_tint(NOW + TINT_SEC);

    fprintf(stderr,"test: Add 1st munro\n");
    lpp->AddPeerMunro(munro_bin,munro_tint);

    bin_t hookin_bin = lpp->GetHookinPos();
    ASSERT_EQ(munro_bin.base_left(),hookin_bin);

    // Number of subtrees of nchunks_per_sig chunks added between 1st and 2nd munro
    int ntrees = 20;

    bin_t new_munro_bin = bin_t(5,481+ntrees);
    tint  new_munro_tint = NOW + TINT_SEC + ntrees*TINT_SEC;
    fprintf(stderr,"test: Add 2nd munro\n");
    lpp->AddPeerMunro(new_munro_bin,new_munro_tint);

    fprintf(stderr,"test: Verify bitrate\n");
    double gotbitrate = lpp->CalculateBitrate();

    double expbytes = ((ntrees*SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN)+SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN-1)*SWIFT_DEFAULT_CHUNK_SIZE;
    double expbitrate = expbytes / ntrees;
    ASSERT_EQ(expbitrate,gotbitrate);

    // If current_pos == munro_bin then current_time must be munro_tint
    fprintf(stderr,"test: Verify sane\n");
    tint got_current_tint = lpp->CalculateCurrentPosInTime(new_munro_bin);
    ASSERT_EQ(new_munro_tint,got_current_tint);

    // We've stalled downloading: Current pos is half
    bin_t exp_current_bin = bin_t(5,481+ntrees/2);
    tint  exp_current_tint = NOW + TINT_SEC + (ntrees/2)*TINT_SEC;

    got_current_tint = lpp->CalculateCurrentPosInTime(exp_current_bin);
    ASSERT_EQ(exp_current_tint,got_current_tint);

    fprintf(stderr,"test: current time %s\n", tintstr(got_current_tint) );
}



int main (int argc, char** argv) {

    swift::LibraryInit();
    testing::InitGoogleTest(&argc, argv);
    Channel::debug_file = stdout;
    int ret = RUN_ALL_TESTS();
    return ret;

}
