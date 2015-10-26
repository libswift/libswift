/*
 *  binfragtest.cpp
 *
 *  Tests for fragmenting a bin into smaller bins by removing some bins,
 *  needed for CANCEL message.
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"

#include <time.h>
#include <set>
#include <gtest/gtest.h>


using namespace swift;


/*
TEST(BinTest,Fragment1)
{
    bin_t hint(3,0);
    bin_t cancel(0,2);
    binvector bv = bin_fragment(hint,cancel);
    binvector::iterator iter;
    for (iter = bv.begin(); iter != bv.end(); iter++)
    {
    bin_t b = *iter;
    fprintf(stderr,"split %s\n", b.str().c_str() );
    }
}

TEST(BinTest,Fragment2)
{
    bin_t hint(3,0);
    bin_t cancel(1,1);
    binvector bv = bin_fragment(hint,cancel);
    binvector::iterator iter;
    for (iter = bv.begin(); iter != bv.end(); iter++)
    {
    bin_t b = *iter;
    fprintf(stderr,"split %s\n", b.str().c_str() );
    }
}


TEST(BinTest,Fragment3)
{
    bin_t hint(3,0);
    bin_t cancel(0,0);
    binvector bv = bin_fragment(hint,cancel);
    binvector::iterator iter;
    for (iter = bv.begin(); iter != bv.end(); iter++)
    {
    bin_t b = *iter;
    fprintf(stderr,"split %s\n", b.str().c_str() );
    }
}

TEST(BinTest,Fragment4)
{
    bin_t hint(3,0);
    bin_t cancel(0,7);
    binvector bv = bin_fragment(hint,cancel);
    binvector::iterator iter;
    for (iter = bv.begin(); iter != bv.end(); iter++)
    {
    bin_t b = *iter;
    fprintf(stderr,"split %s\n", b.str().c_str() );
    }
}
*/


char *fakestr()
{
    return "";
}

int id_ = 1;


TEST(BinTest,Fragment4)
{
    bin_t hintbin(7,0);
    tbqueue hint_in_;
    hint_in_.push_back(tintbin(0,hintbin));

    /*
    d.add( CancelMessage(ChunkRange(69,69)) )
    d.add( CancelMessage(ChunkRange(75,78)) )
    d.add( CancelMessage(ChunkRange(80,80)) )
    d.add( CancelMessage(ChunkRange(96,99)) )
    */
    binvector bv;
    swift::chunk32_to_bin32(69,69,&bv);
    swift::chunk32_to_bin32(75,78,&bv);
    swift::chunk32_to_bin32(80,80,&bv);
    swift::chunk32_to_bin32(96,99,&bv);

    binvector::iterator iter;
    for (iter=bv.begin(); iter != bv.end(); iter++) {
        bin_t cancelbin = *iter;
        fprintf(stderr,"%s #%" PRIu32 " -cancel MUST %s\n",fakestr(),id_,cancelbin.str().c_str());

        // Remove hint from hint_in_ if contained in cancelbin. Use Riccardo's solution:
        int hi = 0;
        while (hi<hint_in_.size() && !cancelbin.contains(hint_in_[hi].bin) && cancelbin != hint_in_[hi].bin)
            hi++;

        // something to cancel?
        if (hi != hint_in_.size()) {
            // Assumption: all fragments of a bin being cancelled consecutive in hint_in_
            do {
                fprintf(stderr,"%s #%" PRIu32 " -cancel frag %s\n",fakestr(),id_,hint_in_[hi].bin.str().c_str());
                hint_in_.erase(hint_in_.begin()+hi);
                if (hint_in_.size() == 0)
                    break;
            } while (cancelbin.contains(hint_in_[hi].bin));
        }

        id_ = 2;

        fprintf(stderr,"%s #%" PRIu32 " -cancel MUST %s len %d\n",fakestr(),id_,cancelbin.str().c_str(), hint_in_.size());

        // Fragment hint from hint_in_ if it covers cancelbin. Use Riccardo's solution:
        hi = 0;
        while (hi<hint_in_.size() && !hint_in_[hi].bin.contains(cancelbin))
            hi++;

        // nothing to cancel
        if (hi==hint_in_.size())
            continue;

        // Split up hint
        tint origt = hint_in_[hi].time;
        bin_t origbin = hint_in_[hi].bin;
        binvector fragbins = swift::bin_fragment(origbin,cancelbin);
        // Erase original
        hint_in_.erase(hint_in_.begin()+hi);
        // Replace with fragments left
        binvector::iterator iter2;
        int idx=0;
        for (iter2=fragbins.begin(); iter2 != fragbins.end(); iter2++) {
            bin_t fragbin = *iter2;
            fprintf(stderr,"%s #%" PRIu32 " -cancel keep %s\n",fakestr(),id_,fragbin.str().c_str());
            tintbin newtb(origt,fragbin);
            hint_in_.insert(hint_in_.begin()+hi+idx,newtb);
            idx++;
        }
    }

    for (int i=0; i<hint_in_.size(); i++) {
        tintbin tb = hint_in_[i];
        fprintf(stderr,"LEFT %s ", tb.bin.str().c_str());
    }
    fprintf(stderr,"\n");
}


int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
