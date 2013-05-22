/*
 *  live.cpp
 *  Subclass of ContentTransfer for live streaming.
 *
 *  Arno: Currently uses ever increasing chunk IDs. The binmap datastructure
 *  can store this quite efficiently, as long as there are few holes.
 *  The Storage object can save all chunks or wrap around, that is, a certain
 *  modulo is applied that overwrites earlier chunks.
 *  This modulo is equivalent to the live discard window (see IETF PPSPP spec).
 *  This overwriting can be done both at the source and in a client.
 *
 * TODO:
 *  - temp check if not diverging too much from source via CalculateHookinPos() authoritative.
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 *  TODO:
 *  - picker that handles total chunk loss
 *       DONE
 *  - picker than optimizes sharing (cf. small swarms sharing)
 *      * don't have piece_due
 *      * need rarest or latest bla-bla (UTorino guy)
 *  - aux live seeders?
 *  - restartable live source (idea for UMT: just start new subtree, remembering transient root hash of previous to be used when tree grows in level above current size.)
 *
 *  - avg upload buggy? Check problem found by Riccardo.
 *
 *  - Windows needs live discard window < max as it doesn't have sparse files,
 *    so tuning in at chunk 197992 without a smaller window causes a
 *    file-allocation stall in client.sh
 *
 *  - (related) Pass live discard window via CMDGW interface.
 *
 *  - Test if CIPM None still works.
 *
 *  - sendrecv.cpp Don't add DATA+bin+etc if read of data fails.
 *
 *  - Idea: client skips when signed peaks arrives that is way past hook-in point.
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
// live transfers get a transfer description (TD) above this offset
#define TRANSFER_DESCR_LIVE_OFFSET	4000000

/** A constructor for a live source. */
LiveTransfer::LiveTransfer(std::string filename, const pubkey_t &pubkey, const privkey_t &privkey, std::string checkpoint_filename, bool check_netwvshash, uint32_t nchunks_per_sign, uint64_t disc_wnd, uint32_t chunk_size) :
	ContentTransfer(LIVE_TRANSFER), chunk_size_(chunk_size), am_source_(true),
	filename_(filename), last_chunkid_(0), offset_(0),
	chunks_since_sign_(0),nchunks_per_sign_(nchunks_per_sign),
	pubkey_(pubkey), privkey_(privkey),
	checkpoint_filename_(checkpoint_filename), checkpoint_bin_(bin_t::NONE)
{
    Initialize(check_netwvshash,disc_wnd);

    picker_ = NULL;

    BinHashSigTuple roottup = ReadCheckpoint();
    if (GetDefaultHandshake().cont_int_prot_ == POPT_CONT_INT_PROT_NONE)
    {
	// Start generating chunks from rootbin.base_right()+1
    }
    else if (GetDefaultHandshake().cont_int_prot_ == POPT_CONT_INT_PROT_UNIFIED_MERKLE)
    {
	/*
	 * Read live source state from checkpoint. This is the info about
	 * the virtual root node of the last tree from the previous instance.
	 * We turn this root node into a first peak in our new tree, but
	 * do not advertise its chunks. Clients should skip over the unused
	 * parts of the old tree and start downloading the chunks in the new
	 * part of our tree.
	 *
	 *           new virtual root
	 *             /          \
	 *            /            \
	 *     checkpoint        first new chunk
	 *         root
	 */
	LiveHashTree *umt = (LiveHashTree *)hashtree_;
	if (roottup.bin() != bin_t::NONE)
	{
	    BinHashSigTuple gottup = umt->InitFromCheckpoint(roottup);
	    checkpoint_bin_ = gottup.bin();
	    last_chunkid_ = checkpoint_bin_.base_right().base_offset()+1;
	    offset_ = last_chunkid_ * chunk_size_;
	}

	bhstvector cursignpeaktuples = umt->GetCurrentSignedPeakTuples();
	bhstvector::iterator iter;
	for (iter= cursignpeaktuples.begin(); iter != cursignpeaktuples.end(); iter++)
	{
	    BinHashSigTuple bhst = *iter;
	    fprintf(stderr,"live: source: restored sigpeak: %s %s %s lastchunkid %llu\n", bhst.bin().str().c_str(), bhst.hash().hex().c_str(), bhst.sig().hex().c_str(), last_chunkid_ );
	}
    }
    else // SIGNALL
    {
	// Start generating chunks from rootbin.base_right()+1
    }
}


