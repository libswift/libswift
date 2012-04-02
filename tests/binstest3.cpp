/*
 *  binstest3.cpp
 *  serp++
 *
 *  Created by Victor Grishchenko on 3/22/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 *  ==========================
 *  Extended by Arno and Riccardo to hunt a bug in find_complement() with
 *  a range parameter.
 */
#include "binmap.h"
#include "binheap.h"

#include <time.h>
#include <set>
#include <gtest/gtest.h>


using namespace swift;


TEST(BinsTest,FindFiltered) {
    
    binmap_t data, filter;
    data.set(bin_t(2,0));
    data.set(bin_t(2,2));
    data.set(bin_t(1,7));
    filter.set(bin_t(4,0));
    filter.reset(bin_t(2,1));
    filter.reset(bin_t(1,4));
    filter.reset(bin_t(0,13));
    
    bin_t x = binmap_t::find_complement(data, filter, bin_t(4,0), 0);
    EXPECT_EQ(bin_t(0,12),x);
    
}

TEST(BinsTest,FindFiltered1b) {

    binmap_t data, filter;
    data.set(bin_t(2,0));
    data.set(bin_t(2,2));
    data.set(bin_t(1,7));
    filter.set(bin_t(4,0));
    filter.reset(bin_t(2,1));
    filter.reset(bin_t(1,4));
    filter.reset(bin_t(0,13));

    char binstr[32];

    bin_t s = bin_t(3,1);
    fprintf(stderr,"Searching 0,12 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    bin_t x = binmap_t::find_complement(data, filter, s, 0);
    EXPECT_EQ(bin_t(0,12),x);

}


TEST(BinsTest,FindFiltered1c) {

    binmap_t data, filter;
    data.set(bin_t(2,0));
    data.set(bin_t(2,2));
    data.set(bin_t(1,7));

    filter.set(bin_t(4,0));
    filter.reset(bin_t(2,1));
    filter.reset(bin_t(1,4));
    //filter.reset(bin_t(0,13));

    char binstr[32];

    bin_t s = bin_t(3,1);
    fprintf(stderr,"Searching 0,12x from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    bin_t x = binmap_t::find_complement(data, filter, s, 0);
    EXPECT_EQ(bin_t(0,12),x);

}


TEST(BinsTest,FindFiltered2) {
    
    binmap_t data, filter;
    for(int i=0; i<1024; i+=2)
        data.set(bin_t(0,i));
    for(int j=0; j<1024; j+=2)
        filter.set(bin_t(0,j));

    fprintf(stderr,"test: width %d\n", filter.cells_number() );
    fprintf(stderr,"test: empty %llu\n", filter.find_empty().toUInt() );


    data.reset(bin_t(0,500));
    EXPECT_EQ(bin_t(0,500),binmap_t::find_complement(data, filter, bin_t(10,0), 0).base_left());
    data.set(bin_t(0,500));
    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(10,0), 0).base_left());
    
}


// Range is strict subtree
TEST(BinsTest,FindFiltered3) {

    binmap_t data, filter;
    for(int i=0; i<1024; i+=2)
        data.set(bin_t(0,i));
    for(int j=0; j<1024; j+=2)
        filter.set(bin_t(0,j));
    data.reset(bin_t(0,500));
    EXPECT_EQ(bin_t(0,500),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
    data.set(bin_t(0,500));
    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());

}

// 1036 leaf tree

TEST(BinsTest,FindFiltered4) {

    binmap_t data, filter;
    for(int i=0; i<1036; i+=2)
        data.set(bin_t(0,i));
    for(int j=0; j<1036; j+=2)
        filter.set(bin_t(0,j));
    data.reset(bin_t(0,500));
    EXPECT_EQ(bin_t(0,500),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
    data.set(bin_t(0,500));
    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());

}

// Make 8 bin hole in 1036 tree

