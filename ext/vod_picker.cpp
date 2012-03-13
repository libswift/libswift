/*
 *  vod_picker.cpp
 *  swift
 *
 *  Created by Riccardo Petrocco.
 *  Copyright 2009-2012 Delft University of Technology. All rights reserved.
 *
 */

#include "swift.h"
#include <cassert>

using namespace swift;

#define HIGHPRIORITYWINDOW 360;	// initial high priority window in bin unit
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

public:

    VodPiecePicker (FileTransfer* file_to_pick_from) : ack_hint_out_(),
           transfer_(file_to_pick_from), twist_(0), range_(bin_t::ALL)
    {
    	avail_ = &(transfer_->availability());
        binmap_t::copy(ack_hint_out_, file().ack_out());
        playback_pos_ = -1;
        high_pri_window_ = HIGHPRIORITYWINDOW;
    }

    virtual ~VodPiecePicker() {}

    HashTree& file() {
        return transfer_->file();
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


    bin_t pickUrgent (binmap_t& offer, uint64_t max_width) {

    	bin_t curr = bin_t((playback_pos_+1)<<1); // the base bin will be indexed by the double of the value (bin(4) == bin(0,2))
    	bin_t hint = bin_t::NONE;
    	uint64_t examined = 0;
		binmap_t binmap;

    	// report the first bin we find
    	while (hint.is_none() && examined < high_pri_window_)
    	{
    		curr = getTopBin(curr, (playback_pos_+1)<<1, high_pri_window_-examined);
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
    		while (hint.base_length()>max_width)
    	        hint = hint.left();

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
				while (rarest_hint.base_length()>max_width)
					rarest_hint = rarest_hint.left();
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
            binmap_t::copy(ack_hint_out_, file().ack_out(), hint_out_.front().bin);
            hint_out_.pop_front();
        }

        // get the first piece to estimate the size, whoever sends it first
        if (!file().size()) {
            return bin_t(0,0);
        }

        do {
			// check the high priority window for data we r missing
			hint = pickUrgent(offer, max_width);

			// check the mid priority window
			if (hint.is_none())
			{
				uint64_t start = (1 + playback_pos_) + HIGHPRIORITYWINDOW;	// start in KB
				int mid = MIDPRIORITYWINDOW;
				int size = mid * HIGHPRIORITYWINDOW;						// size of window in KB


				hint = pickRarest(offer, max_width, start, size);

				//check low priority
				if (hint.is_none())
				{
					start += size;
					size = file().size_in_chunks() - start;
					hint = pickRarest(offer, max_width, start, size);
					set = 'L';
				}
				else
					set = 'M';
			}
			else
				set = 'H';

			// unhinted/late data
			if (!file().ack_out().is_empty(hint)) {
				binmap_t::copy(ack_hint_out_, file().ack_out(), hint);
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
        		while (hint.base_length()>max_width)
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


    void updatePlaybackPos(int size = 1)
    {
    	assert(size>-1);
    	if (size<file().size_in_chunks())
    		playback_pos_ += size;
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
			if (!file().ack_out().is_empty(bin_t(i)))
				t++;
			i++;
		}
		total = t;
		t = t*100/((x<<1)-1);
		fprintf(stderr, "low %u, ", t);
		t = 0;
		while (i<=end_mid)
		{
			if (!file().ack_out().is_empty(bin_t(i)))
				t++;
			i++;
		}
		total += t;
		t = t*100/((x*y)<<1);
		fprintf(stderr, "mid %u, ", t);
		t = 0;
		while (i<=file().size_in_chunks()<<1)
		{
			if (!file().ack_out().is_empty(bin_t(i)))
				t++;
			i++;
		}
		total += t;
		t = t*100/((file().size_in_chunks()-(x*y+playback_pos_))<<1);
		fprintf(stderr, "low %u  -> in total: %i\t pp: %i\n", t, total, playback_pos_);
	}

};
