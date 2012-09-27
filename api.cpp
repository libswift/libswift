/*
 *  api.cpp
 *  Swift top-level API implementation
 *
 *  Created by Victor Grishchenko on 3/6/09.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */


#include "swift.h"

using namespace std;
using namespace swift;

/*
 * Global Operations
 */

int     swift::Listen (Address addr) {
    sckrwecb_t cb;
    cb.may_read = &Channel::LibeventReceiveCallback;
    cb.sock = Channel::Bind(addr,cb);
    // swift UDP receive
    event_assign(&Channel::evrecv, Channel::evbase, cb.sock, EV_READ,
         cb.may_read, NULL);
    event_add(&Channel::evrecv, NULL);
    return cb.sock;
}


void    swift::Shutdown (int sock_des) {
    Channel::Shutdown();
}


/*
 * Per-Swarm Operations
 */


int swift::Open( std::string filename, const Sha1Hash& hash, Address tracker, bool force_check_diskvshash, bool check_netwvshash, uint32_t chunk_size) {
    SwarmData* swarm = SwarmManager::GetManager().AddSwarm( filename, hash, tracker, force_check_diskvshash, check_netwvshash, chunk_size, false );
    if (swarm)
	return swarm->Id();
    return -1;
}


void swift::Close( int transfer, bool removestate, bool removecontent ) {
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (swarm)
	SwarmManager::GetManager().RemoveSwarm( swarm->RootHash(), removestate, removecontent );
}

int swift::Find (Sha1Hash hash) {
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm(hash);
    if (!swarm)
        return -1;
    return swarm->Id();
}


ssize_t swift::Read( int transfer, void *buf, size_t nbyte, int64_t offset )
{
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return -1;
    if (!swarm->Touch()) {
	swarm = SwarmManager::GetManager().ActivateSwarm( swarm->RootHash() );
	if (!swarm->Touch())
	    return -1;
    }
    ContentTransfer* ct = swarm->GetTransfer();
    if (!ct)
	return -1;
    return ct->GetStorage()->Read(buf, nbyte, offset);
}

ssize_t swift::Write( int transfer, const void *buf, size_t nbyte, int64_t offset )
{
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return -1;
    if (!swarm->Touch()) {
	swarm = SwarmManager::GetManager().ActivateSwarm( swarm->RootHash() );
	if (!swarm->Touch())
	    return -1;
    }
    ContentTransfer* ct = swarm->GetTransfer();
    if (!ct)
	return -1;
    return ct->GetStorage()->Write(buf, nbyte, offset);
}


/*
 * Swarm Info
 */

uint64_t swift::Size( int transfer ) {
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return 0;
    return swarm->Size();
}



bool swift::IsComplete( int transfer ) {
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return false;
    return swarm->IsComplete();
}


uint64_t swift::Complete( int transfer ) {
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return 0;
    return swarm->Complete();
}


uint64_t swift::SeqComplete( int transfer, int64_t offset ) {
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return 0;

    // ARNOTODO: why is this an activate? Could be cached
    // SwarmMgr:SeqComplete already does activate in case offset != 0

    if (!swarm->Touch()) {
	swarm = SwarmManager::GetManager().ActivateSwarm( swarm->RootHash() );
	if (!swarm->Touch())
	    return 0;
    }
    ContentTransfer* ct = swarm->GetTransfer();
    if (!ct)
	return 0;
    if (ct->ttype() == FILE_TRANSFER)
    {
	FileTransfer *ft = (FileTransfer *)ct;
        return ct->hashtree()->seq_complete(offset);
    }
    else
	LiveTransfer *lt = (LiveTransfer *)ct;
        return lt->SeqComplete(); // No range support for live
}


const Sha1Hash& swift::SwarmID(int fdes) {
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( fdes );
    if (!swarm)
	return Sha1Hash::ZERO;
    return swarm->swarm_id();
}


/** Returns the number of bytes in a chunk for this transmission */
uint32_t swift::ChunkSize( int fdes)
{
    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if( !swarm )
	return 0;
    return swarm->ChunkSize();
}



tdlist_t swift::GetTransferDescriptors()
{

}

void swift::SetMaxSpeed(int td, data_direction_t ddir, double m)
{

}

double swift::GetCurrentSpeed(int td, data_direction_t ddir)
{

}


transfer_t swift::ttype(int td)
{

}


Storage *swift::GetStorage(int td)
{
}

std::string swift::GetOSPathName(int td)
{

}

bool swift::IsOperational(int td)
{
}




