/*
 *  live_picker.cpp
 *  swift
 *
 *  Created by Arno Bakker and Victor Grishchenko.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
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

  protected:
    binmap_t        ack_hint_out_;	// Legacy, not sure why copy used.
    tbqueue         hint_out_;		// Chunks picked
    LiveTransfer*   transfer_;		// Pointer to container
    uint64_t        twist_;		// Unused

    bool	    search4hookin_;	// Finding hook-in point y/n?
    PeerPosMapType  peer_pos_map_;	// Highest HAVEs for each peer
    bool	    source_seen_;	// Whether connected to source
    uint32_t	    source_channel_id_;	// Channel ID of the source
    bin_t	    hookin_bin_;	// Chosen hook-in point when search4hookin_ = false
    bin_t           current_bin_;	// Current pos, not yet received


  public:

    SimpleLivePiecePicker (LiveTransfer* trans_to_pick_from) :
           ack_hint_out_(), transfer_(trans_to_pick_from), twist_(0), search4hookin_(true), source_seen_(false), source_channel_id_(0), hookin_bin_(bin_t::NONE), current_bin_(bin_t::NONE) {
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

    virtual bin_t Pick (binmap_t& offer, uint64_t max_width, tint expires, uint32_t channelid) {
    	if (search4hookin_)
    	    return bin_t::NONE;

        while (hint_out_.size() && hint_out_.front().time<NOW-TINT_SEC*3/2) { // FIXME sec
            binmap_t::copy(ack_hint_out_, *(transfer_->ack_out()), hint_out_.front().bin);
            hint_out_.pop_front();
        }

        // Advance ptr
        //dprintf("live: pp: new cur start\n" );
        while (transfer_->ack_out()->is_filled(current_bin_))
        {
            current_bin_ = bin_t(0,current_bin_.layer_offset()+1);
            //fprintf(stderr,"live: pp: new cur is %s\n", current_bin_.str().c_str() );
        }
        //dprintf("live: pp: new cur end\n" );

        // Request next from this peer, if not already requested
        bin_t hint = pickLargestBin(offer,current_bin_);
        if (hint == bin_t::NONE)
        {
            //dprintf("live: pp: Look beyond %s\n", current_bin_.str().c_str() );
            // See if there is stuff to download beyond current bin
            hint = ack_hint_out_.find_empty(current_bin_);
            //dprintf("live: pp: Empty is %s boe %llu boc %llu\n", hint.str().c_str(), hint.toUInt(), current_bin_.toUInt() );

            // Safety catch, find_empty(offset) apparently buggy.
            if (hint.base_offset() <= current_bin_.base_offset())
        	hint = bin_t::NONE;

            if (hint != bin_t::NONE)
        	hint = pickLargestBin(offer,hint);
        }

        if (hint == bin_t::NONE)
            return hint;

        //dprintf("live: pp: Picked %s\n", hint.str().c_str() );

	assert(ack_hint_out_.is_empty(hint));
	ack_hint_out_.set(hint);
	hint_out_.push_back(tintbin(NOW,hint));
	return hint;
    }


    bin_t pickLargestBin(binmap_t& offer, bin_t starthint)
    {
	bin_t hint;
        if (offer.is_filled(starthint) && ack_hint_out_.is_empty(starthint))
        {
            // See which is the largest bin that covers starthint
            bin_t goodhint = starthint;
            hint = starthint;
            //dprintf("live: pp: new hint is %s\n", hint.str().c_str() );

	    while (hint.is_left() && offer.is_filled(hint.sibling()) && ack_hint_out_.is_empty(hint.sibling()))
	    {
		// hint is a left node and its sibling is filled, so we can
		// request the parent too.
		goodhint = hint;
	        hint = hint.parent();
	        //dprintf("live: pp: Going to parent %s\n", hint.str().c_str() );
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
    	//fprintf(stderr,"live: pp: StartAddPeerPos: peerpos %s\n", peerpos.str().c_str());
    	if (search4hookin_) {

    	    if (peerissource) {
    		source_seen_ = true;
    		source_channel_id_ = channelid;
    	    }

    	    bin_t peerbasepos(peerpos.base_right());

    	    fprintf(stderr,"live: pp: StartAddPeerPos: peerbasepos %s\n", peerbasepos.str().c_str());
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
	if (!search4hookin_)
	    return;

	bin_t candbin = CalculateHookinPos();
	if (candbin != bin_t::NONE)
	{
	    hookin_bin_ = candbin;
	    current_bin_ = hookin_bin_;
	    search4hookin_ = false;
    	}
    }


    bin_t CalculateHookinPos() // Must not modify state
    {
#ifdef SJAAK
	return bin_t(0,1077);
#endif
        bin_t hookbin(bin_t::NONE);
    	if (source_seen_)
    	{
    	    bin_t cand = peer_pos_map_[source_channel_id_];
    	    Channel *c = Channel::channel(source_channel_id_);
    	    if (c == NULL)
    	    {
    		// Source left building, fallback to old prebuf method (just 10
    		// chunks from source). Perhaps there are still some peers around.
    		hookbin = bin_t(0,std::max((bin_t::uint_t)0,cand.layer_offset()-FALLBACK_LIVE_PP_NUMBER_PREBUF_CHUNKS));
    	    }
    	    else
    	    {
    		// Create prebuffer, try to find a chunk a few seconds before source.
    		bin_t::uint_t m = std::max((bin_t::uint_t)LIVE_PP_NUMBER_PREBUF_CHUNKS,cand.layer_offset());
    		m -= LIVE_PP_NUMBER_PREBUF_CHUNKS;
    		bin_t iterbin = bin_t(0,m);

    		// Check what is actually first available chunk
    		while(!c->ack_in().is_filled(iterbin) && iterbin < cand)
    		    iterbin.to_right();

    		hookbin = iterbin;
    	    }

	    fprintf(stderr,"live: pp: EndAddPeerPos: hookin on source, pos %s diff %llu\n", hookbin.str().c_str(), cand.base_offset() - hookbin.base_offset() );
    	}
        else if (peer_pos_map_.size() > 0)
        {
            // See if there is a quorum among the peers for a certain position
            // Minimum number of peers for a quorum
            int threshold = 2;
            if (peer_pos_map_.size() < threshold)
            {
                fprintf(stderr,"live: pp: EndAddPeerPos: not connected to source, not enough peers for hookin\n");
                return bin_t::NONE;
            }

            // Put in list and sort
            binvector bv;
            PeerPosMapType::const_iterator iter;
            for (iter=peer_pos_map_.begin(); iter!=peer_pos_map_.end(); iter++)
            {
                bin_t p = iter->second;
                bv.push_back(p);
            }
            std::sort(bv.begin(), bv.end());

            binvector::iterator iter2;
            for (iter2=bv.begin(); iter2 != bv.end(); iter2++)
            {
                bin_t pos = *iter2;
                fprintf(stderr,"live: pp: EndAddPeerPos: candidate %s\n", pos.str().c_str() );
            }


            // See if there is a set peers of size 'threshold' with current pos > candidate
            // Otherwise, lower candidate and try again.
            int index=bv.size()-threshold;

            fprintf(stderr,"live: pp: EndAddPeerPos: index %d\n", index);

            bool found=false;
            while (index >= 0 && !found)
            {
                int i=0,count=0;
                for (i=index; i<bv.size(); i++)
                {
                    fprintf(stderr,"live: pp: EndAddPeerPos: index %d count %d bvi %s bvindex %s\n", index, count, bv[i].str().c_str(), bv[index].str().c_str() );
                    if (bv[i] >= bv[index])
                        count++;
                }

                if (count >= threshold)
                {
                    found = true;
                    hookbin = bv[index];
                }
                index--;
            }

            fprintf(stderr,"live: pp: EndAddPeerPos: hookin on peers, pos %s\n", hookbin.str().c_str());
        }
    	return hookbin;
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

    bool GetSearch4Hookin()
    {
	return search4hookin_;
    }
};





/** Optimized for (small swarms) sharing  */
class SharingLivePiecePicker : public SimpleLivePiecePicker {

