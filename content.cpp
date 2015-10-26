/*
 *  content.cpp
 *  Superclass of FileTransfer and LiveTransfer
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 */
#include "swift.h"
#include <cfloat>
#include "swarmmanager.h"
#include <event2/http.h>


using namespace swift;


/*
 * Class variables
 */
struct event ContentTransfer::evclean;
uint64_t ContentTransfer::cleancounter = 0;


/*
 * Local Constants
 */
#define CHANNEL_GARBAGECOLLECT_INTERVAL       5 // seconds, or GlobalCleanCallback calls actually
#define TRANSFER_IDLE_CHECK_DEACTIVATE_INTERVAL  30 // seconds, or GlobalCleanCallback calls actually

#define TRACKER_RETRY_INTERVAL_START    (5*TINT_SEC)
#define TRACKER_RETRY_INTERVAL_EXP  1.1 // exponent used to increase INTERVAL_START
#define TRACKER_RETRY_INTERVAL_MAX  (1800*TINT_SEC) // 30 minutes

ContentTransfer::ContentTransfer(transfer_t ttype) :  ttype_(ttype),
    swarm_id_(), mychannels_(), callbacks_(), picker_(NULL), hashtree_(NULL),
    speedupcount_(0), speeddwcount_(0), trackerurl_(),
    tracker_retry_interval_(TRACKER_RETRY_INTERVAL_START),
    tracker_retry_time_(NOW),
    ext_tracker_client_(NULL),
    slow_start_hints_(0)
{
    cur_speed_[DDIR_UPLOAD] = MovingAverageSpeed();
    cur_speed_[DDIR_DOWNLOAD] = MovingAverageSpeed();
    max_speed_[DDIR_UPLOAD] = DBL_MAX;
    max_speed_[DDIR_DOWNLOAD] = DBL_MAX;
}


ContentTransfer::~ContentTransfer()
{
    dprintf("%s F%d content deconstructor\n",tintstr(),td_);
    CloseChannels(mychannels_,true);
    if (storage_ != NULL) {
        delete storage_;
        storage_ = NULL;
    }

    if (ext_tracker_client_ != NULL) {
        delete ext_tracker_client_;
        ext_tracker_client_ = NULL;
    }
}


void ContentTransfer::CloseChannels(channels_t delset,bool isall)
{
    channels_t::iterator iter;
    for (iter=delset.begin(); iter!=delset.end(); iter++) {
        Channel *c = *iter;
        dprintf("%s F%d content close chans\n",tintstr(),td_);
        c->Close(CLOSE_SEND_IF_ESTABLISHED);
        if (isall)
            c->ClearTransfer();
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
    uint32_t numestablishedpeers=0;
    bool movingforward=false;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++) {
        Channel *c = *iter;
        if (c != NULL) {
            if (c->IsScheduled4Delete())
                delset.push_back(c);

            if (c->is_established()) {
                numestablishedpeers++;

                // Moving forward is when any channel is moving forward
                if (c->IsMovingForward())
                    movingforward = true;
            }
        }
    }
    //dprintf("%s F%d content gc chans\n",tintstr(),td_);
    CloseChannels(delset,false);

    /*
     * If we have no peers left, see if we can get some more (if not seed)
     */
    if (numestablishedpeers == 0)
        movingforward = false;

    // Arno, 2013-09-11: Don't pull extra for peers when seeder and external tracker
    bool complete = (swift::Complete(td_) > 0 && (swift::Complete(td_) == swift::Size(td_)));
    if (complete && ext_tracker_client_ != NULL)  // seed, periodic rereg helps find leechers
        movingforward = true;

    // Arno, 2012-02-24: Check for liveliness.
    ReConnectToTrackerIfAllowed(movingforward);
}





// Global method
void ContentTransfer::LibeventGlobalCleanCallback(int fd, short event, void *arg)
{
    //fprintf(stderr,"ContentTransfer::GlobalCleanCallback %" PRIu64 "\n", ContentTransfer::cleancounter );

    // Arno, 2012-02-24: Why-oh-why, update NOW
    Channel::Time();

    if ((ContentTransfer::cleancounter % TRANSFER_IDLE_CHECK_DEACTIVATE_INTERVAL) == 0) {
        // Deactivate FileTransfers that have been idle too long. Including zerostate
        SwarmManager::GetManager().DeactivateIdleSwarms();
    }

    tdlist_t tds = GetTransferDescriptors();
    tdlist_t::iterator iter;
    for (iter = tds.begin(); iter != tds.end(); iter++) {
        int td = *iter;

        ContentTransfer *ct = swift::GetActivatedTransfer(td);
        if (ct == NULL)
            continue; // not activated, don't bother

        // Update speed measurements such that they decrease when DL/UL stops
        // Always. Must be done on 1 s interval
        ct->OnRecvNoData();
        ct->OnSendNoData();


        // Arno: Call garage collect only once every CHANNEL_GARBAGECOLLECT_INTERVAL
        if ((ContentTransfer::cleancounter % CHANNEL_GARBAGECOLLECT_INTERVAL) == 0)
            ct->GarbageCollectChannels();

        // Some external trackers need periodic reports
        if (ct->ext_tracker_client_ != NULL) {
            tint report_time = ct->ext_tracker_client_->GetReportLastTime() +
                               (TINT_SEC*ct->ext_tracker_client_->GetReportInterval());
            if (NOW > report_time) {
                fprintf(stderr,"content: periodic ConnectToTracker\n");
                ct->ConnectToTracker();
            }
        }
    }

    ContentTransfer::cleancounter++;


    // Arno, 2012-10-01: Reschedule cleanup, started in swift::Open
    evtimer_add(&ContentTransfer::evclean,tint2tv(TINT_SEC));
}