TEST(BinsTest,FindFiltered5) {

    binmap_t data, filter;
    for(int i=0; i<1036; i++) //completely full
        data.set(bin_t(0,i));
    for(int j=0; j<1036; j++)
        filter.set(bin_t(0,j));

    for (int j=496; j<=503; j++)
    	data.reset(bin_t(0,j));

    EXPECT_EQ(bin_t(3,62),binmap_t::find_complement(data, filter, bin_t(9,0), 0) );
    EXPECT_EQ(bin_t(0,496),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
}


// Use simple example tree from RFC
TEST(BinsTest,FindFiltered6) {

    binmap_t data, filter;
    for(int i=0; i<14; i+=2)  //completely full example tree
        data.set(bin_t(i));
    for(int j=0; j<14; j+=2)
        filter.set(bin_t(j));

    for (int j=4; j<=6; j+=2) // reset leaves 4 and 6 (int)
    	data.reset(bin_t(j));

    EXPECT_EQ(bin_t(1,1),binmap_t::find_complement(data, filter, bin_t(2,0), 0) );
    EXPECT_EQ(bin_t(0,2),binmap_t::find_complement(data, filter, bin_t(2,0), 0).base_left());
}


// diff in right tree, range is left tree
TEST(BinsTest,FindFiltered7) {

    binmap_t data, filter;
    for(int i=0; i<14; i+=2)  //completely full example tree
        data.set(bin_t(i));
    data.reset(bin_t(4));	  // clear 4
    for(int j=0; j<14; j+=2)
        filter.set(bin_t(j));
    filter.reset(bin_t(4));

    for (int j=8; j<=10; j+=2)	// make diff out of range
    	data.reset(bin_t(j));

    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(2,0), 0) );
    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(2,0), 0).base_left());
}



// diff in left tree, range is right tree
TEST(BinsTest,FindFiltered8) {

    binmap_t data, filter;
    for(int i=0; i<14; i+=2)  //completely full example tree
        data.set(bin_t(i));
    data.reset(bin_t(4));	  // clear 4
    for(int j=0; j<14; j+=2)
        filter.set(bin_t(j));
    filter.reset(bin_t(4));

    for (int j=4; j<=6; j+=2)	// make diff out of range
    	data.reset(bin_t(j));

    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(2,1), 0) );
    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(2,1), 0).base_left());
}


// reverse empty/full
TEST(BinsTest,FindFiltered9) {

    binmap_t data, filter;
    for(int i=0; i<14; i+=2)  //completely empty example tree
        data.reset(bin_t(i));
    data.set(bin_t(4));	  // clear 4
    for(int j=0; j<14; j+=2)
        filter.reset(bin_t(j));
    filter.set(bin_t(4));

    for (int j=4; j<=6; j+=2)	// make diff out of range
    	data.set(bin_t(j));

    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(2,1), 0) );
    EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(2,1), 0).base_left());
}


// Make 8 bin hole in 999 tree, left subtree

