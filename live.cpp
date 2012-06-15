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


LiveTransfer::LiveTransfer(std::string filename, const Sha1Hash& swarm_id,bool amsource,size_t chunk_size) :
		ContentTransfer(LIVE_TRANSFER), swarm_id_(swarm_id), am_source_(amsource), filename_(filename),
		chunk_size_(chunk_size), last_chunkid_(0), offset_(0)
{
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
    storage_ = new Storage(filename,destdir,fd());
}


LiveTransfer::~LiveTransfer()
{
	delete picker_;
}


uint64_t      LiveTransfer::SeqComplete() {

	bin_t hpos = ((LivePiecePicker *)picker())->GetHookinPos();
	bin_t cpos = ((LivePiecePicker *)picker())->GetCurrentPos();
    uint64_t seqc = cpos.layer_offset() - hpos.layer_offset();
	return seqc*chunk_size_;
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
		fprintf(stderr,"live: AddData: adding chunkid %lli\n", last_chunkid_);

		// New chunk is here
		bin_t chunkbin(0,last_chunkid_);
		ack_out_.set(chunkbin);

		last_chunkid_++;
		offset_ += chunk_size_;
	}

	// Announce chunks to peers
	//fprintf(stderr,"live: AddData: announcing to %d channel\n", mychannels_.size() );

    std::set<Channel *>::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
		Channel *c = *iter;
		fprintf(stderr,"live: AddData: announce to channel %d\n", c->id() );
		//DDOS
		if (c->is_established())
			c->LiveSend();
    }

    return 0;
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
