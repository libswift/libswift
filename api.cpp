/*
 *  api.cpp
 *  Swift top-level API implementation
 *
 *  Created by Victor Grishchenko on 3/6/09.
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */


#include "swift.h"
#include "swarmmanager.h"

using namespace std;
using namespace swift;


#define api_debug	false

/*
 * SwarmID
 */

const SwarmID SwarmID::NOSWARMID = SwarmID();


SwarmID::SwarmID(std::string hexstr)
{
    int val,len=hexstr.length()/2;

    empty_ = false;
    if (len == Sha1Hash::SIZE) // Assumption: pubkey always bigger
    {
	ttype_ = FILE_TRANSFER;
	roothash_ = Sha1Hash(true,hexstr.c_str());
	if (roothash_ == Sha1Hash::ZERO)
	    empty_ = true;
    }
    else
    {
	ttype_ = LIVE_TRANSFER;
	spubkey_ = SwarmPubKey(hexstr);
	if (spubkey_ == SwarmPubKey::NOSPUBKEY)
	{
	    empty_ = true;
	}
    }
}


SwarmID::SwarmID(uint8_t *data,uint16_t datalength)
{
    empty_ = false;
    if (datalength == Sha1Hash::SIZE) // Assumption: pubkey always bigger
    {
	ttype_ = FILE_TRANSFER;
	roothash_ = Sha1Hash(false,(const char*)data);
    }
    else
    {
	ttype_ = LIVE_TRANSFER;
	spubkey_ = SwarmPubKey(data,datalength);
    }
}

SwarmID::~SwarmID()
{
}


SwarmID & SwarmID::operator= (const SwarmID & source)
{
    if (this != &source)
    {
	empty_ = source.empty_;
	ttype_ = source.ttype_;
	roothash_ = source.roothash_;
	spubkey_ = source.spubkey_;
    }
    return *this;
 }


bool    SwarmID::operator == (const SwarmID& b) const
{
    if (empty_ && b.empty_)
	return true;
    else if ((empty_ && !b.empty_) && (!empty_ && b.empty_))
	return false;
    else if (ttype_ == b.ttype_) {
	return roothash_ == b.roothash_ && spubkey_ == b.spubkey_;
    }
    else
	return false;
}



std::string    SwarmID::hex() const
{
    if (empty_)
	return Sha1Hash::ZERO.hex();

    if (ttype_ == FILE_TRANSFER)
	return roothash_.hex();
    else
    {
	return spubkey_.hex();
    }
}


std::string     SwarmID::tofilename() const
{
    if (ttype_ == LIVE_TRANSFER) {
	// Arno, 2013-08-22: Full swarmID in hex too large for filesystem, take 40 byte prefix
	return hex().substr(0,Sha1Hash::SIZE*2);
    }
    else
	return hex();
}


/*
 * Local functions
 */

void StartLibraryCleanup()
{
    if (ContentTransfer::cleancounter == 0)
    {
	// Arno, 2012-10-01: Per-library timer for cleanup on transfers
	evtimer_assign(&ContentTransfer::evclean,Channel::evbase,&ContentTransfer::LibeventGlobalCleanCallback,NULL);
	evtimer_add(&ContentTransfer::evclean,tint2tv(TINT_SEC));
	ContentTransfer::cleancounter = 481;
    }
}


/*
 * Global Operations
 */

int     swift::Listen( Address addr)
{
    if (api_debug)
        fprintf(stderr,"swift::Listen addr %s\n", addr.str().c_str() );

    StartLibraryCleanup();

    sckrwecb_t cb;
    cb.may_read = &Channel::LibeventReceiveCallback;
    cb.sock = Channel::Bind(addr,cb);
    // swift UDP receive
    event_assign(&Channel::evrecv, Channel::evbase, cb.sock, EV_READ,
         cb.may_read, NULL);
    event_add(&Channel::evrecv, NULL);
    return cb.sock;
}


void    swift::Shutdown()
{
    if (api_debug)
	fprintf(stderr,"swift::Shutdown");

    Channel::Shutdown();
}


/*
 * Per-Swarm Operations
 */


