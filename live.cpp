/*
 *  live.cpp
 *  Subclass of ContentTransfer for live streaming.
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
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

// Ric: tmp => changed from size_t to uint32_t to avoid compilation problems (otherwise change the def in swift.h)
LiveTransfer::LiveTransfer(std::string filename, const Sha1Hash& swarm_id,bool amsource,uint32_t chunk_size) :
        ContentTransfer(LIVE_TRANSFER), swarm_id_(swarm_id), am_source_(amsource), filename_(filename),
        chunk_size_(chunk_size), last_chunkid_(0), offset_(0)
{
    GlobalAdd();

    picker_ = new SimpleLivePiecePicker(this);
    picker_->Randomize(rand()&63);

    std::string destdir;
    int ret = file_exists_utf8(filename);
    if (ret == 2 && swarm_id != Sha1Hash::ZERO) {
        // Filename is a directory, download to swarmid-as-hex file there
        destdir = filename;
        filename = destdir+FILE_SEP+swarm_id.hex();
    } else {
        destdir = dirname_utf8(filename);
        if (destdir == "")
            destdir = ".";
    }

    // MULTIFILE
    storage_ = new Storage(filename,destdir,td_);
}


LiveTransfer::~LiveTransfer()
{
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
        uint64_t seqc = ack_out_.find_empty().base_offset();
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

#ifdef _WIN32
    uint64_t till = max(1,nbyte/chunk_size_);
#else
    uint64_t till = std::max((size_t)1,nbyte/chunk_size_);
#endif
    for (uint64_t c=0; c<till; c++)
    {
        // New chunk is here
        bin_t chunkbin(0,last_chunkid_);
        ack_out_.set(chunkbin);

        last_chunkid_++;
        offset_ += chunk_size_;
    }

    fprintf(stderr,"live: AddData: added till chunkid %lli\n", last_chunkid_);
    dprintf("%s %%0 live: AddData: added till chunkid %lli\n", tintstr(), last_chunkid_);

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
            c->LiveSend();
    }

    return 0;
}


void LiveTransfer::UpdateOperational()
{
}


void Channel::LiveSend()
{
    //fprintf(stderr,"live: LiveSend: channel %d\n", id() );

    if (evsendlive_ptr_ == NULL)
        evsendlive_ptr_ = new struct event;

    evtimer_assign(evsendlive_ptr_,evbase,&Channel::LibeventSendCallback,this);
    //fprintf(stderr,"live: LiveSend: next %lld\n", next_send_time_ );
    evtimer_add(evsendlive_ptr_,tint2tv(next_send_time_));
}
