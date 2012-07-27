/*
 *  content.cpp
 *  Superclass of FileTransfer and LiveTransfer
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 */
#include "swift.h"
#include <cfloat>


using namespace swift;

/*
 * Global Variables
 */
std::vector<ContentTransfer*> ContentTransfer::swarms;

/*
 * Local Constants
 */
#define TRACKER_RETRY_INTERVAL_START	(5*TINT_SEC)
#define TRACKER_RETRY_INTERVAL_EXP		1.1				// exponent used to increase INTERVAL_START
#define TRACKER_RETRY_INTERVAL_MAX		(1800*TINT_SEC) // 30 minutes



ContentTransfer::ContentTransfer(transfer_t ttype) :  ttype_(ttype), mychannels_(), cb_installed(0),
		speedzerocount_(0), tracker_(),
		tracker_retry_interval_(TRACKER_RETRY_INTERVAL_START),
		tracker_retry_time_(NOW)
{
	GlobalAdd();

    cur_speed_[DDIR_UPLOAD] = MovingAverageSpeed();
    cur_speed_[DDIR_DOWNLOAD] = MovingAverageSpeed();
    max_speed_[DDIR_UPLOAD] = DBL_MAX;
    max_speed_[DDIR_DOWNLOAD] = DBL_MAX;

	fprintf(stderr,"ContentTransfer::ContentTransfer: Add timer\n" );
    evtimer_assign(&evclean_,Channel::evbase,&ContentTransfer::LibeventCleanCallback,this);
    evtimer_add(&evclean_,tint2tv(5*TINT_SEC));
}


ContentTransfer::~ContentTransfer()
{
	fprintf(stderr,"ContentTransfer::~ContentTransfer\n" );

	CloseChannels(mychannels_);
	if (storage_ != NULL)
		delete storage_;

	// Arno, 2012-02-06: Cancel cleanup timer, otherwise chaos!
	evtimer_del(&evclean_);

	GlobalDel();
}


void ContentTransfer::CloseChannels(std::set<Channel *>	delset)
{
	std::set<Channel *>::iterator iter;
	for (iter=delset.begin(); iter!=delset.end(); iter++)
	{
		Channel *c = *iter;
		c->Close();
		mychannels_.erase(c);
		delete c;
		// ~Channel removes it from Channel::channels
	}
}

// SAFECLOSE
void ContentTransfer::GarbageCollectChannels()
{
	// STL and MS and conditional delete from set not a happy place :-(
	std::set<Channel *>	delset;
	std::set<Channel *>::iterator iter;
	bool hasestablishedpeers=false;
	for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
	{
		Channel *c = *iter;
		if (c != NULL) {
			if (c->IsScheduled4Delete())
				delset.insert(c);

			if (c->is_established ())
				hasestablishedpeers = true;
		}
	}
	CloseChannels(delset);

	// Arno, 2012-02-24: Check for liveliness.
	ReConnectToTrackerIfAllowed(hasestablishedpeers);
}


void ContentTransfer::LibeventCleanCallback(int fd, short event, void *arg)
{
	fprintf(stderr,"ContentTransfer::CleanCallback\n");

	// Arno, 2012-02-24: Why-oh-why, update NOW
	Channel::Time();

	ContentTransfer *ct = (ContentTransfer *)arg;
	if (ct == NULL)
		return;

	ct->GarbageCollectChannels();

	// Reschedule cleanup
    evtimer_assign(&(ct->evclean_),Channel::evbase,&ContentTransfer::LibeventCleanCallback,ct);
    evtimer_add(&(ct->evclean_),tint2tv(5*TINT_SEC));
}



void ContentTransfer::ReConnectToTrackerIfAllowed(bool hasestablishedpeers)
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


void ContentTransfer::ConnectToTracker()
{
	// SWIFTPROC
	Channel *c = NULL;
	// Arno, 2012-01-09: LIVE Old hack: the tracker is assumed to be the live source
    if (tracker_ != Address())
    	c = new Channel(this,INVALID_SOCKET,tracker_,true);
    else if (Channel::tracker!=Address())
    	c = new Channel(this,INVALID_SOCKET,Channel::tracker,true);
}