int swift::Open( std::string filename, SwarmID& swarmid, std::string trackerurl, bool force_check_diskvshash, popt_cont_int_prot_t cipm, bool zerostate, bool activate, uint32_t chunk_size, std::string metadir)
{
    if (api_debug)
        fprintf(stderr,"swift::Open %s id %s track %s cdisk %d cipm %" PRIu32 " zs %d act %d cs %" PRIu32 "\n", filename.c_str(), swarmid.hex().c_str(), trackerurl.c_str(), force_check_diskvshash, cipm, zerostate, activate, chunk_size );

    if (swarmid.ttype() != FILE_TRANSFER)
	return -1;

    SwarmData* swarm = SwarmManager::GetManager().AddSwarm( filename, swarmid.roothash(), trackerurl, force_check_diskvshash, cipm, zerostate, activate, chunk_size, metadir );
    if (swarm == NULL)
        return -1;
    else
        return swarm->Id();
}


void swift::Close( int td, bool removestate, bool removecontent ) {
    if (api_debug)
	fprintf(stderr,"swift::Close td %d rems %d remc%d\n", td, (int)removestate, (int)removecontent );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm != NULL)
    {
	SwarmManager::GetManager().RemoveSwarm( swarm->RootHash(), removestate, removecontent );
        return;
    }

    //LIVE
    LiveTransfer *lt = LiveTransfer::FindByTD(td);
    if (lt != NULL)
    {
        // Arno, 2013-09-11: If external tracker, sign off
	lt->ConnectToTracker(true);
	delete lt;
    }
}

int swift::Find(SwarmID& swarmid, bool activate)
{
    if (api_debug)
	fprintf(stderr,"swift::Find %s act %d\n", swarmid.hex().c_str(), (int)activate );

    if (swarmid.ttype() == FILE_TRANSFER)
    {
	SwarmData* swarm = SwarmManager::GetManager().FindSwarm(swarmid.roothash());
	if (swarm==NULL)
	    return -1;
	else
	{
	    if (activate)
		SwarmManager::GetManager().ActivateSwarm(swarm->RootHash());
	    return swarm->Id();
	}
    }
    else
    {
	//LIVE
	LiveTransfer *lt = LiveTransfer::FindBySwarmID(swarmid);
	if (lt == NULL)
	    return -1;
	else
	    return lt->td();
    }
}


ContentTransfer *swift::GetActivatedTransfer(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::GetActivatedTransfer td %d\n", td );

    ContentTransfer *ct = NULL;
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
	ct = (ContentTransfer *)LiveTransfer::FindByTD(td);
    else
	ct = swarm->GetTransfer(false); // Arno: do not activate if not already
    return ct;
}



// Local method
static ContentTransfer *FindActivateTransferByTD(int td)
{
    ContentTransfer *ct = NULL;
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
	//LIVE
	ct = (ContentTransfer *)LiveTransfer::FindByTD(td);
    else
    {
	if (!swarm->Touch()) {
	    swarm = SwarmManager::GetManager().ActivateSwarm( swarm->RootHash() );
            if (swarm == NULL)
                return NULL;
	    if (!swarm->Touch())
		return NULL;
	}
	ct = swarm->GetTransfer();
    }
    return ct;
}


ssize_t swift::Read( int td, void *buf, size_t nbyte, int64_t offset )
{
    if (api_debug)
	fprintf(stderr,"swift::Read td %d buf %p n " PRISIZET " o %" PRIi64 "\n", td, buf, nbyte, offset );

    ContentTransfer *ct = FindActivateTransferByTD(td);
    if (ct == NULL)
	return -1;
    else
	return ct->GetStorage()->Read(buf, nbyte, offset);
}

ssize_t swift::Write( int td, const void *buf, size_t nbyte, int64_t offset )
{
    if (api_debug)
	fprintf(stderr,"swift::Write td %d buf %p n " PRISIZET " o %" PRIi64 "\n", td, buf, nbyte, offset );

    ContentTransfer *ct = FindActivateTransferByTD(td);
    if (ct == NULL)
	return -1;
    else
	return ct->GetStorage()->Write(buf, nbyte, offset);
}



void     swift::SetTracker(std::string trackerurl) {
    Channel::trackerurl = trackerurl;
}


/*
 * Swarm Info
 */

uint64_t swift::Size(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::Size td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
	return 0; //also for LIVE
    return swarm->Size();
}



bool swift::IsComplete(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::IsComplete td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
	return false; //also for LIVE
    return swarm->IsComplete();
}


uint64_t swift::Complete(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::Complete td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
	return 0; //also for LIVE
    return swarm->Complete();
}


