/*
 *  api.cpp
 *  Swift top-level API implementation
 *
 *  Created by Victor Grishchenko on 3/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */


#include "swift.h"
#include "swarmmanager.h"

using namespace std;
using namespace swift;


static bool api_debug=false;

/*
 * Global Operations
 */

int     swift::Listen( Address addr)
{
    if (api_debug)
        fprintf(stderr,"swift::Listen addr %s\n", addr.str() );

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


int swift::Open( std::string filename, const Sha1Hash& hash, Address tracker, bool force_check_diskvshash, bool check_netwvshash, bool zerostate, bool activate, uint32_t chunk_size)
{
    if (api_debug)
	fprintf(stderr,"swift::Open %s hash %s track %s cdisk %d cnet %d zs %d act %d cs %u\n", filename.c_str(), hash.hex().c_str(), tracker.str(), force_check_diskvshash, check_netwvshash, zerostate, activate, chunk_size );

    if (ContentTransfer::cleancounter == 0)
    {
	// Arno, 2012-10-01: Per-library timer for cleanup on transfers
	evtimer_assign(&ContentTransfer::evclean,Channel::evbase,&ContentTransfer::LibeventGlobalCleanCallback,NULL);
	evtimer_add(&ContentTransfer::evclean,tint2tv(TINT_SEC));
	ContentTransfer::cleancounter = 481;
    }

    SwarmData* swarm = SwarmManager::GetManager().AddSwarm( filename, hash, tracker, force_check_diskvshash, check_netwvshash, zerostate, activate, chunk_size );
    if (swarm)
	return swarm->Id();
    return -1;
}


void swift::Close( int td, bool removestate, bool removecontent ) {
    if (api_debug)
	fprintf(stderr,"swift::Close td %d rems %d remc%d\n", td, (int)removestate, (int)removecontent );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm)
	SwarmManager::GetManager().RemoveSwarm( swarm->RootHash(), removestate, removecontent );

    //LIVE
    LiveTransfer *lt = LiveTransfer::FindByTD(td);
    if (lt != NULL)
	delete lt;
}

int swift::Find(Sha1Hash& swarmid, bool activate)
{
    if (api_debug)
	fprintf(stderr,"swift::Find %s act %d\n", swarmid.hex().c_str(), (int)activate );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(swarmid);
    if (swarm==NULL)
    {
	//LIVE
	LiveTransfer *lt = LiveTransfer::FindBySwarmID(swarmid);
	if (lt == NULL)
	    return -1;
	else
	    return lt->td();
    }
    else
    {
	if (activate)
	    SwarmManager::GetManager().ActivateSwarm(swarm->RootHash());
	return swarm->Id();
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
	fprintf(stderr,"swift::Read td %d buf %p n " PRISIZET " o %lld\n", td, buf, nbyte, offset );

    ContentTransfer *ct = FindActivateTransferByTD(td);
    if (ct == NULL)
	return -1;
    else
	return ct->GetStorage()->Read(buf, nbyte, offset);
}

ssize_t swift::Write( int td, const void *buf, size_t nbyte, int64_t offset )
{
    if (api_debug)
	fprintf(stderr,"swift::Write td %d buf %p n " PRISIZET " o %lld\n", td, buf, nbyte, offset );

    ContentTransfer *ct = FindActivateTransferByTD(td);
    if (ct == NULL)
	return -1;
    else
	return ct->GetStorage()->Write(buf, nbyte, offset);
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
	fprintf(stderr,"swift::SeqComplete td %d o %lld\n", td, offset );

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


const Sha1Hash& swift::SwarmID(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::SwarmID td %d\n", td );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( td );
    if (swarm == NULL)
    {
	LiveTransfer *lt = LiveTransfer::FindByTD(td);
	if (lt == NULL)
	    return Sha1Hash::ZERO;
	else
	    return lt->swarm_id();
    }
    else
	return swarm->RootHash();
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
	    return NULL;
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
    //fprintf(stderr,"swift: HACK checkpointing %s at %lli\n", binmap_filename.c_str(), Complete(td));
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
	fprintf(stderr,"swift::Seek td %d o %lld w %d\n", td, offset, whence );

    dprintf("%s F%d Seek: to %lld\n",tintstr(), td, offset );
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
	if (!swarm->Touch())
	    return -1;
    }
    FileTransfer *ft = swarm->GetTransfer();

    // whence == SEEK_SET && offset < swift::Size(td)  - validated by quick fail above

    // Which bin to seek to?
    int64_t coff = offset - (offset % ft->hashtree()->chunk_size()); // ceil to chunk
    bin_t offbin = bin_t(0,coff/ft->hashtree()->chunk_size());

    char binstr[32];
    dprintf("%s F%i Seek: to bin %s\n",tintstr(), td, offbin.str(binstr) );

    return ft->picker()->Seek(offbin,whence);
}



void swift::AddPeer(Address& addr, const Sha1Hash& swarmid)
{
    if (api_debug)
	fprintf(stderr,"swift::AddPeer addr %s hash %s\n", addr.str(), swarmid.hex().c_str() );

    ContentTransfer *ct = NULL;
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(swarmid);
    if (swarm == NULL)
	ct = (ContentTransfer *)LiveTransfer::FindBySwarmID(swarmid);
    else
    {
	if (!swarm->Touch()) {
	    swarm = SwarmManager::GetManager().ActivateSwarm(swarmid);
	    if (!swarm->Touch())
		return;
	}
	ct = (ContentTransfer *)swarm->GetTransfer();
    }
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
 * LIVE
 */


LiveTransfer *swift::LiveCreate(std::string filename, const Sha1Hash& swarmid, uint32_t chunk_size)
{
    if (api_debug)
	fprintf(stderr,"swift::LiveCreate %s hash %s cs %u\n", filename.c_str(), swarmid.hex().c_str(), chunk_size );

    // Arno: LIVE streams are not managed by SwarmManager
    fprintf(stderr,"swift::LiveCreate: swarmid: %s\n",swarmid.hex().c_str() );
    LiveTransfer *lt = new LiveTransfer(filename,swarmid,true,chunk_size);

    if (lt->IsOperational())
	return lt;
    else
    {
	fprintf(stderr,"swift::LiveCreate: swarm created, but not operational\n",swarmid.hex().c_str() );
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


int swift::LiveOpen(std::string filename, const Sha1Hash& swarmid, Address tracker, bool check_netwvshash, uint32_t chunk_size)
{
    if (api_debug)
	fprintf(stderr,"swift::LiveOpen %s hash %s addr %s cnet %d cs %u\n", filename.c_str(), swarmid.hex().c_str(), tracker.str(), check_netwvshash, chunk_size );

    LiveTransfer *lt = new LiveTransfer(filename,swarmid,false,chunk_size);

    // initiate tracker connections
    // SWIFTPROC
    lt->SetTracker(tracker);
    lt->ConnectToTracker();
    return lt->td();
}


uint64_t  swift::GetHookinOffset(int td)
{
    if (api_debug)
	fprintf(stderr,"swift::GetHookinOffset td %s\n", td );

    LiveTransfer *lt = LiveTransfer::FindByTD(td);
    if (lt == NULL)
	return 0; // also for FileTransfer
    else
	return lt->GetHookinOffset();
}

