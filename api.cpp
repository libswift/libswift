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

int      swift::Open (const char* filename, const Sha1Hash& hash, Address tracker, bool check_hashes, size_t chunk_size) {
    FileTransfer* ft = new FileTransfer(filename, hash, check_hashes, chunk_size);
    if (ft && ft->hashtree()->file_descriptor()) {

        /*if (ContentTransfer::swarms.size()<fdes)  // FIXME duplication
            ContentTransfer::swarms.resize(fdes);
        ContentTransfer::swarms[fdes] = ft;*/

        // initiate tracker connections
    	// SWIFTPROC
    	Channel *c = NULL;
        if (tracker != Address())
        	c = new Channel(ft,INVALID_SOCKET,tracker,false);
        else if (Channel::tracker!=Address())
        	c = new Channel(ft,INVALID_SOCKET,Channel::tracker,false);
        return ft->hashtree()->file_descriptor();
    } else {
        if (ft)
            delete ft;
        return -1;
    }
}


void    swift::Close (int fd) {
    if (fd<ContentTransfer::swarms.size() && ContentTransfer::swarms[fd])
        delete ContentTransfer::swarms[fd];
}


void    swift::AddPeer (Address address, const Sha1Hash& root) {
    Channel::peer_selector->AddPeer(address,root);
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


uint64_t  swift::SeqComplete (int fdes) {
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
    	if (ContentTransfer::swarms[fdes]->ttype() == FILE_TRANSFER)
    		return ((FileTransfer *)ContentTransfer::swarms[fdes])->hashtree()->seq_complete();
    	else
    		return ((LiveTransfer *)ContentTransfer::swarms[fdes])->SeqComplete();
    }
    else
        return 0;
}

// LIVE
uint64_t  swift::LiveStart(int fdes)
{
	if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
	   	if (ContentTransfer::swarms[fdes]->ttype() == FILE_TRANSFER)
	   		return 0;
	   	else
	   		return ((LiveTransfer *)ContentTransfer::swarms[fdes])->LiveStart();
    }
    else
	    return 0;
}


const Sha1Hash& swift::RootMerkleHash (int fd) {
    ContentTransfer* trans = ContentTransfer::transfer(fd);
    if (!trans)
        return Sha1Hash::ZERO;
    return trans->root_hash();
}


/** Returns the number of bytes in a chunk for this transmission */
size_t	  swift::ChunkSize(int fdes)
{
    if (ContentTransfer::swarms.size()>fdes && ContentTransfer::swarms[fdes]) {
    	return ContentTransfer::swarms[fdes]->chunk_size();
    }
    else
        return 0;
}


int swift:: Find (Sha1Hash hash) {
    ContentTransfer* t = ContentTransfer::Find(hash);
    if (t)
        return t->fd();
    return -1;
}


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
    trans->ack_out().set(piece); // that easy
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

