/*
 *  live.cpp
 *  Subclass of ContentTransfer for live streaming.
 *
 *  Arno: Currently uses ever increasing chunk IDs. The binmap datastructure
 *  can store this quite efficiently, as long as there are few holes.
 *  The Storage object will save all chunks. The latter can be modified to wrap
 *  around, that is, a certain modulo is applied that overwrites earlier chunks.
 *  This modulo is equivalent to the live discard window (see IETF PPSPP spec).
 *  This overwriting can be done both at the source and in a client.
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 */
//LIVE
#include "swift.h"
#include <cfloat>

#include "ext/live_picker.cpp" // FIXME FIXME FIXME FIXME

using namespace swift;


/*
 * Global Variables
 */
std::vector<LiveTransfer*> LiveTransfer::liveswarms;


/*
 * Local Constants
 */
#define TRANSFER_DESCR_LIVE_OFFSET	4000000

/** A constructor for a live source. */
LiveTransfer::LiveTransfer(std::string filename, const pubkey_t &pubkey, const privkey_t &privkey, bool check_netwvshash, uint32_t nchunks_per_sign, uint64_t disc_wnd, uint32_t chunk_size) :
	ContentTransfer(LIVE_TRANSFER), chunk_size_(chunk_size), am_source_(true),
	filename_(filename), last_chunkid_(0), offset_(0),
	chunks_since_sign_(0),nchunks_per_sign_(nchunks_per_sign),
	pubkey_(pubkey), privkey_(privkey)
{
    Initialize(check_netwvshash,disc_wnd);

    picker_ = NULL;
}


/** A constructor for live client. */
LiveTransfer::LiveTransfer(std::string filename, const pubkey_t &pubkey, bool check_netwvshash, uint32_t chunk_size) :
        ContentTransfer(LIVE_TRANSFER), pubkey_(pubkey), chunk_size_(chunk_size), am_source_(false),
        filename_(filename), last_chunkid_(0), offset_(0),
        chunks_since_sign_(0),nchunks_per_sign_(0)
{
    Initialize(check_netwvshash);

    picker_ = new SimpleLivePiecePicker(this);
    picker_->Randomize(rand()&63);
}


void LiveTransfer::Initialize(bool check_netwvshash,uint64_t disc_wnd)
{
    GlobalAdd();

    Handshake hs;
    if (check_netwvshash)
    {
	if (nchunks_per_sign_ == 1)
	    hs.cont_int_prot_ = POPT_CONT_INT_PROT_SIGNALL;
	else
	    hs.cont_int_prot_ = POPT_CONT_INT_PROT_UNIFIED_MERKLE;
    }
    else
	hs.cont_int_prot_ = POPT_CONT_INT_PROT_NONE;
    hs.live_disc_wnd_ = disc_wnd;

    fprintf(stderr,"LiveTransfer::Initialize: cipm %d\n", hs.cont_int_prot_);
    fprintf(stderr,"LiveTransfer::Initialize: ldw  %llu\n", hs.live_disc_wnd_);

    SetDefaultHandshake(hs);

    std::string destdir;
    int ret = file_exists_utf8(filename_);
    if (ret == 2) {
        // Filename is a directory, download to swarmid-as-hex file there
        destdir = filename_;
        filename_ = destdir+FILE_SEP+pubkey_.hex();
    } else {
        destdir = dirname_utf8(filename_);
        if (destdir == "")
            destdir = ".";
    }

    // Live, delete any existing storage
    (void)remove_utf8(filename_);

    // MULTIFILE
    uint64_t ldwb = hs.live_disc_wnd_;
    if (ldwb != POPT_LIVE_DISC_WND_ALL)
	ldwb *= chunk_size_;
    storage_ = new Storage(filename_,destdir,td_,ldwb);

    fprintf(stderr,"LiveTransfer::Initialize: def cipm %d\n", def_hs_out_.cont_int_prot_ );

    if (hs.cont_int_prot_ == POPT_CONT_INT_PROT_UNIFIED_MERKLE)
    {
	hashtree_ = new LiveHashTree(storage_,481,chunk_size_);
    }
    else
	hashtree_ = NULL;
}


LiveTransfer::~LiveTransfer()
{
    if (picker_ != NULL)
	delete picker_;

    GlobalDel();
}



void LiveTransfer::GlobalAdd() {

    int idx = liveswarms.size();
    td_ = idx + TRANSFER_DESCR_LIVE_OFFSET;

    if (liveswarms.size()<idx+1)
        liveswarms.resize(idx+1);
    liveswarms[idx] = this;
}


