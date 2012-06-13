/*
 *  live_picker.cpp
 *  swift
 *
 *  Created by Arno Bakker and Victor Grishchenko.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#ifndef LIVE_PICKER_H
#define LIVE_PICKER_H


#include "swift.h"
#include <cassert>
#include <map>

using namespace swift;


#define LIVE_PP_NUMBER_PREBUF_CHUNKS	10 // chunks


typedef std::map<uint32_t, bin_t> PeerPosMapType;


/** Picks pieces nearly sequentialy; some local randomization (twisting)
    is introduced to prevent synchronization among multiple channels. */
class SimpleLivePiecePicker : public LivePiecePicker {

    binmap_t        ack_hint_out_;
    tbqueue         hint_out_;
    LiveTransfer*   transfer_;
    uint64_t        twist_;

	bool			hooking_in_;
	PeerPosMapType	peer_pos_map_;
	bool			source_seen_;
	uint32_t		source_channel_id_;
    bin_t			hookin_bin_;
    bin_t           current_bin_;




public:

    SimpleLivePiecePicker (LiveTransfer* trans_to_pick_from) :
           ack_hint_out_(), transfer_(trans_to_pick_from), twist_(0), hooking_in_(true), source_seen_(false), source_channel_id_(0), hookin_bin_(0,0), current_bin_(bin_t::ALL) {
        binmap_t::copy(ack_hint_out_, *(transfer_->ack_out()));
    }
    virtual ~SimpleLivePiecePicker() {}

    HashTree * hashtree() {
    		return NULL;
    }

    virtual void Randomize (uint64_t twist) {
        twist_ = twist;
    }

    virtual void LimitRange (bin_t range) {
    }

    virtual bin_t Pick (binmap_t& offer, uint64_t max_width, tint expires) {
    	if (hooking_in_)
    		return bin_t::NONE;

        while (hint_out_.size() && hint_out_.front().time<NOW-TINT_SEC*3/2) { // FIXME sec
            binmap_t::copy(ack_hint_out_, *(transfer_->ack_out()), hint_out_.front().bin);
            hint_out_.pop_front();
        }

        // Advance ptr
        if (transfer_->ack_out()->is_filled(current_bin_))
			current_bin_ = bin_t(0,current_bin_.layer_offset()+1);

        // Request next from this peer, if not already requested
        bin_t hint;
        if (offer.is_filled(current_bin_) &&  ack_hint_out_.is_empty(current_bin_))
			hint = current_bin_;
		else
			return bin_t::NONE;

		//while (hint.base_length()>max_width)
		//	hint = hint.left();
		assert(ack_hint_out_.is_empty(hint));
		ack_hint_out_.set(hint);
		hint_out_.push_back(tintbin(NOW,hint));
		return hint;
    }


    int Seek(bin_t offbin, int whence)
    {
    	return -1;
    }


    /** Arno: Because multiple HAVE messages may be encoded in single datagram,
     * make this a transaction like thing.
     *
     * LIVETODO: if latest source pos is not in first datagram, you may hook-in too late.
     *
     * LIVETODO: what if peer departs?
     */
    void StartAddPeerPos(uint32_t channelid, bin_t peerpos, bool peerissource)
    {
    	char binstr[32];

    	//fprintf(stderr,"live: pp: StartAddPeerPos: peerpos %s\n", peerpos.str(binstr));
    	if (hooking_in_) {

    		if (peerissource) {
    			source_seen_ = true;
    			source_channel_id_ = channelid;
    		}

    		bin_t peerbasepos(peerpos.base_right());

    		fprintf(stderr,"live: pp: StartAddPeerPos: peerbasepos %s\n", peerbasepos.str(binstr));
    		bin_t cand;
#ifdef _WIN32
    		cand = bin_t(0,max(0,peerbasepos.layer_offset()-LIVE_PP_NUMBER_PREBUF_CHUNKS));
#else
    		cand = bin_t(0,std::max((bin_t::uint_t)0,peerbasepos.layer_offset()-LIVE_PP_NUMBER_PREBUF_CHUNKS));
#endif
    		PeerPosMapType::iterator iter = peer_pos_map_.find(channelid);
    	    if (iter ==  peer_pos_map_.end())
    	    {
    	        // Unknown channel, just store
    	    	peer_pos_map_.insert(PeerPosMapType::value_type(channelid, cand));
    	    }
    	    else
    	    {
    	    	// Update channelid's position, if newer
				bin_t oldcand = peer_pos_map_[channelid];
				if (cand.layer_offset() > oldcand.layer_offset())
					peer_pos_map_[channelid] = cand;
    	    }
    	}
    }


    void EndAddPeerPos(uint32_t channelid)
    {
    	char binstr[32];

    	if (!hooking_in_)
    		return;

    	if (source_seen_) {
	    	hookin_bin_ = peer_pos_map_[source_channel_id_];
	    	current_bin_ = hookin_bin_;
			hooking_in_ = false;

    		fprintf(stderr,"live: pp: EndAddPeerPos: hookin on source, pos %s\n", hookin_bin_.str(binstr));
    	}
    	else if (peer_pos_map_.size() > 0)
    	{
    		fprintf(stderr,"live: pp: EndAddPeerPos: not connected to source, wait (peer hook-in TODO)\n");
    		// See if there is a quorum for a certain position
    		// LIVETODO
    		/*PeerPosMapType::const_iterator iter;
    	    for (iter=peer_pos_map_.begin(); iter!=peer_pos_map_.end(); iter++)
    	    {
    	    	hookin_pos_ = iter->second;
    	    	current_pos_ = hookin_pos_;
    	    	fprintf(stderr,"live: pp: EndAddPeerPos: cand pos %s\n", hookin_pos_.str(binstr));
    	    }
    		fprintf(stderr,"live: pp: EndAddPeerPos: hookin on peers, pos %s\n", hookin_pos_.str(binstr));
    		hooking_in_ = false;*/
    	}
    }



    bin_t GetHookinPos()
    {
    	return hookin_bin_;
    }

    bin_t GetCurrentPos()
    {
    	// LIVETODO?
    	// GetCurrentPos doesn't mean we obtain the indicated piece!
    	return current_bin_;
    }
};

#endif
