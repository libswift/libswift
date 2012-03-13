/*
 *  freemap.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 3/22/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <time.h>
#include <gtest/gtest.h>
#include <set>
#include "binmap.h"

using namespace swift;

#ifdef _MSC_VER
	#define RANDOM  rand
#else
	#define RANDOM	random
#endif

uint8_t rand_norm (uint8_t lim) {
    long rnd = RANDOM() & ((1<<lim)-1);
    uint8_t bits = 0;
    while (rnd) {
        bits += rnd&1;
        rnd >>= 1;
    }
    return bits;
}

TEST(FreemapTest,Freemap) {
    binmap_t space;
    const bin_t top(30,0);
    space.reset(top);
    typedef std::pair<int,bin_t> timebin_t;
    typedef std::set<timebin_t> ts_t;
    ts_t to_free;
    for (int t=0; t<1000000; t++) {

    	if ((t % 1000) == 0)
    		printf(".");

        if (t<500000 || t>504000) {
            uint8_t lr = rand_norm(28);
            bin_t alloc = space.find_empty();
            while (alloc.layer()>lr)
                alloc = alloc.left();
            ASSERT_NE(0ULL,~alloc.toUInt());
            EXPECT_TRUE(space.is_empty(alloc));
            space.set(alloc);
            long dealloc_time = 1<<rand_norm(22);
#ifdef SHOWPUTPUT
            printf("alloc 2**%i starting at %lli for %li ticks\n",
                (int)lr,alloc.toUInt(),dealloc_time);
#endif
            dealloc_time += t;
            to_free.insert(timebin_t(dealloc_time,alloc));
        }
        // now, the red-black tree
        while (to_free.begin()->first<=t) {
            bin_t freebin = to_free.begin()->second;
            to_free.erase(to_free.begin());
            space.reset(freebin);
#ifdef SHOWOUTPUT
            printf("freed at %lli\n",
                freebin.toUInt());
#endif
       }
        // log: space taken, gaps, binmap cells, tree cells
        int cells = space.cells_number();

#ifdef SHOWOUTPUT
        printf("time %i cells used %i blocks %i\n",
                t,cells,(int)to_free.size());
#endif
        //space.dump("space");
    }
    for(ts_t::iterator i=to_free.begin(); i!=to_free.end(); i++)
        space.reset(i->second);
    EXPECT_TRUE(space.is_empty(top));
}

int main (int argc, char** argv) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
