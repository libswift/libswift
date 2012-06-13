/*
 *  seq_picker.cpp
 *  swift
 *
 *  Created by Victor Grishchenko on 10/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include "swift.h"
#include <cassert>

using namespace swift;


/** Picks pieces nearly sequentialy; some local randomization (twisting)
    is introduced to prevent synchronization among multiple channels. */
class SeqPiecePicker : public PiecePicker {

    binmap_t        ack_hint_out_;
    tbqueue         hint_out_;
    FileTransfer*   transfer_;
    uint64_t        twist_;
    bin_t           range_;

public:

    SeqPiecePicker (FileTransfer* file_to_pick_from) : ack_hint_out_(),
           transfer_(file_to_pick_from), twist_(0), range_(bin_t::ALL) {
        binmap_t::copy(ack_hint_out_, *(hashtree()->ack_out()));
    }
    virtual ~SeqPiecePicker() {}

    HashTree * hashtree() {
        return transfer_->hashtree();
    }

    virtual void Randomize (uint64_t twist) {
        twist_ = twist;
    }

    virtual void LimitRange (bin_t range) {
        range_ = range;
    }

    virtual bin_t Pick (binmap_t& offer, uint64_t max_width, tint expires) {
        while (hint_out_.size() && hint_out_.front().time<NOW-TINT_SEC*3/2) { // FIXME sec
            binmap_t::copy(ack_hint_out_, *(hashtree()->ack_out()), hint_out_.front().bin);
            hint_out_.pop_front();
        }
        if (!hashtree()->size()) {
            return bin_t(0,0); // whoever sends it first
        // Arno, 2011-06-28: Partial fix by Victor. exact_size_known() missing
        //} else if (!hashtree()->exact_size_known()) {
        //    return bin64_t(0,(hashtree()->size()>>10)-1); // dirty
        }
    retry:      // bite me
        twist_ &= (hashtree()->peak(0).toUInt()) & ((1<<6)-1);

        bin_t hint = binmap_t::find_complement(ack_hint_out_, offer, twist_);
        if (hint.is_none()) {
            return hint; // TODO: end-game mode
        }

        if (!hashtree()->ack_out()->is_empty(hint)) { // unhinted/late data
            binmap_t::copy(ack_hint_out_, *(hashtree()->ack_out()), hint);
            goto retry;
        }
        while (hint.base_length()>max_width)
            hint = hint.left();
        assert(ack_hint_out_.is_empty(hint));
        ack_hint_out_.set(hint);
        hint_out_.push_back(tintbin(NOW,hint));
        return hint;
    }

    int Seek(bin_t offbin, int whence)
    {
    	return -1;
    }
};