void LiveTransfer::GlobalDel() {
    int idx = td_ - TRANSFER_DESCR_LIVE_OFFSET;
    liveswarms[idx] = NULL;
}


LiveTransfer *LiveTransfer::FindByTD(int td)
{
    int idx = td - TRANSFER_DESCR_LIVE_OFFSET;
    return idx<liveswarms.size() ? (LiveTransfer *)liveswarms[idx] : NULL;
}

LiveTransfer* LiveTransfer::FindBySwarmID(const Sha1Hash& swarmid) {
    for(int i=0; i<liveswarms.size(); i++)
        if (liveswarms[i] && liveswarms[i]->swarm_id()==swarmid)
            return liveswarms[i];
    return NULL;
}


tdlist_t LiveTransfer::GetTransferDescriptors() {
    tdlist_t tds;
    for(int i=0; i<liveswarms.size(); i++)
        if (liveswarms[i] != NULL)
            tds.push_back(i+TRANSFER_DESCR_LIVE_OFFSET);
    return tds;
}




uint64_t      LiveTransfer::SeqComplete() {

    if (am_source_)
    {
        uint64_t seqc = ack_out()->find_empty().base_offset();
	return seqc*chunk_size_;
    }
    bin_t hpos = ((LivePiecePicker *)picker())->GetHookinPos();
    bin_t cpos = ((LivePiecePicker *)picker())->GetCurrentPos();
    if (hpos == bin_t::NONE || cpos == bin_t::NONE)
        return 0;
    else
    { 
        uint64_t seqc = cpos.layer_offset() - hpos.layer_offset();
        return seqc*chunk_size_;
    }
}


uint64_t      LiveTransfer::GetHookinOffset() {

    bin_t hpos = ((LivePiecePicker *)picker())->GetHookinPos();
    uint64_t seqc = hpos.layer_offset();
    return seqc*chunk_size_;
}




int LiveTransfer::AddData(const void *buf, size_t nbyte)
{
    //fprintf(stderr,"live: AddData: writing to storage %lu\n", nbyte);

    // Save chunk on disk
    int ret = storage_->Write(buf,nbyte,offset_);
    if (ret < 0) {
        print_error("live: create: error writing to storage");
        return ret;
    }
    else
        fprintf(stderr,"live: AddData: stored " PRISIZET " bytes\n", nbyte);

    uint64_t till = std::max((size_t)1,nbyte/chunk_size_);
    bhstvector totalnewpeaktuples;
    bool newepoch=false;
    for (uint64_t c=0; c<till; c++)
    {
        // New chunk is here
        bin_t chunkbin(0,last_chunkid_);
        ack_out_.set(chunkbin);

        last_chunkid_++;
        offset_ += chunk_size_;

        // SIGNPEAK
        if (def_hs_out_.cont_int_prot_ == POPT_CONT_INT_PROT_UNIFIED_MERKLE)
        {
            LiveHashTree *umt = (LiveHashTree *)hashtree();
            size_t bufidx = c*chunk_size_;
            char *bufptr = ((char *)buf)+bufidx;
            size_t s = std::min(chunk_size_,nbyte-bufidx);
            // Build dynamic hash tree
            umt->AddData(bufptr,s);

            // Create new signed peaks after N chunks
            // Note: this means that if we use a file as input, the last < N
            // chunks never get announced.
            chunks_since_sign_++;
            if (chunks_since_sign_ == nchunks_per_sign_)
            {
        	int old = umt->GetCurrentSignedPeakTuples().size();

        	bhstvector newpeaktuples = umt->UpdateSignedPeaks();
        	fprintf(stderr,"live: AddData: UMT: adding %d to %d\n", newpeaktuples.size(), totalnewpeaktuples.size() );
        	totalnewpeaktuples.insert(totalnewpeaktuples.end(), newpeaktuples.begin(), newpeaktuples.end() );

        	fprintf(stderr,"live: AddData: UMT: signed peaks old %d new %d\n", old, umt->GetCurrentSignedPeakTuples().size() );

        	chunks_since_sign_ = 0;
        	newepoch = true;

		// Arno, 2013-02-26: Can only send HAVEs covered by signed peaks
		signed_ack_out_.clear();
		bhstvector cursignpeaktuples = umt->GetCurrentSignedPeakTuples();
		bhstvector::iterator iter;
		for (iter= cursignpeaktuples.begin(); iter != cursignpeaktuples.end(); iter++)
		{
		    BinHashSigTuple bhst = *iter;
		    signed_ack_out_.set(bhst.bin());
		}

		// Forget old part of tree
		if (def_hs_out_.live_disc_wnd_ != POPT_LIVE_DISC_WND_ALL)
		{
		    OnDataPruneTree(def_hs_out_,bin_t(0,last_chunkid_),nchunks_per_sign_);
		}
            }
        }
        else
            newepoch = true;
    }

    fprintf(stderr,"live: AddData: added till chunkid %lli\n", last_chunkid_);
    dprintf("%s %%0 live: AddData: added till chunkid %lli\n", tintstr(), last_chunkid_);

    // Arno, 2013-02-26: When UNIFIED_MERKLE chunks are published in batches
    // of nchunks_per_sign_
    if (!newepoch)
	return 0;


    // Announce chunks to peers
    //fprintf(stderr,"live: AddData: announcing to %d channel\n", mychannels_.size() );

    channels_t::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
        Channel *c = *iter;
        fprintf(stderr,"live: AddData: announce to channel %d\n", c->id() );
        dprintf("%s %%0 live: AddData: announce to channel %d\n", tintstr(), c->id() );
        //DDOS
        if (c->is_established())
        {
            fprintf(stderr,"live: AddData: announce to channel %d new signed %d\n", c->id(), totalnewpeaktuples.size() );
            c->AddSinceSignedPeakTuples(totalnewpeaktuples);
            c->LiveSend();
        }
    }

    return 0;
}


