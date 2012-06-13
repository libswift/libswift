/*
 *  vod_picker.cpp
 *  swift
 *
 *  Created by Riccardo Petrocco.
 *  Copyright 2009-2012 Delft University of Technology. All rights reserved.
 *
 */

#ifndef VOD_PICKER_H
#define VOD_PICKER_H

#include "swift.h"
#include <cassert>


using namespace swift;

#define HIGHPRIORITYWINDOW 45000;	// initial high priority window in bin unit
#define MIDPRIORITYWINDOW 4; 		// proportion of the mid priority window compared to the high pri. one

/** Picks pieces in VoD fashion. The stream is divided in three priority
 *  sets based on the current playback position. In the high priority set
 *  bins are selected in order, while on the medium and low priority sets
 *  in a rarest fist fashion */
class VodPiecePicker : public PiecePicker {

    binmap_t        ack_hint_out_;
    tbqueue         hint_out_;
    FileTransfer*   transfer_;
    Availability* 	avail_;
    uint64_t        twist_;
    bin_t           range_;
    int				playback_pos_;		// playback position in KB
    int				high_pri_window_;
    bin_t           initseq_;			// Hack by Arno to avoid large hints at startup

public:

    VodPiecePicker (FileTransfer* file_to_pick_from) : ack_hint_out_(),
           transfer_(file_to_pick_from), twist_(0), range_(bin_t::ALL), initseq_(0,0)
    {
    	avail_ = &(transfer_->availability());
        binmap_t::copy(ack_hint_out_, *(transfer_->ack_out()));
        playback_pos_ = -1;
        high_pri_window_ = HIGHPRIORITYWINDOW;
    }

    virtual ~VodPiecePicker() {}

    HashTree * hashtree() {
        return transfer_->hashtree();
    }

    virtual void Randomize (uint64_t twist) {
        twist_ = twist;
    }

    virtual void LimitRange (bin_t range) {
        range_ = range;
    }


    bin_t getTopBin(bin_t bin, uint64_t start, uint64_t size)
    {
    	while (bin.parent().base_length() <= size && bin.parent().base_left() >= bin_t(start))
		{
			bin.to_parent();
		}
    	return bin;
    }


    bin_t pickUrgent (binmap_t& offer, uint64_t max_width, uint64_t size) {

    	bin_t curr = bin_t((playback_pos_+1)<<1); // the base bin will be indexed by the double of the value (bin(4) == bin(0,2))
    	bin_t hint = bin_t::NONE;
    	uint64_t examined = 0;
		binmap_t binmap;

    	// report the first bin we find
    	while (hint.is_none() && examined < size)
    	{
    		curr = getTopBin(curr, (playback_pos_+1)<<1, size-examined);
    		if (!ack_hint_out_.is_filled(curr))
    		{
    			binmap.fill(offer);
				binmap_t::copy(binmap, ack_hint_out_, curr);
				hint = binmap_t::find_complement(binmap, offer, twist_);
				binmap.clear();
    		}
    		examined += curr.base_length();
    		curr = bin_t(0, curr.base_right().layer_offset()+1 );
    	}

    	if (!hint.is_none())
    		while (hint.base_length()>max_width && !hint.is_base()) // Arno,2012-01-17: stop!
    	        hint.to_left();

    	return hint;
    }


    bin_t pickRarest (binmap_t& offer, uint64_t max_width, uint64_t start, uint64_t size) {

    	//fprintf(stderr,"%s #1 Picker -> choosing from mid/low priority \n",tintstr());
    	bin_t curr = bin_t(start<<1);
		bin_t hint = bin_t::NONE;
		uint64_t examined = 0;
		//uint64_t size = end-start;
		bin_t rarest_hint = bin_t::NONE;
		// TODO remove..
		binmap_t binmap;

		// TODO.. this is the dummy version... put some logic in deciding what to DL
		while (examined < size)
		{
			curr = getTopBin(curr, start<<1, size-examined);

			if (!ack_hint_out_.is_filled(curr))
			{
				// remove
				//binmap_t::copy(binmap, offer);
				//binmap.reset(curr);

				binmap.fill(offer);
				binmap_t::copy(binmap, ack_hint_out_, curr);
				//hint = binmap_t::find_complement(ack_hint_out_, offer, curr, twist_);
				hint = binmap_t::find_complement(binmap, offer, twist_);
				binmap.clear();

				if (!hint.is_none())
				{
					if (avail_->size())
					{
						rarest_hint = avail_->get(rarest_hint) < avail_->get(hint) ? rarest_hint : hint;
					}
					else
					{
						examined = size;
						rarest_hint = hint;
					}
				}
			}

			examined += curr.base_length();
			curr = bin_t(0, curr.base_right().layer_offset()+1 );

		}

		if (!rarest_hint.is_none())
		{
			if (avail_->size())
				rarest_hint = avail_->getRarest(rarest_hint, max_width);
			else
				while (rarest_hint.base_length()>max_width && !rarest_hint.is_base()) // Arno,2012-01-17: stop!
					rarest_hint.to_left();
		}

		return rarest_hint;
    }