uint64_t swift::SeqComplete( int td, int64_t offset )
{
    if (api_debug)
	fprintf(stderr,"swift::SeqComplete td %d o %" PRIi64 "\n", td, offset );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return 0;
	else
	    return lt->SeqComplete(); // No range support for live
    }
    else
    {
	return swarm->SeqComplete(offset);
    }
}


SwarmID swift::GetSwarmID(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::SwarmID td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return SwarmID::NOSWARMID;
	else
	    return lt->swarm_id();
    }
    else
	return SwarmID(swarm->RootHash());
}


/** Returns the number of bytes in a chunk for this transmission */
uint32_t swift::ChunkSize( int td)
{
    if (api_debug)
	fprintf(stderr,"swift::ChunkSize td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td);
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return 0;
	else
	    return lt->chunk_size();
    }
    else
	return swarm->ChunkSize();
}



tdlist_t swift::GetTransferDescriptors()
{
    if (api_debug)
	fprintf(stderr,"swift::GetTransferDescriptors\n" );
    tdlist_t filetdl = SwarmManager::GetManager().GetTransferDescriptors();
    tdlist_t livetdl = LiveTransfer::GetTransferDescriptors();
    filetdl.insert(filetdl.end(),livetdl.begin(),livetdl.end()); // append
    return filetdl;
}


void swift::SetMaxSpeed(int td, data_direction_t ddir, double speed)
{
    if (api_debug)
	fprintf(stderr,"swift::SetMaxSpeed td %d dir %d speed %lf\n", td, (int)ddir, speed );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return;
	else
	{
	    // Arno, 2012-05-25: SetMaxSpeed resets the current speed history, so
	    // be careful here.
	    if( lt->GetMaxSpeed( ddir ) != speed )
		lt->SetMaxSpeed( ddir, speed );
	}
    }
    else
	swarm->SetMaxSpeed(ddir,speed); // checks current set speed beforehand
}

double swift::GetCurrentSpeed(int td, data_direction_t ddir)
{
    if (api_debug)
	fprintf(stderr,"swift::GetCurrentSpeed td %d ddir %d\n", td, (int)ddir );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return -1.0;
	else
	    return lt->GetCurrentSpeed(ddir);
    }
    else
    {
	FileTransfer *ft = swarm->GetTransfer(false); // Arno: do not activate for this
	if (!ft)
	    return -1.0;
	else
	    return ft->GetCurrentSpeed(ddir);
    }
}


uint32_t swift::GetNumSeeders(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::GetNumSeeders td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return 0;
	else
	    return lt->GetNumSeeders();
    }
    else
    {
	FileTransfer *ft = swarm->GetTransfer(false); // Arno: do not activate for this
	if (!ft)
	    return 0;
	else
	    return ft->GetNumSeeders();
    }
}


uint32_t swift::GetNumLeechers(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::GetNumLeechers td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return 0;
	else
	    return lt->GetNumLeechers();
    }
    else
    {
	FileTransfer *ft = swarm->GetTransfer(false); // Arno: do not activate for this
	if (!ft)
	    return 0;
	else
	    return ft->GetNumLeechers();
    }
}



transfer_t swift::ttype(int td)
{
    //if (api_debug)
    //	fprintf(stderr,"swift::ttype td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
	return LIVE_TRANSFER; // approx of truth
    else
	return FILE_TRANSFER;
}

Storage *swift::GetStorage(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::GetStorage td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return NULL;
	else
	    return lt->GetStorage();
    }
    else
    {
	FileTransfer *ft = swarm->GetTransfer(); // Must activate for this
	if (!ft)
	    return NULL;
	else
	    return ft->GetStorage();
    }
}

std::string swift::GetOSPathName(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::GetOSPathName td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL || lt->GetStorage() == NULL)
	    return "";
	else
	    return lt->GetStorage()->GetOSPathName();
    }
    else
	return swarm->OSPathName();
}

bool swift::IsOperational(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::IsOperational td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return false;
	else
	    return lt->IsOperational();
    }
    else
    {
	FileTransfer *ft = swarm->GetTransfer(false);   // Arno: do not activate for this
	if (!ft)
	    return false;
	else
	    return ft->IsOperational();
    }
}



bool swift::IsZeroState(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::IsZeroState td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
	return false;
    else
	return swarm->IsZeroState();
}