//CHECKPOINT
int swift::Checkpoint(int fdes) {
    // If file, save transfer's binmap for zero-hashcheck restart

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return -1;
    ContentTransfer* ct = swarm->GetTransfer(false);
    if (!ct || ct->ttype() == LIVE_TRANSFER)
        return -1;
    FileTransfer *ft = (FileTransfer *)ct;
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
    //fprintf(stderr,"swift: HACK checkpointing %s at %lli\n", binmap_filename.c_str(), Complete(fdes));
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
int swift::Seek(int fdes, int64_t offset, int whence)
{
    dprintf("%s F%i Seek: to %lld\n",tintstr(), fdes, offset );

    // Quick fail in order not to activate a swarm only to fail after activation
    if( whence != SEEK_SET ) // TODO other
	return -1;
    if( offset >= swift::Size(fdes) )
	return -1;

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if (!swarm)
	return -1;
    if( !swarm->Touch() ) {
	swarm = SwarmManager::GetManager().ActivateSwarm( swarm->swarm_id() );
	if (!swarm->Touch())
	    return -1;
    }
    ContentTransfer* ct = swarm->GetTransfer();
    if (!ct))
	return -1;

    // ARNOTODO: no seek in live, so don't activate, or something
    if (ct->ttype() == LIVE_TRANSFER)
        return -1;

    FileTransfer *ft = (FileTransfer *)ct;

    // whence == SEEK_SET && offset < swift::Size(transfer)  - validated by quick fail above

    // Which bin to seek to?
    int64_t coff = offset - (offset % ft->hashtree()->chunk_size()); // ceil to chunk
    bin_t offbin = bin_t(0,coff/ft->hashtree()->chunk_size());

    char binstr[32];
    dprintf("%s F%i Seek: to bin %s\n",tintstr(), fdes, offbin.str(binstr) );

    return ft->picker()->Seek(offbin,whence);
}





void swift::AddPeer( Address address, const Sha1Hash& root ) {

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( root );
    if( !swarm )
	return;
    if( !swarm->Touch() ) {
	swarm = SwarmManager::GetManager().ActivateSwarm( root );
	if( !swarm->Touch() )
	    return;
    }
    ContentTransfer* ct = swarm->GetTransfer();
    if (ct)
	ct->AddPeer(address);
    // FIXME: When cached addresses are supported in swapped-out swarms, add the peer to that cache instead
}



/*
 * Progress Monitoring
 */


void swift::AddProgressCallback (int transfer,ProgressCallback cb,uint8_t agg) {

    //fprintf(stderr,"swift::AddProgressCallback: transfer %i\n", transfer );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if( !swarm )
        return;
    swarm->AddProgressCallback( cb, agg );

    //fprintf(stderr,"swift::AddProgressCallback: swarm obj %p %p\n", swarm, cb );
}



void swift::RemoveProgressCallback (int transfer, ProgressCallback cb) {

    //fprintf(stderr,"swift::RemoveProgressCallback: transfer %i\n", transfer );

    SwarmData* swarm = SwarmManager::GetManager().FindSwarm( transfer );
    if( !swarm )
        return;
    swarm->RemoveProgressCallback( cb );
}






/*
 * LIVE
 */


LiveTransfer *swift::LiveCreate(std::string filename, const Sha1Hash& swarmid, size_t chunk_size)
{
    // ARNOTODO: SwarmManager integration, or not.

    fprintf(stderr,"live: swarmid: %s\n",swarmid.hex().c_str() );
    LiveTransfer *lt = new LiveTransfer(filename,swarmid,true,chunk_size);
    return lt;
}


int swift::LiveWrite(LiveTransfer *lt, const void *buf, size_t nbyte, long offset)
{
    return lt->AddData(buf,nbyte);
}


int swift::LiveOpen(std::string filename, const Sha1Hash& hash,Address tracker,  bool check_netwvshash, size_t chunk_size)
{
    LiveTransfer *lt = new LiveTransfer(filename,hash,false,chunk_size);

    // initiate tracker connections
    // SWIFTPROC
    lt->SetTracker(tracker);
    lt->ConnectToTracker();
    return lt->fd();
}


uint64_t  swift::GetHookinOffset(int fdes)
{
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
        if (ContentTransfer::swarms[fdes]->ttype() == FILE_TRANSFER)
            return 0;
        else
            return ((LiveTransfer *)ContentTransfer::swarms[fdes])->GetHookinOffset();
    }
    else
       return 0;
}


