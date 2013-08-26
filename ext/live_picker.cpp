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


//#define FALLBACK_LIVE_PP_NUMBER_PREBUF_CHUNKS	10 // chunks

// Set this to a few seconds worth of data to get a decent prebuffer. Should
// work when content is MPEG-TS. With LIVESOURCE=ANDROID and H.264 prebuffering
// does *not* work, as video player (VLC) will play super-realtime and catch up
// with source. Probably due to lack of timing codes in raw H.264?!
//
#define LIVE_PP_NUMBER_PREBUF_CHUNKS		10 // chunks

// How often to test if we are not hook-in at a point too far from the
// source (due to communication problems or source restart)
//
#define LIVE_DIVERGENCE_TEST_INTERVAL		(10*TINT_SEC)

// TODO: policy
#define LIVE_PP_MAX_NUMBER_DIVERGENCE_CHUNKS	100 // chunks


// Map to store the highest chunk each peer has, used for hooking in.
typedef std::map<uint32_t, bin_t> PeerPosMapType;


/** Picks pieces nearly sequentially after hook-in point */
class SimpleLivePiecePicker : public LivePiecePicker {

  protected:
    binmap_t        ack_hint_out_;	// Legacy, not sure why copy used.
    tbqueue         hint_out_;		// Chunks picked
    LiveTransfer*   transfer_;		// Pointer to container
    uint64_t        twist_;		// Unused

    bool	    search4hookin_;	// Search for hook-in point y/n?
    PeerPosMapType  peer_pos_map_;	// Highest HAVEs for each peer
    bool	    source_seen_;	// Whether connected to source
    uint32_t	    source_channel_id_;	// Channel ID of the source
    bin_t	    hookin_bin_;	// Chosen hook-in point when search4hookin_ = false
    bin_t           current_bin_;	// Current pos, not yet received
    tint	    last_div_test_time_;// Time we last checked divergence from source/signed peaks

  public:

    SimpleLivePiecePicker (LiveTransfer* trans_to_pick_from) :
           ack_hint_out_(), transfer_(trans_to_pick_from),
           twist_(0), search4hookin_(true),
           source_seen_(false), source_channel_id_(0),
           hookin_bin_(bin_t::NONE), current_bin_(bin_t::NONE),
           last_div_test_time_(0)
    {
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
        bin_t hint = PickLargestBin(offer,current_bin_);
        if (hint == bin_t::NONE)
        {
            // See if there is stuff to download beyond current bin
            //dprintf("live: pp: Look beyond %s\n", current_bin_.str().c_str() );
            hint = PickBeyondCurrentPos(offer);
        }

        if (hint == bin_t::NONE)
            return hint;

        //dprintf("live: pp: Picked %s\n", hint.str().c_str() );

	assert(ack_hint_out_.is_empty(hint));
	ack_hint_out_.set(hint);
	hint_out_.push_back(tintbin(NOW,hint));
	return hint;
    }


    bin_t PickLargestBin(const binmap_t& offer, bin_t starthint)
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