void ContentTransfer::ReConnectToTrackerIfAllowed(bool movingforward)
{
    // fprintf(stderr,"%s F%d content reconnect to tracker: movingfwd %s\n",tintstr(),td_,(movingforward ? "true":"false") );

    // If I'm not connected to any
    // peers, try to contact the tracker again.
    if (!movingforward) {
        if (NOW > tracker_retry_time_) {
            //fprintf(stderr,"content: -movingforward ConnectToTracker\n");
            ConnectToTracker();

            // Should be: if fail then exp backoff
            tracker_retry_interval_ *= TRACKER_RETRY_INTERVAL_EXP;
            if (tracker_retry_interval_ > TRACKER_RETRY_INTERVAL_MAX)
                tracker_retry_interval_ = TRACKER_RETRY_INTERVAL_MAX;
            tracker_retry_time_ = NOW + tracker_retry_interval_;
        }
    } else {
        tracker_retry_interval_ = TRACKER_RETRY_INTERVAL_START;
        tracker_retry_time_ = NOW + tracker_retry_interval_;
    }
}


/** Called by ExternalTrackerClient when results come in from the server */
static void global_bttracker_callback(int td, std::string status, uint32_t interval, peeraddrs_t peerlist)
{
    //fprintf(stderr,"content global_bttracker_callback: td %d status %s int %" PRIu32 " npeers %" PRIu32 "\n", td, status.c_str(), interval, peerlist.size() );

    ContentTransfer *ct = swift::GetActivatedTransfer(td);
    if (ct == NULL)
        return; // not activated, don't bother

    if (status == "") {
        // Success
        dprintf("%s F%d content contact tracker: ext OK int %" PRIu32 " npeers " PRISIZET "\n",tintstr(),td,interval,
                peerlist.size());

        // Record reporting interval
        ExternalTrackerClient *bttrackclient = ct->GetExternalTrackerClient();

        if (bttrackclient != NULL) // unlikely
            bttrackclient->SetReportInterval(interval);

        // Attempt to create channels to peers
        peeraddrs_t::iterator iter;
        for (iter=peerlist.begin(); iter!=peerlist.end(); iter++) {
            Address addr = *iter;
            ct->OnPexIn(addr);
        }
    } else {
        dprintf("%s F%d content contact tracker: ext failure reason %s\n",tintstr(),td,status.c_str());
    }
}


void ContentTransfer::ConnectToTracker(bool stop)
{
    // dprintf("%s F%d content contact tracker\n",tintstr(),td_);

    if (!IsOperational())
        return;

    struct evhttp_uri *evu = NULL;

    if (trackerurl_ != "")
        evu = evhttp_uri_parse(trackerurl_.c_str());
    else {
        if (Channel::trackerurl == "") {
            // Testing
            dprintf("%s F%d content contact tracker: No tracker defined\n",tintstr(),td_);
            return;
        }
        evu = evhttp_uri_parse(Channel::trackerurl.c_str());
    }

    if (evu == NULL) {
        dprintf("%s F%d content contact tracker: failure parsing URL\n",tintstr(),td_);
        return;
    }

    char buf[1024+1];
    char *uricstr = evhttp_uri_join(evu,buf,1024);
    dprintf("%s F%d content contact tracker: Tracker is %s scheme %s\n",tintstr(),td_,uricstr, evhttp_uri_get_scheme(evu));

    std::string scheme = evhttp_uri_get_scheme(evu);
    if (scheme == SWIFT_URI_SCHEME) {
        // swift tracker
        const char *host = evhttp_uri_get_host(evu);
        if (host == NULL) {
            dprintf("%s F%d content contact tracker: failure parsing URL host\n",tintstr(),td_);
            return;
        }
        int port = evhttp_uri_get_port(evu);

        Address trackaddr(host,port);
        Channel *c = new Channel(this,INVALID_SOCKET,trackaddr);
    } else {
        // External tracker
        std::string event = EXTTRACK_EVENT_WORKING;
        if (ext_tracker_client_ == NULL) {
            // First call, create client
            event = EXTTRACK_EVENT_STARTED;
            if (trackerurl_ != "")
                ext_tracker_client_ = new ExternalTrackerClient(trackerurl_);
            else
                ext_tracker_client_ = new ExternalTrackerClient(Channel::trackerurl);
        } else if (stop) {
            event = EXTTRACK_EVENT_STOPPED;
        } else if (!ext_tracker_client_->GetReportedComplete()) {
            // Vulnerable to Automatic Size detection not being finished
            if (swift::Complete(td()) > 0 && swift::Complete(td()) == swift::Size(td()))
                event = EXTTRACK_EVENT_COMPLETED;
        }

        ext_tracker_client_->Contact(this,event,global_bttracker_callback);
    }

    evhttp_uri_free(evu);
}


