/*
 *  content.cpp
 *  Superclass of FileTransfer and LiveTransfer
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 */
#include "swift.h"
#include <cfloat>
#include "swarmmanager.h"


using namespace swift;


/*
 * Class variables
 */
struct event ContentTransfer::evclean;
uint64_t ContentTransfer::cleancounter = 0;


/*
 * Local Constants
 */
#define CHANNEL_GARBAGECOLLECT_INTERVAL	5 // seconds, or GlobalCleanCallback calls actually
#define TRANSFER_IDLE_DEACTIVATE_INTERVAL  30 // seconds, or GlobalCleanCallback calls actually

#define TRACKER_RETRY_INTERVAL_START	(5*TINT_SEC)
#define TRACKER_RETRY_INTERVAL_EXP	1.1	// exponent used to increase INTERVAL_START
#define TRACKER_RETRY_INTERVAL_MAX	(1800*TINT_SEC) // 30 minutes



ContentTransfer::ContentTransfer(transfer_t ttype) :  ttype_(ttype), mychannels_(), callbacks_(), picker_(NULL),
    speedzerocount_(0), tracker_(),
    tracker_retry_interval_(TRACKER_RETRY_INTERVAL_START),
    tracker_retry_time_(NOW)
{


    cur_speed_[DDIR_UPLOAD] = MovingAverageSpeed();
    cur_speed_[DDIR_DOWNLOAD] = MovingAverageSpeed();
    max_speed_[DDIR_UPLOAD] = DBL_MAX;
    max_speed_[DDIR_DOWNLOAD] = DBL_MAX;
}


ContentTransfer::~ContentTransfer()
{
    CloseChannels(mychannels_);
    if (storage_ != NULL)
        delete storage_;
}


void ContentTransfer::CloseChannels(channels_t delset)
{
    channels_t::iterator iter;
    for (iter=delset.begin(); iter!=delset.end(); iter++)
    {
        Channel *c = *iter;
        c->Close();
        delete c;
        // ~Channel removes it from Channel::channels and mychannels_.erase(c);
    }
}

// SAFECLOSE
void ContentTransfer::GarbageCollectChannels()
{
    // STL and MS and conditional delete from set not a happy place :-(
    channels_t   delset;
    channels_t::iterator iter;
    bool hasestablishedpeers=false;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
        Channel *c = *iter;
        if (c != NULL) {
            if (c->IsScheduled4Delete())
                delset.push_back(c);

            if (c->is_established ())
                hasestablishedpeers = true;
        }
    }
    CloseChannels(delset);

    // Arno, 2012-02-24: Check for liveliness.
    ReConnectToTrackerIfAllowed(hasestablishedpeers);
}

// Global method
void ContentTransfer::LibeventGlobalCleanCallback(int fd, short event, void *arg)
{
    //fprintf(stderr,"ContentTransfer::GlobalCleanCallback\n");

    // Arno, 2012-02-24: Why-oh-why, update NOW
    Channel::Time();

    if ((ContentTransfer::cleancounter % TRANSFER_IDLE_DEACTIVATE_INTERVAL) == 0) {
	// Deactivate FileTransfers that have been idle too long. Including zerostate
	SwarmManager::GetManager().DeactivateIdleSwarms();
    }

    tdlist_t tds = GetTransferDescriptors();
    tdlist_t::iterator iter;
    for (iter = tds.begin(); iter != tds.end(); iter++)
    {
	int td = *iter;

	ContentTransfer *ct = swift::GetActivatedTransfer(td);
	if (ct == NULL)
	    continue; // not activated, don't bother

	// Update speed measurements such that they decrease when DL/UL stops
	// Always. Must be done on 1 s interval
	ct->OnRecvData(0);
	ct->OnSendData(0);


	// Arno: Call garage collect only once every CHANNEL_GARBAGECOLLECT_INTERVAL
	if ((ContentTransfer::cleancounter % CHANNEL_GARBAGECOLLECT_INTERVAL) == 0)
	    ct->GarbageCollectChannels();
    }

    ContentTransfer::cleancounter++;


    // Arno, 2012-10-01: Reschedule cleanup, started in swift::Open
    evtimer_add(&ContentTransfer::evclean,tint2tv(TINT_SEC));
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
    if (!IsOperational())
 	return;

    Channel *c = NULL;
    if (tracker_ != Address())
        c = new Channel(this,INVALID_SOCKET,tracker_,true);
    else if (Channel::tracker!=Address())
        c = new Channel(this,INVALID_SOCKET,Channel::tracker,true);
}


bool ContentTransfer::OnPexIn (const Address& addr)
{
    //fprintf(stderr,"ContentTransfer::OnPexIn: %s\n", addr.str() );
    // Arno: this brings safety, but prevents private swift installations.
    // TODO: detect public internet.
    //if (addr.is_private())
    //   return false;

    channels_t::iterator iter;
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
    channels_t choose_from;
    channels_t::iterator iter;
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
void      ContentTransfer::OnRecvData(int n)
{
    // Got n ~ 32K
    cur_speed_[DDIR_DOWNLOAD].AddPoint((uint64_t)n);
}

void      ContentTransfer::OnSendData(int n)
{
    // Sent n ~ 1K
    cur_speed_[DDIR_UPLOAD].AddPoint((uint64_t)n);
}


void      ContentTransfer::OnSendNoData()
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


double      ContentTransfer::GetCurrentSpeed(data_direction_t ddir)
{
    return cur_speed_[ddir].GetSpeedNeutral();
}


void      ContentTransfer::SetMaxSpeed(data_direction_t ddir, double m)
{
    max_speed_[ddir] = m;
    // Arno, 2012-01-04: Be optimistic, forget history.
    cur_speed_[ddir].Reset();
}


double      ContentTransfer::GetMaxSpeed(data_direction_t ddir)
{
    return max_speed_[ddir];
}

//STATS
uint32_t   ContentTransfer::GetNumLeechers()
{
    uint32_t count = 0;
    channels_t::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++)
    {
       Channel *c = *iter;
       if (c != NULL)
          if (!c->IsComplete()) // incomplete?
             count++;
    }
    return count;
}


uint32_t   ContentTransfer::GetNumSeeders()
{
    uint32_t count = 0;
    channels_t::iterator iter;
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
    channels_t::iterator iter;
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



/*
 * Progress Monitoring
 */


void ContentTransfer::AddProgressCallback(ProgressCallback cb, uint8_t agg) {
    callbacks_.push_back( progcallbackreg_t( cb, agg ) );
}

void ContentTransfer::RemoveProgressCallback(ProgressCallback cb) {
    progcallbackregs_t::iterator iter;
    for (iter= callbacks_.begin(); iter != callbacks_.end(); iter++ ) {
        if( (*iter).first == cb ) {
	    callbacks_.erase( iter );
	    return;
	}
    }
}

void ContentTransfer::Progress(bin_t bin) {
    int minlayer = bin.layer();
    // Arno, 2012-10-02: Callback may call RemoveCallback and thus mess up iterator, so use copy    
    progcallbackregs_t copycbs(callbacks_);
    progcallbackregs_t::iterator iter;
    for (iter=copycbs.begin(); iter != copycbs.end(); iter++ ) {
	if( minlayer >= (*iter).second )
	    ((*iter).first)( td_, bin );
    }
}