//CHECKPOINT
int swift::Checkpoint(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::Checkpoint td %d\n", td );

    // If file, save transfer's binmap for zero-hashcheck restart
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
	return -1; // also for LIVE
    FileTransfer *ft = swarm->GetTransfer(false);
    if (ft == NULL)
        return -1; // not activated
    if (ft->IsZeroState())
        return -1;

    MmapHashTree *ht = (MmapHashTree *)ft->hashtree();
    if (ht == NULL)
    {
        fprintf(stderr,"swift: checkpointing: ht is NULL\n");
        return -1;
    }

    std::string binmap_filename = ft->GetStorage()->GetOSPathName();
    binmap_filename.append(".mbinmap");
    //fprintf(stderr,"swift: HACK checkpointing %s at %" PRIi64 "\n", binmap_filename.c_str(), Complete(td));
    FILE *fp = fopen_utf8(binmap_filename.c_str(),"wb");
    if (!fp) {
        print_error("cannot open mbinmap for writing");
        return -1;
    }
    int ret = ht->serialize(fp);
    if (ret < 0)
        print_error("writing to mbinmap");
    fclose(fp);
    return ret;
}



// SEEK
int swift::Seek(int td, int64_t offset, int whence)
{
    if (api_debug)
	fprintf(stderr,"swift::Seek td %d o %" PRIi64 " w %d\n", td, offset, whence );

    dprintf("%s F%d Seek: to %" PRIi64 "\n",tintstr(), td, offset );
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
	return -1; // also for LIVE

    // Quick fail in order not to activate a swarm only to fail after activation
    if( whence != SEEK_SET ) // TODO other
	return -1;
    if( offset >= swift::Size(td) )
	return -1;

    if( !swarm->Touch() ) {
	swarm = SwarmManager::GetManager().ActivateSwarm( swarm->RootHash() );
        if (swarm == NULL)
           return -1;
	if (!swarm->Touch())
	    return -1;
    }
    FileTransfer *ft = swarm->GetTransfer();

    // whence == SEEK_SET && offset < swift::Size(td)  - validated by quick fail above

    // Which bin to seek to?
    int64_t coff = offset - (offset % ft->hashtree()->chunk_size()); // ceil to chunk
    bin_t offbin = bin_t(0,coff/ft->hashtree()->chunk_size());

    dprintf("%s F%i Seek: to bin %s\n",tintstr(), td, offbin.str().c_str() );

    return ft->picker()->Seek(offbin,whence);
}



void swift::AddPeer(Address& addr, SwarmID& swarmid)
{
    if (api_debug)
	fprintf(stderr,"swift::AddPeer addr %s hash %s\n", addr.str().c_str(), swarmid.hex().c_str() );

    ContentTransfer *ct = NULL;
    if (swarmid.ttype() == FILE_TRANSFER)
    {
	SwarmData* swarm = SwarmManager::GetManager().FindSwarm(swarmid.roothash());
	if (swarm == NULL)
	    return;
	else
	{
	    if (!swarm->Touch()) {
		swarm = SwarmManager::GetManager().ActivateSwarm(swarmid.roothash());
		if (swarm == NULL)
		    return;
		if (!swarm->Touch())
		    return;
	    }
	    ct = (ContentTransfer *)swarm->GetTransfer();
	}
    }
    else
	ct = (ContentTransfer *)LiveTransfer::FindBySwarmID(swarmid);

    if (ct == NULL)
	return;
    else
	ct->AddPeer(addr);
    // FIXME: When cached addresses are supported in swapped-out swarms, add the peer to that cache instead
}



/*
 * Progress Monitoring
 */


void swift::AddProgressCallback(int td,ProgressCallback cb,uint8_t agg)
{
    if (api_debug)
	fprintf(stderr,"swift::AddProgressCallback: td %d\n", td);

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return;
	else
	    lt->AddProgressCallback(cb,agg);
	return;
    }
    else
	swarm->AddProgressCallback( cb, agg );

    //fprintf(stderr,"swift::AddProgressCallback: swarm obj %p %p\n", swarm, cb );
}



void swift::RemoveProgressCallback(int td, ProgressCallback cb)
{
    if (api_debug)
	fprintf(stderr,"swift::RemoveProgressCallback: td %d\n", td);

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return;
	else
	    lt->RemoveProgressCallback(cb);
	return;
    }
    else
	swarm->RemoveProgressCallback(cb);
}


/*
 * Offline hash checking. Writes .mhash and .mbinmap file for the specified
 * content filename.
 *
 * MUST NOT use any swift global variables!
 */