TEST(BinsTest,FindFiltered10) {

    binmap_t data, filter;
    for(int i=0; i<999; i++) //completely full
        data.set(bin_t(0,i));
    for(int j=0; j<999; j++)
        filter.set(bin_t(0,j));

    for (int j=496; j<=503; j++)
    	data.reset(bin_t(0,j));

    EXPECT_EQ(bin_t(3,62),binmap_t::find_complement(data, filter, bin_t(9,0), 0) );
    EXPECT_EQ(bin_t(0,496),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
}


// Make 8 bin hole in 999 tree, right subtree, does not start a 8-bin substree
TEST(BinsTest,FindFiltered11) {

    binmap_t data, filter;
    for(int i=0; i<999; i++) //completely full
        data.set(bin_t(0,i));
    for(int j=0; j<999; j++)
        filter.set(bin_t(0,j));

    for (int j=514; j<=521; j++)
    	data.reset(bin_t(0,j));

    EXPECT_EQ(bin_t(1,257),binmap_t::find_complement(data, filter, bin_t(9,1), 0) );
    EXPECT_EQ(bin_t(0,514),binmap_t::find_complement(data, filter, bin_t(9,1), 0).base_left());
}

// Make 8 bin hole in 999 tree, move hole
TEST(BinsTest,FindFiltered12) {

    binmap_t data, filter;
    for(int i=0; i<999; i++) //completely full
        data.set(bin_t(0,i));
    for(int j=0; j<999; j++)
        filter.set(bin_t(0,j));

    for (int x=0; x<999-8; x++)
    {
    	fprintf(stderr,"x%u ", x);
    	for (int j=x; j<=x+7; j++)
    		data.reset(bin_t(0,j));

    	int subtree = (x <= 511) ? 0 : 1;
    	EXPECT_EQ(bin_t(0,x),binmap_t::find_complement(data, filter, bin_t(9,subtree), 0).base_left());

    	// Restore
    	for (int j=x; j<=x+7; j++) {
    		data.set(bin_t(0,j));
    	}
    }
}


// Make 8 bin hole in sparse 999 tree, move hole
TEST(BinsTest,FindFiltered13) {

    binmap_t data, filter;
    for(int i=0; i<999; i+=2) // sparse
        data.set(bin_t(0,i));
    for(int j=0; j<999; j+=2)
        filter.set(bin_t(0,j));

    for (int x=0; x<999-8; x++)
    {
    	fprintf(stderr,"x%u ", x);
    	for (int j=x; j<=x+7; j++)
    		data.reset(bin_t(0,j));

    	int y = (x % 2) ? x+1 : x;
    	int subtree = (x <= 511) ? 0 : 1;
    	if (x < 511)
    		EXPECT_EQ(bin_t(0,y),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
    	else if (x == 511) // sparse bitmap 101010101..., so actual diff in next subtree
    		EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
    	else
    		EXPECT_EQ(bin_t(0,y),binmap_t::find_complement(data, filter, bin_t(9,1), 0).base_left());


        for(int i=0; i<999; i+=2) // sparse
            data.set(bin_t(0,i));
    }
}


// Make 8 bin hole in sparse 999 tree, move hole
TEST(BinsTest,FindFiltered14) {

    binmap_t data, filter;
    for(int i=0; i<999; i+=2) // sparse
        data.set(bin_t(0,i));
    for(int j=0; j<999; j+=2)
        filter.set(bin_t(0,j));

    // Add other diff
    filter.set(bin_t(0,995));

    for (int x=0; x<999-8; x++)
    {
    	fprintf(stderr,"x%u ", x);
    	for (int j=x; j<=x+7; j++)
    		data.reset(bin_t(0,j));

    	int y = (x % 2) ? x+1 : x;
    	int subtree = (x <= 511) ? 0 : 1;
    	if (x < 511)
    		EXPECT_EQ(bin_t(0,y),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
    	else if (x == 511) // sparse bitmap 101010101..., so actual diff in next subtree
    		EXPECT_EQ(bin_t::NONE,binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
    	else
    		EXPECT_EQ(bin_t(0,y),binmap_t::find_complement(data, filter, bin_t(9,1), 0).base_left());


        for(int i=0; i<999; i+=2) // sparse
            data.set(bin_t(0,i));
    }
}



// Make holes at 292, problematic in a specific experiment
TEST(BinsTest,FindFiltered15) {

    binmap_t data, filter;
    for(int i=0; i<999; i++) // completely full
        data.set(bin_t(0,i));
    for(int j=0; j<999; j++)
        filter.set(bin_t(0,j));

    data.reset(bin_t(292));
    data.reset(bin_t(296));
    data.reset(bin_t(514));
    data.reset(bin_t(998));

	EXPECT_EQ(bin_t(292),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
}



// VOD like. Make first hole at 292.
TEST(BinsTest,FindFiltered16) {

    binmap_t data, filter;
    for(int i=0; i<292/2; i++) // prefix full
        data.set(bin_t(0,i));
    for(int i=147; i<999; i+=21) // postfix sparse
    {
    	for (int x=0; x<8; x++)
    		data.set(bin_t(0,i+x));
    }

    for(int j=0; j<999; j++)
        filter.set(bin_t(0,j));

	EXPECT_EQ(bin_t(292),binmap_t::find_complement(data, filter, bin_t(9,0), 0).base_left());
}


// VOD like. Make first hole at 292.
TEST(BinsTest,FindFiltered17) {

    binmap_t offer, ack_hint_out;
    for(int i=0; i<999; i++) // offer completely full
        offer.set(bin_t(0,i));

    for(int i=0; i<292/2; i++) // request prefix full
        ack_hint_out.set(bin_t(0,i));
    for(int i=147; i<999; i+=21) // request postfix sparse
    {
    	for (int x=0; x<8; x++)
    		ack_hint_out.set(bin_t(0,i+x));
    }

	binmap_t binmap;

	// report the first bin we find
	int layer = 0;
	bin_t::uint_t twist = 0;
	bin_t hint = bin_t::NONE;
	while (hint.is_none() && layer <10)
	{
		char binstr[32];

		bin_t curr = bin_t(layer++,0);
		binmap.fill(offer);
		binmap_t::copy(binmap, ack_hint_out, curr);
		hint = binmap_t::find_complement(binmap, offer, twist);
		binmap.clear();
	}

	EXPECT_EQ(bin_t(292),hint);
}


// VOD like. Make first hole at 292. Twisting + patching holes
TEST(BinsTest,FindFiltered19) {

    binmap_t offer, ack_hint_out;
    for(int i=0; i<999; i++) // offer completely full
        offer.set(bin_t(0,i));

    for(int i=0; i<292/2; i++) // request prefix full
        ack_hint_out.set(bin_t(0,i));
    for(int i=147; i<999; i+=21) // request postfix sparse
    {
    	for (int x=0; x<8; x++)
    		ack_hint_out.set(bin_t(0,i+x));
    }

	binmap_t binmap;

	int layer = 0;
	bin_t::uint_t twist = 0;
	bin_t hint = bin_t::NONE;
	while (!hint.contains(bin_t(292)))
	{
		char binstr[32];

		twist = rand();

		bin_t curr = bin_t(layer,0);
		if (layer < 10)
			layer++;

		binmap.fill(offer);
		binmap_t::copy(binmap, ack_hint_out, curr);
		hint = binmap_t::find_complement(binmap, offer, twist);

		if (!hint.is_none())
			fprintf(stderr,"Found alt ");
		binmap.clear();

		//patch hole
		ack_hint_out.set(hint);
	}

	char binstr[32],binstr2[32];
	EXPECT_EQ(bin_t(292),hint);
}


void create_ack_hint_out(binmap_t &ack_hint_out)
{
	ack_hint_out.clear();
    for(int i=0; i<292/2; i++) // request prefix full
        ack_hint_out.set(bin_t(0,i));
    for(int i=147; i<999; i+=21) // request postfix sparse
    {
    	for (int x=0; x<8; x++)
    		ack_hint_out.set(bin_t(0,i+x));
    }
}



// VOD like. Make first hole at 292. Twisting + patching holes. Stalled
// at Playbackpos, looking increasingly higher layers.
TEST(BinsTest,FindFiltered20) {

    binmap_t offer, ack_hint_out;
    for(int i=0; i<999; i++) // offer completely full
        offer.set(bin_t(0,i));

    create_ack_hint_out(ack_hint_out);

	binmap_t binmap;

	int layer = 0;
	bin_t::uint_t twist = 0;
	bin_t hint = bin_t::NONE;

	for (layer=0; layer<=9; layer++)
	{
		fprintf(stderr,"Layer %d\n", layer );
		while (!hint.contains(bin_t(292)))
		{
			char binstr[32];

			twist = rand();

			bin_t curr = bin_t(0,292/2);
			for (int p=0; p<layer; p++)
				curr = curr.parent();

			binmap.fill(offer);
			binmap_t::copy(binmap, ack_hint_out, curr);
			hint = binmap_t::find_complement(binmap, offer, twist);

			if (!hint.is_none())
				fprintf(stderr,"Found alt %s ", hint.str(binstr) );
			binmap.clear();

			//patch hole
			ack_hint_out.set(hint);
		}
		create_ack_hint_out(ack_hint_out);
	}

	EXPECT_EQ(bin_t(292),hint);
}


void DoFindFilteredRiccardo(bin_t::uint_t twist)
{
    binmap_t data, filter;

    for(int i=0; i<1024; i+=2)
        filter.set(bin_t(0,i));

    char binstr[32];

    // Case 1
    bin_t s(1,2);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    s = bin_t(2,1);
    fprintf(stderr,"Searching 0,6 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    bin_t got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,6),got);
    EXPECT_EQ(true,s.contains(got));


    // Case 2
    s = bin_t(1,8);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    s = bin_t(2,4);
    fprintf(stderr,"Searching 0,18 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,18),got);
    EXPECT_EQ(true,s.contains(got));


    // Case 5
    s = bin_t(1,80);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    s = bin_t(2,40);
    fprintf(stderr,"Searching 0,162 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,162),got);
    EXPECT_EQ(true,s.contains(got));

    // Case 6

    s = bin_t(1,84);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    s = bin_t(2,42);
    fprintf(stderr,"Searching 0,168 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,170),got);
    EXPECT_EQ(true,s.contains(got));


    // Case 3
    s = bin_t(1,86);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    s = bin_t(2,43);
    fprintf(stderr,"Searching 0,174 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,174),got);
    EXPECT_EQ(true,s.contains(got));


    // Case 7
    s = bin_t(1,90);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );


    s = bin_t(2,45);
    fprintf(stderr,"Searching 0,182 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,182),got);
    EXPECT_EQ(true,s.contains(got));


    // Case 4
    s = bin_t(1,92);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    s = bin_t(2,46);
    fprintf(stderr,"Searching 0,184 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,186),got);
    EXPECT_EQ(true,s.contains(got));


    // Case 8
    s = bin_t(1,94);
    data.set(s);

    fprintf(stderr,"Setting from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    s = bin_t(2,47);
    fprintf(stderr,"Searching 0,188 from %s ", s.base_left().str(binstr ) );
    fprintf(stderr,"to %s\n", s.base_right().str(binstr ) );

    got = binmap_t::find_complement(data, filter, s, twist).base_left();
    EXPECT_EQ(bin_t(0,190),got);
    EXPECT_EQ(true,s.contains(got));
}


TEST(BinsTest,FindFilteredRiccardo3) {

	DoFindFilteredRiccardo(0);
}


TEST(BinsTest,FindFilteredRiccardo3Twist) {

	DoFindFilteredRiccardo( rand() );
}






int main (int argc, char** argv) {
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