/** A constructor for live client. */
LiveTransfer::LiveTransfer(std::string filename, const pubkey_t &pubkey, bool check_netwvshash, uint64_t disc_wnd, uint32_t chunk_size) :
        ContentTransfer(LIVE_TRANSFER), pubkey_(pubkey), chunk_size_(chunk_size), am_source_(false),
        filename_(filename), last_chunkid_(0), offset_(0),
        chunks_since_sign_(0),nchunks_per_sign_(0),
        checkpoint_filename_(""), checkpoint_bin_(bin_t::NONE)
{
    Initialize(check_netwvshash,disc_wnd);

    picker_ = new SharingLivePiecePicker(this);
    picker_->Randomize(rand()&63);
}


void LiveTransfer::Initialize(bool check_netwvshash,uint64_t disc_wnd)
{
    GlobalAdd();

    Handshake hs;
    if (check_netwvshash)
    {
#if ENABLE_LIVE_AUTH == 1
    	if (nchunks_per_sign_ == 1)
            hs.cont_int_prot_ = POPT_CONT_INT_PROT_SIGNALL;
        else
            hs.cont_int_prot_ = POPT_CONT_INT_PROT_UNIFIED_MERKLE;
#else
        hs.cont_int_prot_ = POPT_CONT_INT_PROT_NONE;
#endif
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
    fprintf(stderr,"%s live: AddData: writing to storage %lu\n", tintstr(), nbyte);

    // Save chunk on disk
    int ret = storage_->Write(buf,nbyte,offset_);
    if (ret < 0) {
        print_error("live: create: error writing to storage");
        return ret;
    }
    else
        fprintf(stderr,"%s live: AddData: stored " PRISIZET " bytes\n", tintstr(), nbyte);

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

        	// LIVECHECKPOINT
        	if (checkpoint_filename_.length() > 0)
        	{
        	    BinHashSigTuple roottup = umt->GetRootTuple();
        	    WriteCheckpoint(roottup);
        	}

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
		// LIVECHECKPOINT, see constructor
		signed_ack_out_.reset(checkpoint_bin_);

		// TEST
		/* fprintf(stderr,"live: AddData: UMT: checkp %s\n", checkpoint_bin_.str().c_str() );
		bin_t bf = signed_ack_out_.find_filled();
		bin_t be = signed_ack_out_.find_empty();
		bin_t sbe = signed_ack_out_.find_empty(checkpoint_bin_);

		fprintf(stderr,"live: AddData: UMT: bf %s be %s sbe %s\n", bf.str().c_str(), be.str().c_str(), sbe.str().c_str() );
		*/

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
        dprintf("%s %%0 live: AddData: record %d peaks for channel %d\n", tintstr(), totalnewpeaktuples.size(), c->id() );
        c->AddSinceSignedPeakTuples(totalnewpeaktuples);
        //DDOS
        if (c->is_established())
        {
            dprintf("%s %%0 live: AddData: send on channel %d\n", tintstr(), c->id() );
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
        // Arno, 2013-05-13: Also record for channels being established
        if (c != srcc && !c->IsSource())
        {
            //fprintf(stderr,"live: OnVerified: schedule for channel %d\n", c->id() );
            c->AddSinceSignedPeakTuples(newpeaktuples);
        }
    }

    // Arno, 2013-05-13: clients need to know nchunks_per_sig for right hashes
    // (see sendrecv.cpp::AddLivePeakRightHashes(). Do autodetect.
    if (nchunks_per_sign_ == 0 || bhst.bin().base_length() < nchunks_per_sign_)
    {
	nchunks_per_sign_ = bhst.bin().base_length();
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
	    //fprintf(stderr,"live: OnDataPruneTree: prune %s log %lf nchunks %d window %llu when %llu\n", leftpos.str().c_str(), log2((double)lastchunkid), nchunks2forget, hs_out.live_disc_wnd_, lastchunkid );
	    LiveHashTree *umt = (LiveHashTree *)hashtree();
	    umt->PruneTree(leftpos);
	}
    }
}