int swift::HashCheckOffline( std::string filename, Sha1Hash *calchashptr, uint32_t chunk_size)
{
    if (api_debug)
	fprintf(stderr,"swift::HashCheckOffline %s hashptr %p cs %" PRIu32 "\n", filename.c_str(), calchashptr, chunk_size );

    // From transfer.cpp::FileTransfer constructor
    std::string destdir = dirname_utf8(filename);
    if (destdir == "")
	destdir = ".";

    // MULTIFILE
    Storage *storage_ = new Storage(filename,destdir,-1,0);

    std::string hash_filename;
    hash_filename.assign(filename);
    hash_filename.append(".mhash");

    std::string binmap_filename;
    binmap_filename.assign(filename);
    binmap_filename.append(".mbinmap");

    MmapHashTree *hashtree_ = new MmapHashTree(storage_,Sha1Hash::ZERO,chunk_size,hash_filename,true,binmap_filename);

    FILE *fp = fopen_utf8(binmap_filename.c_str(),"wb");
    if (!fp) {
        print_error("cannot open mbinmap for writing");
        return -1;
    }
    int ret = hashtree_->serialize(fp);
    if (ret < 0)
        print_error("writing to mbinmap");
    fclose(fp);

    *calchashptr = hashtree_->root_hash();

    return ret;
}



/*
 * LIVE
 */

LiveTransfer *swift::LiveCreate(std::string filename, KeyPair &keypair, std::string checkpoint_filename, popt_cont_int_prot_t cipm, uint64_t disc_wnd, uint32_t nchunks_per_sign, uint32_t chunk_size)
{
    if (api_debug)
	fprintf(stderr,"swift::LiveCreate %s keypair checkp %s cipm %" PRIu32 " ldw %" PRIu64 " nsign %" PRIu32 " cs %" PRIu32 "\n", filename.c_str(), checkpoint_filename.c_str(), cipm, disc_wnd, nchunks_per_sign, chunk_size );

    // Arno: LIVE streams are not managed by SwarmManager
    LiveTransfer *lt = new LiveTransfer(filename,keypair,checkpoint_filename,cipm,disc_wnd,nchunks_per_sign,chunk_size);
    fprintf(stderr,"swift::LiveCreate: swarmid: %s\n",lt->swarm_id().hex().c_str() );

    if (lt->IsOperational())
    {
	// External tracker
	fprintf(stderr,"swift::LiveCreate: ConnectToTracker\n");
	lt->ConnectToTracker();
	return lt;
    }
    else
    {
	fprintf(stderr,"swift::LiveCreate: %s swarm created, but not operational\n", lt->swarm_id().hex().c_str() );
	delete lt;
	return NULL;
    }
}


int swift::LiveWrite(LiveTransfer *lt, const void *buf, size_t nbyte)
{
    //if (api_debug)
    //	fprintf(stderr,"swift::LiveWrite lt %p buf %p n " PRISIZET "\n", lt, buf. nbyte );

    return lt->AddData(buf,nbyte);
}


int swift::LiveOpen(std::string filename, SwarmID &swarmid, std::string trackerurl, Address &srcaddr, popt_cont_int_prot_t cipm, uint64_t disc_wnd, uint32_t chunk_size)
{
    if (api_debug)
	fprintf(stderr,"swift::LiveOpen %s hash %s track %s src %s cipm %" PRIu32 " ldw %" PRIu64 " cs %" PRIu32 "\n", filename.c_str(), swarmid.hex().c_str(), trackerurl.c_str(), srcaddr.str().c_str(), cipm, disc_wnd, chunk_size );

    // Help user
    if (cipm == POPT_CONT_INT_PROT_MERKLE)
	cipm = POPT_CONT_INT_PROT_UNIFIED_MERKLE;

    LiveTransfer *lt = new LiveTransfer(filename,swarmid,srcaddr,cipm,disc_wnd,chunk_size);

    // initiate tracker connections
    // SWIFTPROC
    lt->SetTracker(trackerurl);
    fprintf(stderr,"swift::LiveOpen: ConnectToTracker\n");
    lt->ConnectToTracker();
    return lt->td();
}


uint64_t  swift::GetHookinOffset(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::GetHookinOffset td %d\n", td );

    LiveTransfer *lt = LiveTransfer::FindByTD(td);
    if (lt == NULL)
	return 0; // also for FileTransfer
    else
	return lt->GetHookinOffset();
}


// Called from sendrecv.cpp
void swift::Touch(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::Touch: td %d\n", td);

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(td);
    if (swarm != NULL)
	swarm->Touch();
}