    /** 
     * Arno, 2013-08-26: Hook-in now based on SIGNED_INTEGRITY. Code needs refactoring.
     * 
     * Arno, 2012-01-01: Because multiple HAVE messages may be encoded in single datagram,
     * make this a transaction like thing.
     *
     * LIVETODO: if latest source pos is not in first datagram, you may hook-in too late.
     *
     */
    void StartAddPeerPos(uint32_t channelid, bin_t peerpos, bool peerissource)
    {
    	//fprintf(stderr,"live: pp: StartAddPeerPos: peerpos %s\n", peerpos.str().c_str());
	if (peerissource) {
	    source_seen_ = true;
	    source_channel_id_ = channelid;
	}

	bin_t peerbasepos(peerpos.base_right());

	//fprintf(stderr,"live: pp: StartAddPeerPos: peerbasepos %s\n", peerbasepos.str().c_str());
	bin_t cand;
	cand = bin_t(0,peerbasepos.layer_offset());

	PeerPosMapType::iterator iter = peer_pos_map_.find(channelid);
	if (iter == peer_pos_map_.end())
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


    void EndAddPeerPos(uint32_t channelid)
    {
	// See if first hook-in or we are checking divergence from source
	if (!search4hookin_)
	{
	    if ((last_div_test_time_+LIVE_DIVERGENCE_TEST_INTERVAL) > NOW)
		return;
	}

	last_div_test_time_ = NOW;
	bin_t candbin = CalculateHookinPos();
	if (candbin != bin_t::NONE)
	{
	    // Found a candidate to hook-in on.
	    bool setnewhookin = search4hookin_;
	    if (!setnewhookin)
	    {
		// Already tuned in, see if not too much divergence
		if (candbin > current_bin_)
		{
		    // How much peer can be apart from source. Currently live discard window.
	            uint64_t maxdiff = transfer_->GetDefaultHandshake().live_disc_wnd_;
	            if (maxdiff == POPT_LIVE_DISC_WND_ALL)
	        	maxdiff = LIVE_PP_MAX_NUMBER_DIVERGENCE_CHUNKS;
		    bool closeenough = candbin.toUInt() < (current_bin_.toUInt()+maxdiff);

		    fprintf(stderr,"live: pp: EndAddPeerPos: New %s from %s closeenough %s\n", candbin.str().c_str(), current_bin_.str().c_str(), (closeenough ? "true" : "false") );

		    if (!closeenough)
		    {
			setnewhookin = true;
			fprintf(stderr,"live: pp: EndAddPeerPos: Re-hook-in on %s from %s\n", candbin.str().c_str(), current_bin_.str().c_str() );
		    }
		}
	    }

	    fprintf(stderr,"live: pp: EndAddPeerPos: exec %s\n", (setnewhookin ? "true":"false") );

	    // First time, or re-hook-in
	    if (setnewhookin)
	    {
		hookin_bin_ = candbin;
		current_bin_ = hookin_bin_;
		search4hookin_ = false;
		fprintf(stderr,"live: pp: EndAddPeerPos: Execute hook-in on %s\n", hookin_bin_.str().c_str() );
	    }
    	}
    }


    void ClearPeerPos(uint32_t channelid)
    {
	peer_pos_map_.erase(channelid);
	if (source_seen_ && channelid == source_channel_id_)
	{
	    source_seen_ = false;
	    source_channel_id_ = 0;
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
    	    if (c == NULL || !c->PeerIsSource()) // Arno, 2013-05-23: Safety catch
    	    {
    		// Source left building, fallback to old prebuf method (just 10
    		// chunks from source). Perhaps there are still some peers around.
    		// hookbin = bin_t(0,std::max((bin_t::uint_t)0,cand.layer_offset()-FALLBACK_LIVE_PP_NUMBER_PREBUF_CHUNKS));
    	    }
    	    else
    	    {
    		// Create prebuffer, try to find a chunk a few seconds before source.
    		bin_t::uint_t m = std::max((bin_t::uint_t)LIVE_PP_NUMBER_PREBUF_CHUNKS,cand.layer_offset());
    		m -= LIVE_PP_NUMBER_PREBUF_CHUNKS;
    		bin_t iterbin = bin_t(0,m);

    		// Check what is actually first available chunk
    		while(!c->ack_in().is_filled(iterbin) && iterbin < cand)
    		    iterbin = bin_t(0,iterbin.layer_offset()+1);

    		hookbin = iterbin;

    		fprintf(stderr,"live: pp: CalculateHookinPos: hook-in on source, pos %s diff %llu\n", hookbin.str().c_str(), cand.base_offset() - hookbin.base_offset() );
    	    }
    	}

    	// If not connected to source, hook-in on peers
    	if (hookbin == bin_t::NONE && peer_pos_map_.size() > 0)
        {
            // See if there is a quorum among the peers for a certain position
            // Minimum number of peers for a quorum
            int threshold = 2;
    	    if (transfer_->GetDefaultHandshake().cont_int_prot_ == POPT_CONT_INT_PROT_UNIFIED_MERKLE)
    		threshold = 1; // based on signed peaks, so 1 enough
    	    // TODO: SIGN_ALL: how to deal with many malicious peers sending future HAVEs

            // How much peers in a quorum can be apart. Currently live discard window.
            uint64_t maxdiff = transfer_->GetDefaultHandshake().live_disc_wnd_;

            if (peer_pos_map_.size() < threshold)
            {
                fprintf(stderr,"live: pp: CalculateHookinPos: not connected to source, not enough peers for hookin\n");
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
                fprintf(stderr,"live: pp: CalculateHookinPos: candidate %s\n", pos.str().c_str() );
            }


            // See if there is a set peers of size 'threshold' with current pos > candidate
            // Otherwise, lower candidate and try again.
            int index=bv.size()-threshold;

            fprintf(stderr,"live: pp: CalculateHookinPos: index %d\n", index);

            bool found=false;
            while (index >= 0 && !found)
            {
                int i=0,count=0;
                for (i=index; i<bv.size(); i++)
                {
                    //fprintf(stderr,"live: pp: CalculateHookinPos: index %d count %d bvi %s bvindex %s\n", index, count, bv[i].str().c_str(), bv[index].str().c_str() );
                    bool closeenough = bv[i].toUInt() < (bv[index].toUInt()+maxdiff);
                    if (bv[i] >= bv[index] && closeenough)
                        count++;
                }

                if (count >= threshold)
                {
                    found = true;
                    hookbin = bv[index];
                }
                index--;
            }

            fprintf(stderr,"live: pp: CalculateHookinPos: hook-in on peers, pos %s\n", hookbin.str().c_str());
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

    /** See if chunks are on offer beyond current pos */
    bin_t PickBeyondCurrentPos(const binmap_t &offer)
    {
	//dprintf("live: pp: Look beyond %s\n", current_bin_.str().c_str() );
	bin_t hint = ack_hint_out_.find_empty(current_bin_);
	//dprintf("live: pp: Empty is %s boe %llu boc %llu\n", hint.str().c_str(), hint.toUInt(), current_bin_.toUInt() );

	// Safety catch, find_empty(offset) apparently buggy.
	if (hint.base_offset() <= current_bin_.base_offset())
	    hint = bin_t::NONE;

	if (hint != bin_t::NONE)
	    hint = PickLargestBin(offer,hint);

	return hint;
    }

};

/* the number of peers at which the chance of downloading from the source
 * is lowest.  */
#define SHAR_LIVE_PP_BIAS_LOW_NPEERS 		10

/* if a peer has not uploaded a chunk in this amount of seconds it is no longer
 * considered an uploader in the peers bias algorithm. */
#define SHAR_LIVE_PP_BIAS_UPLOAD_IDLE_SECS 	5.0

/* The increase in probability of downloading from the source that peers get
 * that have uploaded data in the last SHAR_LIVE_PP_BIAS_UPLOAD_IDLE_SECS */
#define SHAR_LIVE_PP_BIAS_FORWARDER_DLPROB_BONUS  0.5  // 0..1

/** How often to test if a chunk should be skipped when curren pos not progressing */
#define SHAR_LIVE_PP_MAX_ATTEMPTS_BEFORE_CHUNK_DROP	100


/**
 * Optimized for (small swarms) sharing
 */
class SharingLivePiecePicker : public SimpleLivePiecePicker {

    uint32_t same_curbin_count_;

public:

    SharingLivePiecePicker (LiveTransfer* trans_to_pick_from) : SimpleLivePiecePicker(trans_to_pick_from), same_curbin_count_(0)
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
	    same_curbin_count_ = 0;
	    //fprintf(stderr,"live: pp: new cur is %s\n", current_bin_.str().c_str() );
	}
	same_curbin_count_++;
	//dprintf("live: pp: new cur end\n" );

	char priority='H';
	// Request next from this peer, if not already requested
	bin_t hint = PickLargestBin(offer,current_bin_);
	if (hint == bin_t::NONE)
	{
	    if (same_curbin_count_ > SHAR_LIVE_PP_MAX_ATTEMPTS_BEFORE_CHUNK_DROP)
	    {
	        // current_bin_ not picked for many times. Check if we should skip
		same_curbin_count_ = 0;
		bool skip = CheckSkipPolicy();
		if (skip)
		{
		    // Skip over chunk
		    current_bin_ = bin_t(0,current_bin_.layer_offset()+1);
		    dprintf("%s live: pp: SKIP to chunk %s\n", tintstr(), current_bin_.str().c_str() );
		    fprintf(stderr,"%s live: pp: SKIP to chunk %s\n", tintstr(), current_bin_.str().c_str() );

                    hint = PickLargestBin(offer,current_bin_);
		    if (hint == bin_t::NONE)
		    {
		         // Again not found, see if there is stuff to download beyond current bin
			 priority = 'M';
			 hint = PickBeyondCurrentPos(offer);
		    }
		}
		else
		{
		    // wait to get from other peer
		    fprintf(stderr,"live: pp: chunk not offered in long time, but avail or newest %s\n", current_bin_.str().c_str() );
		}
	    }
	    else
	    {
		// See if there is stuff to download beyond current bin
		priority = 'M';
                hint = PickBeyondCurrentPos(offer);
	    }
	}

	if (hint == bin_t::NONE)
	{
	    // current_bin_ not on offer, nor any subsequent chunks
	    return hint;
	}

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
		swarm has SHAR_LIVE_PP_BIAS_LOW_NPEERS peers,
		after that it increases again, to ensure the
		behaviour is the old behaviour for larger
		swarms. Old behaviour is to download immediately.
		In larger swarms this is needed because the
		source only has a limited number of upload
		slots, so if you are granted the chance to
		download, you should such that you can forward
		to your other peers.
	     */
	    int32_t nlow = SHAR_LIVE_PP_BIAS_LOW_NPEERS;
	    int32_t npeers = transfer_->GetNumLeechers()+transfer_->GetNumSeeders();
	    uint32_t x = std::max((int32_t)1,std::min(npeers,nlow) - std::max((int32_t)0,npeers-nlow));
	    double dlprob = 1.0/((double)x);

	    // Extra: Increase download prob when you are
	    // forwarding
	    //since_last_upload = time.time() - connection.upload.last_upload_time.get()
    	    //if since_last_upload < self.transporter.SHAR_LIVE_PP_BIAS_UPLOAD_IDLE_SECS:
            //    dlprob += SHAR_LIVE_PP_BIAS_FORWARDER_DLPROB_BONUS;

	    double r = (double)rand()/(double)RAND_MAX;
	    if (r >= dlprob)  // Trust you will get it from peers, don't dl from source
	    {
		//fprintf(stderr,"live: pp: ssopt r %.02lf dlprob %.02lf npeers %u\n", r, dlprob, npeers);
		return bin_t::NONE;
	    }
	}


	//dprintf("live: pp: Picked %s\n", hint.str().c_str() );

	assert(ack_hint_out_.is_empty(hint));
	ack_hint_out_.set(hint);
	hint_out_.push_back(tintbin(NOW,hint));
	return hint;
    }


    bool CheckSkipPolicy()
    {
	/** Policy: See if chunk is available from any of the connected peers.
	 * If so, don't skip. If not check if there are chunks beyond.
	 */
        channels_t *chansptr = transfer_->GetChannels();
        channels_t::iterator iter;
        bool found=false;
        bool beyond=false;
	for (iter=chansptr->begin(); iter!=chansptr->end(); iter++)
	{
	    Channel *c = *iter;
	    if (c != NULL && c->is_established())
	    {
	        found = c->ack_in() .is_filled(current_bin_);
	        if (found)
	            return false; // don't skip
	        else
	        {
	            // Check if there are chunks already past current_bin_ that we can skip to
	            // If not, don't skip. May be waiting for source to generate next chunk.
	            bin_t hint = PickBeyondCurrentPos(c->ack_in());
	            if (hint != bin_t::NONE)
	        	beyond = true;
	        }
	    }
	}
	return beyond;
    }
};


#endif