int LiveTransfer::WriteCheckpoint(BinHashSigTuple &roottup)
{
    // FORMAT: (layer,layeroff) roothash-in-hex rootsig-in-hex\n
    std::string s = roottup.bin().str()+" "+roottup.hash().hex()+" "+roottup.sig().hex()+"\n";
    char *cstr = new char[strlen(s.c_str())+1];

    strcpy(cstr,s.c_str());

    // TODO: atomic?
    int fd = open_utf8(checkpoint_filename_.c_str(),OPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (fd < 0)
    {
	print_error("could not write checkpoint file");
	delete cstr;
	return fd;
    }
    int ret = write(fd,cstr,strlen(cstr));
    if (ret < 0)
    {
	print_error("could not write live checkpoint data");
	delete cstr;
	return ret;
    }
    delete cstr;
    ret = close(fd);

    return ret;
}


BinHashSigTuple LiveTransfer::ReadCheckpoint()
{
    // TODO: atomic?
    int fd = open_utf8(checkpoint_filename_.c_str(),ROOPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (fd < 0)
    {
	print_error("could not read live checkpoint file");
	return BinHashSigTuple(bin_t::NONE,Sha1Hash::ZERO,Signature());
    }
    char buffer[1024];
    int ret = read(fd,buffer,1024);
    if (ret < 0)
    {
	print_error("could not read live checkpoint data");
	return BinHashSigTuple(bin_t::NONE,Sha1Hash::ZERO,Signature());
    }
    close(fd);

    // Overwrite \n with end-of-string
    buffer[ret-1] = '\0';
    std::string pstr(buffer);

    std::string binstr;
    std::string hashstr;
    std::string sigstr;
    int sidx = pstr.find(" ");
    if (sidx == std::string::npos)
    {
	print_error("could not parsing live checkpoint: no bin");
	return BinHashSigTuple(bin_t::NONE,Sha1Hash::ZERO,Signature());
    }
    else
    {
	binstr = pstr.substr(0,sidx);
	int midx = pstr.find(" ",sidx+1);
	if (midx == std::string::npos)
	{
	    print_error("could not parsing live checkpoint: no hash");
	    return BinHashSigTuple(bin_t::NONE,Sha1Hash::ZERO,Signature());
	}
	else
	{
	    hashstr = pstr.substr(sidx+1,midx-sidx-1);
	    sigstr = pstr.substr(midx+1);
	}
    }

    sidx = binstr.find(",");
    if (sidx == std::string::npos)
    {
	print_error("could not parsing live checkpoint: bin bad");
	return BinHashSigTuple(bin_t::NONE,Sha1Hash::ZERO,Signature());
    }

    std::string layerstr=binstr.substr(1,sidx-1);
    std::string layeroffstr=binstr.substr(sidx+1,binstr.length()-sidx-2);

    int layer;
    ret = sscanf(layerstr.c_str(),"%d",&layer);
    if (ret != 1)
    {
	print_error("could not parsing live checkpoint: bin layer bad");
	return BinHashSigTuple(bin_t::NONE,Sha1Hash::ZERO,Signature());
    }
    bin_t::uint_t layeroff;
    ret = sscanf(layeroffstr.c_str(),"%llu",&layeroff);
    if (ret != 1)
    {
	print_error("could not parsing live checkpoint: bin layer off bad");
	return BinHashSigTuple(bin_t::NONE,Sha1Hash::ZERO,Signature());
    }

    bin_t rootbin(layer,layeroff);
    Sha1Hash roothash = Sha1Hash(true,hashstr.c_str());
    Signature rootsig = Signature(true,(const uint8_t *)sigstr.c_str(),(uint16_t)sigstr.length());

    //fprintf(stderr,"CHECKPOINT parsed <%s> %d %llu <%s> <%s>\n", rootbin.str().c_str(), layer, layeroff, roothash.hex().c_str(), rootsig.hex().c_str() );

    return BinHashSigTuple(rootbin,roothash,rootsig);
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


