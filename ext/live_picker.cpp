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

#define LIVE_PP_MIN_BITRATE_MEASUREMENT_INTERVAL	10 // seconds


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
    bin_t           last_munro_bin_;	// Last known munro from source
    tint	    last_munro_tint_;	// Timestamp of source's last known munro
    bin_t	    hookin_bin_;	// Chosen hook-in point when search4hookin_ = false
    tint	    hookin_tint_;	// Timestamp of *munro* off which hook-in was calculated
    bin_t           current_bin_;	// Current pos, not yet received

  public:

    SimpleLivePiecePicker (LiveTransfer* trans_to_pick_from) :
           ack_hint_out_(), transfer_(trans_to_pick_from),
           twist_(0), search4hookin_(true),
           last_munro_bin_(bin_t::NONE), last_munro_tint_(0),
           hookin_bin_(bin_t::NONE), hookin_tint_(0),
           current_bin_(bin_t::NONE)
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

    	CheckIfLagging();

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


    void AddPeerMunro(bin_t munro, tint sourcet)
    {
    	//fprintf(stderr,"live: pp: AddPeerMunro: munro %s\n", munro.str().c_str());

	// Source has advanced
	if (sourcet > last_munro_tint_)
	{
	    last_munro_bin_ = munro;
	    last_munro_tint_ = sourcet;
	}

	if (!search4hookin_)
	{
	    // Already hooked in, check for too much divergence from source
	    if (!CheckIfLagging())
		return;
	    fprintf(stderr,"live: pp: AddPeerMunro: We are lagging behind\n");
	}

	// First time hook-in, or diverging
	bin_t candbin = CalculateHookinPos();
	if (candbin != bin_t::NONE)
	{
	    hookin_bin_ = candbin;
	    current_bin_ = hookin_bin_;
	    hookin_tint_ = last_munro_tint_;
	    search4hookin_ = false;
	    fprintf(stderr,"live: pp: Execute hook-in on %s\n", hookin_bin_.str().c_str() );
	}
    }

    bool CheckIfLagging()
    {
	tint candtint = CalculateCurrentPosInTime(current_bin_);
	if (candtint == TINT_NEVER)
	    return false; // could not calc

	tint nowdifft = NOW - candtint;
	double nowdifftinsecs = (double)nowdifft/(double)TINT_SEC;
	fprintf(stderr,"live: pp: Current now lag %lf\n", nowdifftinsecs );

	if (nowdifft < (SWIFT_LIVE_MAX_SOURCE_DIVERGENCE_TIME*TINT_SEC))
	{
	    tint sourcedifft = last_munro_tint_ - candtint;
	    double sourcedifftinsecs = (double)sourcedifft/(double)TINT_SEC;
	    fprintf(stderr,"live: pp: Current source lag %lf\n", sourcedifftinsecs );

	    // Not lagging
	    return false;
	}
	else
	    return true;
    }

    bin_t CalculateHookinPos()
    {
	/* If you want more prebuffering, you should substract a few seconds worth
	 * of chunks from last_munro_bin_. Be sure to check if they are
	 * actually available from peers (e.g. case where source just started)
	 */

	// Return base start of munro subtree, this means prebuffering of
	// nchunks_per_sign
	//
	return last_munro_bin_.base_left();
	//return bin_t(0,1077);
    }


    /** Returns estimated bitrate */
    double CalculateBitrate()
    {
	if (hookin_tint_ == last_munro_tint_ || (hookin_tint_+(LIVE_PP_MIN_BITRATE_MEASUREMENT_INTERVAL*TINT_SEC)) > last_munro_tint_)
	{
	    fprintf(stderr,"CalculateBitrate: hook %lld last %lld\n", hookin_tint_, last_munro_tint_);

	    // No info to calc bitrate on, or too short interval
	    fprintf(stderr,"CalculateBitrate: time too short\n" );
	    return 0.0;
	}
	tint bdifft = last_munro_tint_ - hookin_tint_;

	fprintf(stderr,"CalculateBitrate: time  between munro and hookin %lld\n", bdifft );

	bin_t::uint_t bdiffc = last_munro_bin_.base_right().layer_offset() - hookin_bin_.layer_offset();

	fprintf(stderr,"CalculateBitrate: chunks between munro and hookin %lld\n", bdiffc );

	double bitrate = ((double)bdiffc*transfer_->chunk_size()) / (double)(bdifft/TINT_SEC);

	fprintf(stderr,"CalculateBitrate: bitrate %lf\n", bitrate );

	return bitrate;
    }


    /** Returns the equivalent in time of current_bin_, using bitrate estimation */
    tint CalculateCurrentPosInTime(bin_t pos)
    {
	double bitrate = CalculateBitrate();
	if (bitrate == 0.0)
	    return TINT_NEVER;

	bin_t::uint_t cdiffc = pos.base_right().layer_offset() - hookin_bin_.layer_offset();

	fprintf(stderr,"CalculateCurrentPosInTime: chunks between current and hookin %lld\n", cdiffc );

	fprintf(stderr,"CalculateCurrentPosInTime: cdifft %lld bitrate %lf\n", cdiffc, bitrate );
	tint cdifft = TINT_SEC * (tint)((double)(cdiffc*transfer_->chunk_size())/bitrate);

	fprintf(stderr,"CalculateCurrentPosInTime: time  between current and hookin %lld\n", cdifft );

	fprintf(stderr,"CalculateCurrentPosInTime: hookin  time %s\n", tintstr(hookin_tint_) );
	fprintf(stderr,"CalculateCurrentPosInTime: munro   time %s\n", tintstr(last_munro_tint_) );
	fprintf(stderr,"CalculateCurrentPosInTime: current time %s\n", tintstr(hookin_tint_ + cdifft) );

	return hookin_tint_ + cdifft;
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


/*
 * SharingLivePiecePicker: A piece picker with optimizations for small swarms.
 * Below is a description of the idea. TODO is to look at the due time better
 * and to reactivate the bonus for uploaders.
 *
 * From P2P-Next deliverable D4.0.5:
 *
 * "[Users] observed a peculiar problem with live streaming with a small number
 * of peers (e.g. 16). In many cases most bandwidth would be delivered by the
 * live-content injector instead of by the peers. For larger swarms the desired
 * behaviour where peers supply most bandwidth would naturally evolve. [..]
 *
 * Hence, we implemented a new download policy for live streams to improve
 * sharing in small swarms. When offered the opportunity to download a piece
 * from the injector [..], the policy calculates a download probability between
 * 0..1 to see if it actually should. If the piece is due for playback soon,
 * the probability is 1. Otherwise, the download probability is based on the
 * size of the swarm.
 *
 * For small swarms up to size Z, the probability is inversely proportional to
 * the number of peers connected to (a measure of the swarm size). So the more
 * peers the lower the chance of downloading a piece from the injector. This
 * policy therefore takes the optimistic approach that some other peer will
 * download that piece and you can get it from him a bit later. For swarms
 * larger than Z, the download probability linearly increases again, reaching
 * 1 for >= 2Z peers. This preserves the previous download behaviour of a peer
 * in large swarms. The rationale is that for larger swarms the peers that have
 * the chance to download from the injector should do so, such that the pieces
 * can be distributed by more peers sooner.
 *
 * Extra feature of the policy is that the download chance is increased if the
 * peer was forwarding to others in the last N seconds. This ensures that
 * sharers will remain sharers. This new policy results in much improved
 * sharing behaviour in small swarms in lab tests for Z=10, with up to 80% of
 * the bandwidth being supplied by peers."
 */

/* the number of peers at which the chance of downloading from the source
 * is lowest (aka Z in the above text).  */
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
	Channel *c = Channel::channel(channelid);
	if (c == NULL)
	    return bin_t::NONE; // error

	if (priority != 'H' && c->PeerIsSource())
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
