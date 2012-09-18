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


int swift::Find (Sha1Hash hash) {
    ContentTransfer* t = ContentTransfer::Find(hash);
    if (t)
        return t->fd();
    return -1;
}


/*
 * Per-Swarm Operations
 */

int swift::Open (std::string filename, const Sha1Hash& roothash, Address tracker, bool force_check_diskvshash, bool check_netwvshash, uint32_t chunk_size) {
    FileTransfer* ft = new FileTransfer(filename, roothash, force_check_diskvshash, check_netwvshash, chunk_size);
    if (ft->fd() && ft->IsOperational()) {

        // initiate tracker connections
    	// SWIFTPROC
    	ft->SetTracker(tracker);
    	ft->ConnectToTracker();

    	return ft->fd();
    } else {
		delete ft;
        return -1;
    }
}



void    swift::Close (int fd) {
    if (fd<ContentTransfer::swarms.size() && ContentTransfer::swarms[fd])
        delete ContentTransfer::swarms[fd];
}


ssize_t  swift::Read(int fdes, void *buf, size_t nbyte, int64_t offset)
{
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes])
        return ContentTransfer::swarms[fdes]->GetStorage()->Read(buf,nbyte,offset);
    else
        return -1;
}

ssize_t  swift::Write(int fdes, const void *buf, size_t nbyte, int64_t offset)
{
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes])
        return ContentTransfer::swarms[fdes]->GetStorage()->Write(buf,nbyte,offset);
    else
        return -1;
}

uint64_t  swift::Size (int fdes) {
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
    	if (ContentTransfer::swarms[fdes]->ttype() == FILE_TRANSFER)
            return ((FileTransfer *)ContentTransfer::swarms[fdes])->hashtree()->size();
    	else
            return 0;
    }
    else
        return 0;
}


bool  swift::IsComplete (int fdes) {
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
    	if (ContentTransfer::swarms[fdes]->ttype() == FILE_TRANSFER)
    	    return ((FileTransfer *)ContentTransfer::swarms[fdes])->hashtree()->is_complete();
    	else
    	    return false;
    }
    else
        return false;
}


uint64_t  swift::Complete (int fdes) {
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {

    	if (ContentTransfer::swarms[fdes]->ttype() == FILE_TRANSFER)
    	     return ((FileTransfer *)ContentTransfer::swarms[fdes])->hashtree()->complete();
    	else
    	     return 0;
    }
    else
        return 0;
}


uint64_t  swift::SeqComplete (int fdes, int64_t offset) {
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
    	if (ContentTransfer::swarms[fdes]->ttype() == FILE_TRANSFER)
    	    return ((FileTransfer *)ContentTransfer::swarms[fdes])->hashtree()->seq_complete(offset);
    	else
    	    return ((LiveTransfer *)ContentTransfer::swarms[fdes])->SeqComplete(); // No range support for live
    }
    else
        return 0;
}


const Sha1Hash& swift::SwarmID (int fd) {
    ContentTransfer* trans = ContentTransfer::transfer(fd);
    if (!trans)
        return Sha1Hash::ZERO;
    return trans->swarm_id();
}


size_t	  swift::ChunkSize(int fdes)
{
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
    	return ContentTransfer::swarms[fdes]->chunk_size();
    }
    else
        return 0;
}


int swift::Checkpoint(int fdes) {
    // If file, save transfer's binmap for zero-hashcheck restart

    ContentTransfer* ct = ContentTransfer::transfer(fdes);
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

    ContentTransfer* ct = ContentTransfer::transfer(fdes);
    if (!ct || ct->ttype() == LIVE_TRANSFER)
            return -1;
    FileTransfer *ft = (FileTransfer *)ct;

    if (whence == SEEK_SET)
    {
        if (offset >= swift::Size(fdes))
           return -1; // seek beyond end of content

        // Which bin to seek to?
        int64_t coff = offset - (offset % ft->hashtree()->chunk_size()); // ceil to chunk
        bin_t offbin = bin_t(0,coff/ft->hashtree()->chunk_size());

        char binstr[32];
        dprintf("%s F%i Seek: to bin %s\n",tintstr(), fdes, offbin.str(binstr) );

        return ft->picker()->Seek(offbin,whence);
   }
   else
        return -1; // TODO
}


/*
 * Progress Monitoring
 */

void swift::AddProgressCallback (int fdes,ProgressCallback cb,uint8_t agg) {

   //fprintf(stderr,"swift::AddProgressCallback: transfer %i\n", fdes );

    ContentTransfer* trans = ContentTransfer::transfer(fdes);
    if (!trans)
        return;

    //fprintf(stderr,"swift::AddProgressCallback: ft obj %p %p\n", trans, cb );

    trans->cb_agg[trans->cb_installed] = agg;
    trans->callbacks[trans->cb_installed] = cb;
    trans->cb_installed++;
}


void swift::ExternallyRetrieved (int fdes,bin_t piece) {
    ContentTransfer* trans = ContentTransfer::transfer(fdes);
    if (!trans)
        return;
    trans->ack_out()->set(piece); // that easy
}


void swift::RemoveProgressCallback (int fdes, ProgressCallback cb) {

    //fprintf(stderr,"swift::RemoveProgressCallback: transfer %i\n", fdes );

    ContentTransfer* trans = ContentTransfer::transfer(fdes);
    if (!trans)
        return;

    //fprintf(stderr,"swift::RemoveProgressCallback: transfer %i ft obj %p %p\n", fdes, trans, cb );

    for(int i=0; i<trans->cb_installed; i++)
        if (trans->callbacks[i]==cb)
            trans->callbacks[i]=trans->callbacks[--trans->cb_installed];

    for(int i=0; i<trans->cb_installed; i++)
    {
       fprintf(stderr,"swift::RemoveProgressCallback: transfer %i remain %p\n", fdes, trans->callbacks[i] );
    }
}


/*
 * LIVE
 */


LiveTransfer *swift::LiveCreate(std::string filename, const Sha1Hash& swarmid, size_t chunk_size)
{
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