bool ContentTransfer::OnPexIn(const Address& addr)
{
    //fprintf(stderr,"ContentTransfer::OnPexIn: %s\n", addr.str().c_str() );
    // Arno: this brings safety, but prevents private swift installations.
    // TODO: detect public internet.
    //if (addr.is_private())
    //   return false;

    Address myaddr = Channel::BoundAddress(Channel::default_socket());
    if (addr == myaddr)
        return false; // Connect to self

    channels_t::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++) {
        Channel *c = *iter;
        if (c != NULL && c->peer()==addr)
            return false; // already connected or connecting, Gertjan fix = return false
    }
    // Gertjan fix: PEX redo
    if (mychannels_.size()<SWIFT_MAX_OUTGOING_CONNECTIONS)
        new Channel(this,Channel::default_socket(),addr);
    return true;
}

//Gertjan
Channel *ContentTransfer::RandomChannel(Channel *notc)
{
    channels_t choose_from;
    channels_t::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++) {
        Channel *c = *iter;
        if (c != NULL && c != notc && c->is_established())
            choose_from.push_back(c);
    }
    if (choose_from.size() == 0)
        return NULL;

    return choose_from[rand() % choose_from.size()];
}





// RATELIMIT
void ContentTransfer::OnRecvData(int n)
{
    speeddwcount_++;
    uint32_t speed = cur_speed_[DDIR_DOWNLOAD].GetSpeedNeutral();
    uint32_t rate = speed & ~1048575 ? 32:8;
    if (speeddwcount_>=rate) {
        cur_speed_[DDIR_DOWNLOAD].AddPoint((uint64_t)n*rate);
        speeddwcount_=0;
    }
}

void ContentTransfer::OnSendData(int n)
{
    speedupcount_++;
    uint32_t speed = cur_speed_[DDIR_UPLOAD].GetSpeedNeutral();
    uint32_t rate = speed & ~1048575 ? 32:8;
    if (speedupcount_>=rate) {
        cur_speed_[DDIR_UPLOAD].AddPoint((uint64_t)n*rate);
        speedupcount_ = 0;
    }
}


void ContentTransfer::OnRecvNoData()
{
    // AddPoint(0) everytime we don't AddData gives bad speed measurement
    cur_speed_[DDIR_DOWNLOAD].AddPoint((uint64_t)0);
}

void ContentTransfer::OnSendNoData()
{
    // AddPoint(0) everytime we don't SendData gives bad speed measurement
    cur_speed_[DDIR_UPLOAD].AddPoint((uint64_t)0);
}



double ContentTransfer::GetCurrentSpeed(data_direction_t ddir)
{
    return cur_speed_[ddir].GetSpeedNeutral();
}


void ContentTransfer::SetMaxSpeed(data_direction_t ddir, double m)
{
    max_speed_[ddir] = m;
    // Arno, 2012-01-04: Be optimistic, forget history.
    cur_speed_[ddir].Reset();
}


double ContentTransfer::GetMaxSpeed(data_direction_t ddir)
{
    return max_speed_[ddir];
}

//STATS
uint32_t ContentTransfer::GetNumLeechers()
{
    uint32_t count = 0;
    channels_t::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++) {
        Channel *c = *iter;
        if (c != NULL)
            if (!c->IsComplete()) // incomplete?
                count++;
    }
    return count;
}


uint32_t ContentTransfer::GetNumSeeders()
{
    uint32_t count = 0;
    channels_t::iterator iter;
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++) {
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
    for (iter=mychannels_.begin(); iter!=mychannels_.end(); iter++) {
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


void ContentTransfer::AddProgressCallback(ProgressCallback cb, uint8_t agg)
{
    callbacks_.push_back(progcallbackreg_t(cb, agg));
}

void ContentTransfer::RemoveProgressCallback(ProgressCallback cb)
{
    progcallbackregs_t::iterator iter;
    for (iter= callbacks_.begin(); iter != callbacks_.end(); iter++) {
        if ((*iter).first == cb) {
            callbacks_.erase(iter);
            return;
        }
    }
}

void ContentTransfer::Progress(bin_t bin)
{
    int minlayer = bin.layer();
    // Arno, 2012-10-02: Callback may call RemoveCallback and thus mess up iterator, so use copy
    progcallbackregs_t copycbs(callbacks_);
    progcallbackregs_t::iterator iter;
    for (iter=copycbs.begin(); iter != copycbs.end(); iter++) {
        if (minlayer >= (*iter).second)
            ((*iter).first)(td_, bin);
    }
}

void ContentTransfer::SetTD(int td)
{
    td_ = td;
    if (storage_ != NULL)
        storage_->SetTD(td);
}