    /* the number of peers at which the chance of downloading from the source
     * is lowest. See PiecePickerStreaming. */
    static const uint32_t LIVE_PEERS_BIAS_LOW_NPEERS = 10;

    /* if a peer has not uploaded a chunk in this amount of seconds it is no longer
     * considered an uploader in the peers bias algorithm. */
    static const float LIVE_PEERS_BIAS_UPLOAD_IDLE_SECS = 5.0;

    /* The increase in probability of downloading from the source that peers get
     * that have uploaded data in the last LIVE_PEERS_BIAS_UPLOAD_IDLE_SECS */
    static const float LIVE_PEERS_BIAS_FORWARDER_DLPROB_BONUS = 0.5;  // 0..1


public:

    SharingLivePiecePicker (LiveTransfer* trans_to_pick_from) : SimpleLivePiecePicker(trans_to_pick_from)
    {
    }

    virtual ~SharingLivePiecePicker() {}


    virtual bin_t Pick (binmap_t& offer, uint64_t max_width, tint expires, uint32_t channelid) {
	if (search4hookin_)
	    return bin_t::NONE;

	while (hint_out_.size() && hint_out_.front().time<NOW-TINT_SEC*3/2) { // FIXME sec
	    binmap_t::copy(ack_hint_out_, *(transfer_->ack_out()), hint_out_.front().bin);
	    hint_out_.pop_front();
	}

	// Advance ptr
	//dprintf("live: pp: new cur start\n" );
	while (transfer_->ack_out()->is_filled(current_bin_))
	{
	    current_bin_ = bin_t(0,current_bin_.layer_offset()+1);
	    //fprintf(stderr,"live: pp: new cur is %s\n", current_bin_.str().c_str() );
	}
	//dprintf("live: pp: new cur end\n" );

	char priority='H';
	// Request next from this peer, if not already requested
	bin_t hint = pickLargestBin(offer,current_bin_);
	if (hint == bin_t::NONE)
	{
	    priority = 'M';
	    //dprintf("live: pp: Look beyond %s\n", current_bin_.str().c_str() );
	    // See if there is stuff to download beyond current bin
	    hint = ack_hint_out_.find_empty(current_bin_);
	    //dprintf("live: pp: Empty is %s boe %llu boc %llu\n", hint.str().c_str(), hint.toUInt(), current_bin_.toUInt() );

	    // Safety catch, find_empty(offset) apparently buggy.
	    if (hint.base_offset() <= current_bin_.base_offset())
		hint = bin_t::NONE;

	    if (hint != bin_t::NONE)
		hint = pickLargestBin(offer,hint);
	}

	if (hint == bin_t::NONE)
	    return hint;

	// When picking from source, do small-swarms optimization, unless urgent
	if (priority != 'H' && source_seen_ && source_channel_id_ == channelid)
	{
	     /* Up to trustdl seconds before playout deadline
		we put our faith into peers to deliver us the
		piece instead of the source.

		So instead of downloading from the source
		when possible, just one in x peers will DL
		from source.

		This download probability will decrease till
		swarm has LIVE_PEERS_BIAS_LOW_NPEERS peers,
		after that it increases again, to ensure the
		behaviour is the old behaviour for larger
		swarms. Old behaviour is to download immediately.
		In larger swarms this is needed because the
		source only has a limited number of upload
		slots, so if you are granted the chance to
		download, you should such that you can forward
		to your other peers.
	     */
	    int32_t nlow = LIVE_PEERS_BIAS_LOW_NPEERS;
	    int32_t npeers = transfer_->GetNumLeechers()+transfer_->GetNumSeeders();
	    uint32_t x = std::max((int32_t)1,std::min(npeers,nlow) - std::max((int32_t)0,npeers-nlow));
	    double dlprob = 1.0/((double)x);

	    // Extra: Increase download prob when you are
	    // forwarding
	    //since_last_upload = time.time() - connection.upload.last_upload_time.get()
    	    //if since_last_upload < self.transporter.LIVE_PEERS_BIAS_UPLOAD_IDLE_SECS:
            //    dlprob += LIVE_PEERS_BIAS_FORWARDER_DLPROB_BONUS;

	    double r = (double)rand()/(double)RAND_MAX;
	    if (r >= dlprob)  // Trust you will get it from peers, don't dl from source
	    {
		fprintf(stderr,"live: pp: ssopt r %.02lf dlprob %.02lf npeers %u x %u\n", r, dlprob, npeers, x );
		return bin_t::NONE;
	    }
	}


	//dprintf("live: pp: Picked %s\n", hint.str().c_str() );

	assert(ack_hint_out_.is_empty(hint));
	ack_hint_out_.set(hint);
	hint_out_.push_back(tintbin(NOW,hint));
	return hint;
    }
};



#endif
