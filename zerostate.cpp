/*
 *  zerostate.cpp
 *  Class to seed content directly from disk, both hashes and data.
 *
 *  Created by Arno Bakker.
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 */

#include "swift.h"
#include "compat.h"

using namespace swift;


ZeroState * ZeroState::__singleton = NULL;

#define CLEANUP_INTERVAL	5	// seconds

ZeroState::ZeroState() : contentdir_(".")
{
	if (__singleton == NULL)
	{
		__singleton = this;
	}

	//fprintf(stderr,"ZeroState: registering clean up\n");
	evtimer_assign(&evclean_,Channel::evbase,&ZeroState::LibeventCleanCallback,this);
	evtimer_add(&evclean_,tint2tv(CLEANUP_INTERVAL*TINT_SEC));
}


ZeroState::~ZeroState()
{
	//fprintf(stderr,"ZeroState: deconstructor\n");

    // Arno, 2012-02-06: Cancel cleanup timer, otherwise chaos!
    evtimer_del(&evclean_);
}


void ZeroState::LibeventCleanCallback(int fd, short event, void *arg)
{
	//fprintf(stderr,"zero clean: enter\n");

	// Arno, 2012-02-24: Why-oh-why, update NOW
	Channel::Time();

	ZeroState *zs = (ZeroState *)arg;
	if (zs == NULL)
		return;

	// See which zero state FileTransfers have no clients
	std::set<FileTransfer *>	delset;
    for(int i=0; i<ContentTransfer::swarms.size(); i++)
    {
    	FileTransfer *ft = (FileTransfer *)ContentTransfer::swarms[i];
        if (ft && ft->IsZeroState())
        {

        	if (ft->GetChannels().size() == 0)
        	{
        		// Ain't go no clients, cleanup.
        		delset.insert(ft);
        	}
        	else
        		dprintf("%s zero clean %s has %d peers\n",tintstr(),ft->root_hash().hex().c_str(), ft->GetChannels().size() );
        }
    }

    // Delete 0-state FileTransfers sans peers
	std::set<FileTransfer *>::iterator iter;
	for (iter=delset.begin(); iter!=delset.end(); iter++)
	{
		FileTransfer *ft = *iter;
		dprintf("%s F%u zero clean close\n",tintstr(),ft->fd() );
		//fprintf(stderr,"%s F%u zero clean close\n",tintstr(),ft->fd() );
		swift::Close(ft->fd());
	}

	// Reschedule cleanup
	evtimer_add(&(zs->evclean_),tint2tv(CLEANUP_INTERVAL*TINT_SEC));
}



ZeroState * ZeroState::GetInstance()
{
	//fprintf(stderr,"ZeroState::GetInstance: %p\n", Channel::evbase );
	if (__singleton == NULL)
	{
		new ZeroState();
	}
	return __singleton;
}


void ZeroState::SetContentDir(std::string contentdir)
{
	contentdir_ = contentdir;
}


FileTransfer * ZeroState::Find(Sha1Hash &root_hash)
{
	//fprintf(stderr,"swift: zero: Got request for %s\n",root_hash.hex().c_str() );

	//std::string file_name = "content.avi";
	std::string file_name = contentdir_+FILE_SEP+root_hash.hex();
	uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE;

	std::string reqfilename = file_name;
    int ret = file_exists_utf8(reqfilename);
	if (ret == 0 || ret == 2)
		return NULL;
	reqfilename = file_name+".mbinmap";
    ret = file_exists_utf8(reqfilename);
	if (ret == 0 || ret == 2)
		return NULL;
	reqfilename = file_name+".mhash";
    ret = file_exists_utf8(reqfilename);
	if (ret == 0 || ret == 2)
		return NULL;

	FileTransfer *ft = new FileTransfer(file_name,root_hash,false,chunk_size,true);
	if (ft->hashtree() == NULL || !ft->hashtree()->is_complete())
	{
		// Safety catch
		return NULL; 
	}
	else
  	    return ft;
}


void Channel::OnDataZeroState(struct evbuffer *evb)
{
	dprintf("%s #%u zero -data, don't need it, am a seeder\n",tintstr(),id_);
}

void Channel::OnHaveZeroState(struct evbuffer *evb)
{
	uint32_t binint = evbuffer_remove_32be(evb);
	// Forget about it, i.e.. don't build peer binmap.
}

void Channel::OnHashZeroState(struct evbuffer *evb)
{
	dprintf("%s #%u zero -hash, don't need it, am a seeder\n",tintstr(),id_);
}

void Channel::OnPexAddZeroState(struct evbuffer *evb)
{
    uint32_t ipv4 = evbuffer_remove_32be(evb);
    uint16_t port = evbuffer_remove_16be(evb);
    // Forget about it
}

void Channel::OnPexReqZeroState(struct evbuffer *evb)
{
    // Ignore it
}

