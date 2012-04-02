//LIVE
#include "swift.h"
#include <cfloat>

#include "ext/live_picker.cpp" // FIXME FIXME FIXME FIXME

using namespace swift;

std::vector<ContentTransfer*> ContentTransfer::swarms(20);


LiveTransfer *swift::Create(const char* filename, size_t chunk_size)
{
	const char *swarmidstr = "ArnosFirstSwarm";
	Sha1Hash swarmid(swarmidstr, strlen(swarmidstr));

	fprintf(stderr,"live: swarmid: %s\n",swarmid.hex().c_str() );

	LiveTransfer *lt = new LiveTransfer(filename,swarmid,true,chunk_size);
	return lt;
}


int swift::Write(LiveTransfer *lt, const void *buf, size_t nbyte, long offset)
{
	return lt->AddData(buf,nbyte);
}


int     swift::LiveOpen(const char* filename, const Sha1Hash& hash,Address tracker, bool check_hashes,size_t chunk_size)
{
	LiveTransfer *lt = new LiveTransfer(filename,hash,false,chunk_size);
    if (lt && lt->fd()) {
        // initiate tracker connections
    	// SWIFTPROC
    	Channel *c = NULL;
    	// Arno, 2012-01-09: Old hack: the tracker is assumed to be the live source
        if (tracker != Address())
        	c = new Channel(lt,INVALID_SOCKET,tracker,true);
        else if (Channel::tracker!=Address())
        	c = new Channel(lt,INVALID_SOCKET,Channel::tracker,true);
        return lt->fd();
    }
	return -1;
}



ContentTransfer::ContentTransfer() :  mychannels_(), cb_installed(0), speedzerocount_(0)
{
    cur_speed_[DDIR_UPLOAD] = MovingAverageSpeed();
    cur_speed_[DDIR_DOWNLOAD] = MovingAverageSpeed();
    max_speed_[DDIR_UPLOAD] = DBL_MAX;
    max_speed_[DDIR_DOWNLOAD] = DBL_MAX;

    evtimer_assign(&evclean_,Channel::evbase,&ContentTransfer::LibeventCleanCallback,this);
    evtimer_add(&evclean_,tint2tv(5*TINT_SEC));
}


ContentTransfer::~ContentTransfer()
{
	// Arno, 2012-02-06: Cancel cleanup timer, otherwise chaos!
	evtimer_del(&evclean_);
}


void ContentTransfer::LibeventCleanCallback(int fd, short event, void *arg)
{
	fprintf(stderr,"CleanCallback: **************\n");

	ContentTransfer *ct = (ContentTransfer *)arg;
	if (ct == NULL)
		return;

	// STL and MS and conditional delete from set not a happy place :-(
	std::set<Channel *>	delset;
	std::set<Channel *>::iterator iter;
	for (iter=ct->mychannels_.begin(); iter!=ct->mychannels_.end(); iter++)
	{
		Channel *c = *iter;
		if (c != NULL) {
			if (c->IsScheduled4Close())
				delset.insert(c);
		}
	}
	for (iter=delset.begin(); iter!=delset.end(); iter++)
	{
		Channel *c = *iter;
		c->Close();
		ct->mychannels_.erase(c);
		delete c;
	}

	// Reschedule cleanup
    evtimer_assign(&(ct->evclean_),Channel::evbase,&ContentTransfer::LibeventCleanCallback,ct);
    evtimer_add(&(ct->evclean_),tint2tv(5*TINT_SEC));
}



void ContentTransfer::GlobalAdd() {
	if (swarms.size()<fd()+1)
		swarms.resize(fd()+1);
	swarms[fd()] = this;
}


ContentTransfer* ContentTransfer::Find (const Sha1Hash& swarmid) {
    for(int i=0; i<swarms.size(); i++)
        if (swarms[i] && swarms[i]->root_hash()==swarmid)
            return swarms[i];
    return NULL;
}



bool ContentTransfer::OnPexIn (const Address& addr) {

	//fprintf(stderr,"ContentTransfer::OnPexIn: %s\n", addr.str() );

    for(int i=0; i<hs_in_.size(); i++) {
        Channel* c = Channel::channel(hs_in_[i].toUInt());
        if (c && c->transfer()->fd()==this->fd() && c->peer()==addr) {
            return false; // already connected or connecting, Gertjan fix = return false
        }
    }
    // Gertjan fix: PEX redo
    if (hs_in_.size()<SWIFT_MAX_CONNECTIONS)
        new Channel(this,Channel::default_socket(),addr);
    return true;
}

//Gertjan
int ContentTransfer::RandomChannel (int own_id) {
    binqueue choose_from;
    int i;

    for (i = 0; i < (int) hs_in_.size(); i++) {
        if (hs_in_[i].toUInt() == own_id)
            continue;
        Channel *c = Channel::channel(hs_in_[i].toUInt());
        if (c == NULL || c->transfer()->fd() != this->fd()) {
            /* Channel was closed or is not associated with this ContentTransfer (anymore). */
            hs_in_[i] = hs_in_[0];
            hs_in_.pop_front();
            i--;
            continue;
        }
        if (!c->is_established())
            continue;
        choose_from.push_back(hs_in_[i]);
    }
    if (choose_from.size() == 0)
        return -1;

    return choose_from[((double) rand() / RAND_MAX) * choose_from.size()].toUInt();
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




LiveTransfer::LiveTransfer(const char *filename, const Sha1Hash& swarm_id,bool amsource,size_t chunk_size) :
		ContentTransfer(), swarm_id_(swarm_id), am_source_(amsource), filename_(filename), storagefd_(-1), chunk_size_(chunk_size), last_chunkid_(0), offset_(0)
{
	picker_ = new SimpleLivePiecePicker(this);
	picker_->Randomize(rand()&63);

	storagefd_ = open(filename,CREATEFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (storagefd_ < 0) {
		print_error("live: create: error opening storage");
	}

	GlobalAdd();
}


LiveTransfer::~LiveTransfer()
{
}


uint64_t      LiveTransfer::SeqComplete() {

	bin_t hpos = picker()->getHookinPos();
	bin_t cpos = picker()->getCurrentPos();
    uint64_t seqc = cpos.layer_offset() - hpos.layer_offset();
	return seqc*chunk_size_;
}


uint64_t      LiveTransfer::LiveStart() {

	bin_t hpos = picker()->getHookinPos();
    uint64_t seqc = hpos.layer_offset();
	return seqc*chunk_size_;
}




int LiveTransfer::AddData(const void *buf, size_t nbyte)
{
	//fprintf(stderr,"live: AddData: writing to storage %lu\n", nbyte);

	// Save chunk on disk
	int ret = pwrite(storagefd_,buf,nbyte,offset_);
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