    virtual bin_t Pick (binmap_t& offer, uint64_t max_width, tint expires)
    {
    	bin_t hint;
    	bool retry;
        char tmp[32];
        char set = 'X';	// TODO remove set var, only used for debug

    	// TODO check... the seconds should depend on previous speed of the peer
        while (hint_out_.size() && hint_out_.front().time<NOW-TINT_SEC*3/2) { // FIXME sec
            binmap_t::copy(ack_hint_out_, *(transfer_->ack_out()), hint_out_.front().bin);
            hint_out_.pop_front();
        }

        // get the first piece to estimate the size, whoever sends it first
        if (!hashtree()->size()) {
            return bin_t(0,0);
        }
        else if (hashtree()->ack_out()->is_empty(bin_t(0,0)))
        {
        	// Arno, 2012-05-03: big initial hint avoidance hack:
        	// Just ask sequentially till first chunk in.
        	initseq_ = bin_t(initseq_.layer(),initseq_.layer_offset()+1);
        	return initseq_;
        }

        do {
        	uint64_t max_size = hashtree()->size_in_chunks() - playback_pos_ - 1;
        	max_size = high_pri_window_ < max_size ? high_pri_window_ : max_size;

			// check the high priority window for data we r missing
			hint = pickUrgent(offer, max_width, max_size);

			// check the mid priority window
			uint64_t start = (1 + playback_pos_) + HIGHPRIORITYWINDOW;	// start in KB
			if (hint.is_none() && start < hashtree()->size_in_chunks())
			{
				int mid = MIDPRIORITYWINDOW;
				int size = mid * HIGHPRIORITYWINDOW;						// size of window in KB
				// check boundaries
				max_size = hashtree()->size_in_chunks() - start;
				max_size = size < max_size ? size : max_size;

				hint = pickRarest(offer, max_width, start, max_size);

				//check low priority
				start += max_size;
				if (hint.is_none() && start < hashtree()->size_in_chunks())
				{
					size = hashtree()->size_in_chunks() - start;
					hint = pickRarest(offer, max_width, start, size);
					set = 'L';
				}
				else
					set = 'M';
			}
			else
				set = 'H';

			// unhinted/late data
			if (!transfer_->ack_out()->is_empty(hint)) {
				binmap_t::copy(ack_hint_out_, *(transfer_->ack_out()), hint);
				retry = true;
			}
			else
				retry = false;

        } while (retry);


        if (hint.is_none()) {
        	// TODO, control if we want: check for missing hints (before playback pos.)
        	hint = binmap_t::find_complement(ack_hint_out_, offer, twist_);
        	// TODO: end-game mode
        	if (hint.is_none())
        		return hint;
        	else
				while (hint.base_length()>max_width && !hint.is_base()) // Arno,2012-01-17: stop!
					hint.to_left();


        }

        assert(ack_hint_out_.is_empty(hint));
        ack_hint_out_.set(hint);
        hint_out_.push_back(tintbin(NOW,hint));


        // TODO clean ... printing percentage of completeness for the priority sets
        //status();

        //fprintf(stderr,"%s #1 Picker -> picked %s\t from %c set\t max width %lu \n",tintstr(), hint.str(tmp), set, max_width );
        //if (avail_->size())
        return hint;
    }

    int Seek(bin_t offbin, int whence)
    {
    	char binstr[32];
    	fprintf(stderr,"vodpp: seek: %s whence %d\n", offbin.str(binstr), whence );

    	if (whence != SEEK_SET)
    		return -1;

    	// TODO: convert playback_pos_ to a bin number
    	uint64_t cid = offbin.toUInt()/2;
    	if (cid > 0)
    		cid--; // Riccardo assumes playbackpos is already in.

    	//fprintf(stderr,"vodpp: pos in K %llu size %llu\n", cid, hashtree()->size_in_chunks() );

    	if (cid > hashtree()->size_in_chunks())
    		return -1;

    	playback_pos_ = cid;
    	return 0;
    }


    // TODOGT: Remove for non live PP.

    void startAddPeerPos(uint32_t channelid, bin_t peerpos, bool peerissource)
    {
    }

    void endAddPeerPos(uint32_t channelid)
    {
    }


    bin_t getHookinPos()
    {
    	return bin_t(0,0);
    }


    bin_t getCurrentPos()
    {
    	// LIVETODO?
    	return bin_t::NONE;
    }

    void status()
	{
		int t = 0;
		int x = HIGHPRIORITYWINDOW;
		int y = MIDPRIORITYWINDOW;
		int i = playback_pos_ + 1;
		int end_high = (x+playback_pos_)<<1;
		int end_mid = ((x*y)+x+playback_pos_)<<1;
		int total = 0;


		while (i<=end_high)
		{
			if (!transfer_->ack_out()->is_empty(bin_t(i)))
				t++;
			i++;
		}
		total = t;
		t = t*100/((x<<1)-1);
		fprintf(stderr, "low %u, ", t);
		t = 0;
		while (i<=end_mid)
		{
			if (!transfer_->ack_out()->is_empty(bin_t(i)))
				t++;
			i++;
		}
		total += t;
		t = t*100/((x*y)<<1);
		fprintf(stderr, "mid %u, ", t);
		t = 0;
		while (i<=hashtree()->size_in_chunks()<<1)
		{
			if (!transfer_->ack_out()->is_empty(bin_t(i)))
				t++;
			i++;
		}
		total += t;
		t = t*100/((hashtree()->size_in_chunks()-(x*y+playback_pos_))<<1);
		fprintf(stderr, "low %u  -> in total: %i\t pp: %i\n", t, total, playback_pos_);
	}

};

#endif
