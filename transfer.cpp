/*
 *  transfer.cpp
 *  some transfer-scope code
 *
 *  Created by Victor Grishchenko on 10/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <errno.h>
#include <string>
#include <sstream>
#include "swift.h"

#include "ext/seq_picker.cpp" // FIXME FIXME FIXME FIXME
#include "ext/vod_picker.cpp"

using namespace swift;

#define BINHASHSIZE (sizeof(bin64_t)+sizeof(Sha1Hash))


#define TRACKER_RETRY_INTERVAL_START	(5*TINT_SEC)
#define TRACKER_RETRY_INTERVAL_EXP		1.1				// exponent used to increase INTERVAL_START
#define TRACKER_RETRY_INTERVAL_MAX		(1800*TINT_SEC) // 30 minutes

// FIXME: separate Bootstrap() and Download(), then Size(), Progress(), SeqProgress()

FileTransfer::FileTransfer(std::string filename, const Sha1Hash& root_hash, bool check_hashes, uint32_t chunk_size, bool zerostate) :
    ContentTransfer(), tracker_(), tracker_retry_interval_(TRACKER_RETRY_INTERVAL_START), tracker_retry_time_(NOW), zerostate_(zerostate)
{
    std::string destdir;
	int ret = file_exists_utf8(filename);
	if (ret == 2 && root_hash != Sha1Hash::ZERO) {
		// Filename is a directory, download root_hash there
		destdir = filename;
		filename = destdir+FILE_SEP+root_hash.hex();
	} else {
		destdir = dirname_utf8(filename);
		if (destdir == "")
			destdir = ".";
	}

	// MULTIFILE
    storage_ = new Storage(filename,destdir,fd());

	std::string hash_filename;
	hash_filename.assign(filename);
	hash_filename.append(".mhash");

	std::string binmap_filename;
	binmap_filename.assign(filename);
	binmap_filename.append(".mbinmap");

	if (!zerostate_)
	{
		hashtree_ = (HashTree *)new MmapHashTree(storage_,root_hash,chunk_size,hash_filename,check_hashes,binmap_filename);

		if (ENABLE_VOD_PIECEPICKER) {
			// Ric: init availability
			availability_ = new Availability();
			// Ric: TODO assign picker based on input params...
			picker_ = new VodPiecePicker(this);
		}
		else
			picker_ = new SeqPiecePicker(this);
		picker_->Randomize(rand()&63);
	}
	else
	{
		// ZEROHASH
		hashtree_ = (HashTree *)new ZeroHashTree(storage_,root_hash,chunk_size,hash_filename,check_hashes,binmap_filename);
	}

    init_time_ = Channel::Time();

    // SAFECLOSE
    evtimer_assign(&evclean_,Channel::evbase,&FileTransfer::LibeventCleanCallback,this);
    evtimer_add(&evclean_,tint2tv(5*TINT_SEC));
}


// SAFECLOSE
void FileTransfer::LibeventCleanCallback(int fd, short event, void *arg)
{
	// Arno, 2012-02-24: Why-oh-why, update NOW
	Channel::Time();

	FileTransfer *ft = (FileTransfer *)arg;
	if (ft == NULL)
		return;

	// STL and MS and conditional delete from set not a happy place :-(
	std::set<Channel *>	delset;
	std::set<Channel *>::iterator iter;
	bool hasestablishedpeers=false;
	for (iter=ft->mychannels_.begin(); iter!=ft->mychannels_.end(); iter++)
	{
		Channel *c = *iter;
		if (c != NULL) {
			if (c->IsScheduled4Close())
				delset.insert(c);

			if (c->is_established ()) {
				hasestablishedpeers = true;
				//fprintf(stderr,"%s peer %s\n", ft->hashtree()->root_hash().hex().c_str(), c->peer().str() );
			}
		}
	}
	for (iter=delset.begin(); iter!=delset.end(); iter++)
	{
		Channel *c = *iter;
		dprintf("%s #%u clean cb close\n",tintstr(),c->id());
		c->Close();
		ft->mychannels_.erase(c);
		delete c;
    }

	// Arno, 2012-02-24: Check for liveliness.
	ft->ReConnectToTrackerIfAllowed(hasestablishedpeers);

	// Reschedule cleanup
    evtimer_add(&(ft->evclean_),tint2tv(5*TINT_SEC));
}


void FileTransfer::ReConnectToTrackerIfAllowed(bool hasestablishedpeers)
{
	// If I'm not connected to any
	// peers, try to contact the tracker again.
	if (!hasestablishedpeers)
	{
		if (NOW > tracker_retry_time_)
		{
			ConnectToTracker();

			tracker_retry_interval_ *= TRACKER_RETRY_INTERVAL_EXP;
			if (tracker_retry_interval_ > TRACKER_RETRY_INTERVAL_MAX)
				tracker_retry_interval_ = TRACKER_RETRY_INTERVAL_MAX;
			tracker_retry_time_ = NOW + tracker_retry_interval_;
		}
	}
	else
	{
		tracker_retry_interval_ = TRACKER_RETRY_INTERVAL_START;
		tracker_retry_time_ = NOW + tracker_retry_interval_;
	}
}


void FileTransfer::ConnectToTracker()
{
	Channel *c = NULL;
    if (tracker_ != Address())
    	c = new Channel(this,INVALID_SOCKET,tracker_);
    else if (Channel::tracker!=Address())
    	c = new Channel(this);
}


FileTransfer::~FileTransfer ()
{
	delete hashtree_;
	if (!IsZeroState())
	{
		delete picker_;
		delete availability_;
	}
}


