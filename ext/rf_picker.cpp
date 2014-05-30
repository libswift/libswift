/*
 *  rf_picker.cpp
 *  swift
 *
 *  Created by Riccardo Petrocco.
 *  Copyright 2009-2012 Delft University of Technology. All rights reserved.
 *
 */

#include "swift.h"
#include <cassert>

using namespace swift;

#define DEBUGPICKER     0

/**
 * Picks pieces in rarest first fashion.
 * */
class RFPiecePicker : public PiecePicker
{

    binmap_t        ack_hint_out_;
    tbqueue         hint_out_;
    FileTransfer*   transfer_;
    Availability*   avail_;
    uint64_t        twist_;
    bin_t           range_;

public:

    RFPiecePicker(FileTransfer* file_to_pick_from) : ack_hint_out_(),
        transfer_(file_to_pick_from), twist_(0), range_(bin_t::ALL) {
        avail_ = transfer_->availability();
        binmap_t::copy(ack_hint_out_, *(hashtree()->ack_out()));
        if (DEBUGPICKER)
            dprintf("Init picker\n");
    }

    virtual ~RFPiecePicker() {}

    HashTree * hashtree() {
        return transfer_->hashtree();
    }

    virtual void LimitRange(bin_t range) {
        range_ = range;
    }

    virtual void Randomize(uint64_t twist) {
        twist_ = twist;
    }

    virtual bin_t Pick(binmap_t& offer, uint64_t max_width, tint expires, uint32_t channelid) {
        bin_t hint = bin_t::NONE;

        int ret_size = 0;

        // delete outdated hints
        while (hint_out_.size() && hint_out_.front().time<NOW-TINT_SEC*3/2) { // FIXME sec
            binmap_t::copy(ack_hint_out_, *(hashtree()->ack_out()), hint_out_.front().bin);
            hint_out_.pop_front();
        }

        // get the first piece to estimate the size, whoever sends it first
        // Ric: we could get whatever bin?!
        uint64_t size = hashtree()->size();
        if (!size) {
            return bin_t(0,0);
        }

        if (DEBUGPICKER)
            dprintf("RF picker:");

        int rarity_idx = 0;
        do {
            bool retry = false;

            if (DEBUGPICKER)
                dprintf("Index: %d\n", rarity_idx);

            binmap_t *rare = avail_->get(rarity_idx);
            if (rare==NULL) {
                if (DEBUGPICKER)
                    dprintf("failed!! %p\n", rare);
                break;
            }
            if (!rare->is_empty()) {

                binmap_t curr;
                binmap_t::copy(curr, *rare);
                bool checked_all = false;

                while (hint.is_none() && !checked_all) {

                    hint = binmap_t::find_match(curr, offer, range_, twist_);

                    if (DEBUGPICKER)
                        dprintf("found %s (rarity:%d)", hint.str().c_str(), rarity_idx);

                    if (hint.is_none()) {
                        if (DEBUGPICKER)
                            dprintf(" => move to the next index\n");
                        checked_all = true;
                    } else if (!ack_hint_out_.is_filled(hint)) {
                        hint = binmap_t::find_complement(ack_hint_out_, offer, hint, twist_);

                        if (DEBUGPICKER)
                            dprintf(" and selected %s \n", hint.str().c_str());

                        // unhinted/late data
                        if (!hashtree()->ack_out()->is_empty(hint)) {
                            if (DEBUGPICKER)
                                dprintf("RF picker: ..but has been requested already\n");
                            binmap_t::copy(ack_hint_out_, *(hashtree()->ack_out()), hint);
                            retry = true;
                            hint = bin_t::NONE;
                        } else {
                            if (DEBUGPICKER)
                                dprintf(" returning %s \n", hint.str().c_str());
                            // store it for returning
                            assert(ack_hint_out_.is_empty(hint));

                        }
                    } else {
                        if (DEBUGPICKER)
                            dprintf(" but already in ack_hint_out \n");
                        curr.reset(hint);
                        hint = bin_t::NONE;
                    }
                }

            }
            if (!retry)
                rarity_idx++;

        } while (rarity_idx<SWIFT_MAX_OUTGOING_CONNECTIONS && hint.is_none());

        if (hint.is_none()) {
            hint = binmap_t::find_complement(ack_hint_out_, offer, twist_);
            if (DEBUGPICKER)
                dprintf("last resort returned: %s (is none: %d)\n", hint.str().c_str(), hint.is_none());
        }

        // end game
        if (hint.is_none()) {
            return hint;
        }

        ack_hint_out_.set(hint);
        hint_out_.push_back(tintbin(NOW,hint));

        return hint;
    }

    int Seek(bin_t offbin, int whence) {
        return 0;
    }
};