void LiveTransfer::UpdateOperational()
{
}


binmap_t *LiveTransfer::ack_out_signed()
{
    if (!am_source_ || hashtree() == NULL)
	return &ack_out_;

    // Arno, 2013-02-26: Cannot send HAVEs not covered by signed peak
    return &signed_ack_out_;
}

binmap_t *LiveTransfer::ack_out()
{
    if (GetDefaultHandshake().cont_int_prot_ == POPT_CONT_INT_PROT_UNIFIED_MERKLE)
	return hashtree_->ack_out();
    else
	return &ack_out_; // tree less, use local binmap.
}


void LiveTransfer::OnVerifiedPeakHash(BinHashSigTuple &bhst, Channel *srcc)
{
    // Channel c received a correctly signed peak hash. Schedule for
    // distribution to other channels.

    bhstvector newpeaktuples;
    newpeaktuples.push_back(bhst);

    channels_t::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
        Channel *c = *iter;
        if (c != srcc && c->is_established())
        {
            fprintf(stderr,"live: OnVerified: announce to channel %d\n", c->id() );
            c->AddSinceSignedPeakTuples(newpeaktuples);
        }
    }
}


void LiveTransfer::OnDataPruneTree(Handshake &hs_out, bin_t pos, uint32_t nchunks2forget)
{
    uint64_t lastchunkid = pos.layer_offset();

    int64_t oldcid = ((int64_t)lastchunkid - (int64_t)hs_out.live_disc_wnd_);
    if (oldcid > 0)
    {
	// Find subtree left of window with width nchunks2forget that can be pruned
	uint64_t extracid = oldcid % nchunks2forget;
	uint64_t startcid = oldcid - extracid;
	int64_t leftcid = ((int64_t)startcid - (int64_t)nchunks2forget);
	if (leftcid >= 0)
	{
	    bin_t leftpos(0,leftcid);

	    bin_t::uint_t nchunks_per_sign_layer = (bin_t::uint_t)log2((double)nchunks2forget);
	    for (int h=0; h<nchunks_per_sign_layer; h++)
	    {
		leftpos = leftpos.parent();
	    }

	    // Find biggest subtree to remove
	    if (leftpos.is_right())
	    {
		while (leftpos.parent().is_right())
		{
		    leftpos = leftpos.parent();
		}
	    }
	    fprintf(stderr,"live: OnDataPruneTree: prune %s log %lf nchunks %d window %llu when %llu\n", leftpos.str().c_str(), log2((double)lastchunkid), nchunks2forget, hs_out.live_disc_wnd_, lastchunkid );
	    LiveHashTree *umt = (LiveHashTree *)hashtree();
	    umt->PruneTree(leftpos);
	}
    }
}



/*
 * Channel extensions for live
 */

void Channel::LiveSend()
{
    //fprintf(stderr,"live: LiveSend: channel %d\n", id() );

    if (evsendlive_ptr_ == NULL)
    {
        evsendlive_ptr_ = new struct event;
        // Arno, 2013-02-01: Don't reassign, causes crashes.
        evtimer_assign(evsendlive_ptr_,evbase,&Channel::LibeventSendCallback,this);
    }
    //fprintf(stderr,"live: LiveSend: next %lld\n", next_send_time_ );
    evtimer_add(evsendlive_ptr_,tint2tv(next_send_time_));
}


