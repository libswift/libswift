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


#define FALLBACK_LIVE_PP_NUMBER_PREBUF_CHUNKS	10 // chunks

// Set this to a few seconds worth of data to get a decent prebuffer. Should
// work when content is MPEG-TS. With LIVESOURCE=ANDROID and H.264 prebuffering
// does *not* work, as video player (VLC) will play super-realtime and catch up
// with source. Probably due to lack of timing codes in raw H.264?!
//
#define LIVE_PP_NUMBER_PREBUF_CHUNKS		10 // chunks


// Map to store the highest chunk each peer has, used for hooking in.
typedef std::map<uint32_t, bin_t> PeerPosMapType;


/** Picks pieces nearly sequentially after hook-in point */
class SimpleLivePiecePicker : public LivePiecePicker {

    binmap_t        ack_hint_out_;	// Legacy, not sure why copy used.
    tbqueue         hint_out_;		// Chunks picked
    LiveTransfer*   transfer_;		// Pointer to container
    uint64_t        twist_;		// Unused

    bool	    hooking_in_;	// Finding hook-in point y/n?
    PeerPosMapType  peer_pos_map_;	// Highest HAVEs for each peer
    bool	    source_seen_;	// Whether connected to source
    uint32_t	    source_channel_id_;	// Channel ID of the source
    bin_t	    hookin_bin_;	// Chosen hook-in point when hooking_in_ = false
    bin_t           current_bin_;	// Current pos, not yet received


public:

    SimpleLivePiecePicker (LiveTransfer* trans_to_pick_from) :
           ack_hint_out_(), transfer_(trans_to_pick_from), twist_(0), hooking_in_(true), source_seen_(false), source_channel_id_(0), hookin_bin_(bin_t::NONE), current_bin_(bin_t::NONE) {
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

        char binstr[32];

        // Advance ptr
        //dprintf("live: pp: new cur start\n" );
        while (transfer_->ack_out()->is_filled(current_bin_))
        {
            current_bin_ = bin_t(0,current_bin_.layer_offset()+1);
            //fprintf(stderr,"live: pp: new cur is %s\n", current_bin_.str(binstr) );
        }
        //dprintf("live: pp: new cur end\n" );


        // Request next from this peer, if not already requested
        bin_t hint = pickLargestBin(offer,current_bin_);
        if (hint == bin_t::NONE)
        {
            //dprintf("live: pp: Look beyond %s\n", current_bin_.str(binstr) );
            // See if there is stuff to download beyond current bin
            hint = ack_hint_out_.find_empty(current_bin_);
            //dprintf("live: pp: Empty is %s boe %llu boc %llu\n", hint.str(binstr), hint.toUInt(), current_bin_.toUInt() );

            // Safety catch, find_empty(offset) apparently buggy.
            if (hint.base_offset() <= current_bin_.base_offset())
        	hint = bin_t::NONE;

            if (hint != bin_t::NONE)
        	hint = pickLargestBin(offer,hint);
        }

        if (hint == bin_t::NONE)
            return hint;

        //dprintf("live: pp: Picked %s\n", hint.str(binstr) );

	assert(ack_hint_out_.is_empty(hint));
	ack_hint_out_.set(hint);
	hint_out_.push_back(tintbin(NOW,hint));
	return hint;
    }


    bin_t pickLargestBin(binmap_t& offer, bin_t starthint)
    {
        char binstr[32];
	bin_t hint;
        if (offer.is_filled(starthint) && ack_hint_out_.is_empty(starthint))
        {
            // See which is the largest bin that covers starthint
            bin_t goodhint = starthint;
            hint = starthint;
            //dprintf("live: pp: new hint is %s\n", hint.str(binstr) );

	    while (hint.is_left() && offer.is_filled(hint.sibling()) && ack_hint_out_.is_empty(hint.sibling()))
	    {
		// hint is a left node and its sibling is filled, so we can
		// request the parent too.
		goodhint = hint;
	        hint = hint.parent();
	        //dprintf("live: pp: Going to parent %s\n", hint.str(binstr) );
	    }
	    // Previous one was the max.
	    return goodhint;
        }
	else
	    return bin_t::NONE;
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
    	    cand = bin_t(0,peerbasepos.layer_offset());

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

    	    bin_t cand = peer_pos_map_[source_channel_id_];
    	    Channel *c = Channel::channel(source_channel_id_);
    	    if (c == NULL)
    	    {
    		// Source left building, fallback to old prebuf method (just 10
    		// chunks from source). Perhaps there are still some peers around.
#ifdef _WIN32
    		hookin_bin_ = bin_t(0,max(0,cand.layer_offset()-FALLBACK_LIVE_PP_NUMBER_PREBUF_CHUNKS));
#else
    		hookin_bin_ = bin_t(0,std::max((bin_t::uint_t)0,cand.layer_offset()-FALLBACK_LIVE_PP_NUMBER_PREBUF_CHUNKS));
#endif
    	    }
    	    else
    	    {
    		// More prebuffer, try to find a few seconds before source.
#ifdef _WIN32
    		bin_t::uint_t m = max(LIVE_PP_NUMBER_PREBUF_CHUNKS,cand.layer_offset());
#else
    		bin_t::uint_t m = std::max((bin_t::uint_t)LIVE_PP_NUMBER_PREBUF_CHUNKS,cand.layer_offset());
#endif
    		m -= LIVE_PP_NUMBER_PREBUF_CHUNKS;
    		bin_t iterbin = bin_t(0,m);

    		// Check what is actually first available chunk
    		while(!c->ack_in().is_filled(iterbin) && iterbin < cand)
    		    iterbin.to_right();

    		hookin_bin_ = iterbin;
    	    }
	    current_bin_ = hookin_bin_;
	    hooking_in_ = false;

	    fprintf(stderr,"live: pp: EndAddPeerPos: hookin on source, pos %s diff %llu\n", hookin_bin_.str(binstr), cand.base_offset() - hookin_bin_.base_offset() );
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