void ContentTransfer::GlobalAdd() {

	fd_ = swarms.size();

	if (swarms.size()<fd_+1)
		swarms.resize(fd_+1);
	swarms[fd_] = this;
}


void ContentTransfer::GlobalDel() {
	swarms[fd_] = NULL;
}


ContentTransfer* ContentTransfer::Find (const Sha1Hash& swarmid) {
    for(int i=0; i<swarms.size(); i++)
        if (swarms[i] && swarms[i]->swarm_id()==swarmid)
            return swarms[i];
    return NULL;
}



bool ContentTransfer::OnPexIn (const Address& addr) {

	//fprintf(stderr,"ContentTransfer::OnPexIn: %s\n", addr.str() );
	// Arno: this brings safety, but prevents private swift installations.
	// TODO: detect public internet.
	//if (addr.is_private())
	//	return false;

	std::set<Channel *>::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
	    Channel *c = *iter;
	    if (c != NULL && c->peer()==addr)
            return false; // already connected or connecting, Gertjan fix = return false
    }
    // Gertjan fix: PEX redo
    if (mychannels_.size()<SWIFT_MAX_CONNECTIONS)
        new Channel(this,Channel::default_socket(),addr);
    return true;
}

//Gertjan
Channel *ContentTransfer::RandomChannel(Channel *notc) {
    std::vector<Channel *>	choose_from;
	std::set<Channel *>::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
	    Channel *c = *iter;
	    if (c != NULL && c != notc && c->is_established())
			choose_from.push_back(c);
    }
    if (choose_from.size() == 0)
        return NULL;

    return choose_from[rand() % choose_from.size()];
}





// RATELIMIT
void		ContentTransfer::OnRecvData(int n)
{
	// Got n ~ 32K
	cur_speed_[DDIR_DOWNLOAD].AddPoint((uint64_t)n);
}

void		ContentTransfer::OnSendData(int n)
{
	// Sent n ~ 1K
	cur_speed_[DDIR_UPLOAD].AddPoint((uint64_t)n);
}


void		ContentTransfer::OnSendNoData()
{
	// AddPoint(0) everytime we don't AddData gives bad speed measurement
	// batch 32 such events into 1.
	speedzerocount_++;
	if (speedzerocount_ >= 32)
	{
		cur_speed_[DDIR_UPLOAD].AddPoint((uint64_t)0);
		speedzerocount_ = 0;
	}
}


double		ContentTransfer::GetCurrentSpeed(data_direction_t ddir)
{
	return cur_speed_[ddir].GetSpeedNeutral();
}


void		ContentTransfer::SetMaxSpeed(data_direction_t ddir, double m)
{
	max_speed_[ddir] = m;
	// Arno, 2012-01-04: Be optimistic, forget history.
	cur_speed_[ddir].Reset();
}


double		ContentTransfer::GetMaxSpeed(data_direction_t ddir)
{
	return max_speed_[ddir];
}

//STATS
uint32_t	ContentTransfer::GetNumLeechers()
{
	uint32_t count = 0;
	std::set<Channel *>::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
	    Channel *c = *iter;
	    if (c != NULL)
		    if (!c->IsComplete()) // incomplete?
			    count++;
    }
    return count;
}


uint32_t	ContentTransfer::GetNumSeeders()
{
	uint32_t count = 0;
	std::set<Channel *>::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
	    Channel *c = *iter;
	    if (c != NULL)
		    if (c->IsComplete()) // complete?
			    count++;
    }
    return count;
}

void ContentTransfer::AddPeer(Address &peer)
{
	Channel *c = new Channel(this,INVALID_SOCKET,peer);
}


Channel * ContentTransfer::FindChannel(const Address &addr, Channel *notc)
{
	std::set<Channel *>::iterator iter;
	for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
	{
		Channel *c = *iter;
		if (c != NULL) {
			if (c != notc && (c->peer() == addr || c->recv_peer() == addr)) {
				return c;
			}
		}
	}
	return NULL;
}
