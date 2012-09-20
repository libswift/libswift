/*
 *  zerostate.cpp
 *  manager for starting on-demand transfers that serve content and hashes
 *  directly from disk (so little state in memory). Requires content (named
 *  as roothash-in-hex), hashes (roothash-in-hex.mhash file) and checkpoint
 *  (roothash-in-hex.mbinmap) to be present on disk.
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include "compat.h"

using namespace swift;


ZeroState * ZeroState::__singleton = NULL;

#define CLEANUP_INTERVAL			30	// seconds

ZeroState::ZeroState() : contentdir_("."), connect_timeout_(TINT_NEVER)
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
    std::set<FileTransfer *>    delset;
    for(int i=0; i<ContentTransfer::swarms.size(); i++)
    {
        ContentTransfer *ct = ContentTransfer::swarms[i];
	if (ct == NULL || ct->ttype() != FILE_TRANSFER)
  	    continue;

	FileTransfer *ft = (FileTransfer *)ct;
    	if (!ft->IsZeroState())
    	    continue;

    	// Arno, 2012-09-20: Work with copy of list, as "delete c" edits list.
    	channels_t copychans(*ft->GetChannels());
	if (copychans.size() == 0)
	{
	    // Ain't go no clients, cleanup transfer.
	    delset.insert(ft);
	}
	else if (zs->connect_timeout_ != TINT_NEVER)
	{
	    // Garbage collect really slow connections, essential on Mac.
	    dprintf("%s zero clean %s has %d peers\n",tintstr(),ft->swarm_id().hex().c_str(), ft->GetChannels()->size() );
	    channels_t::iterator iter2;
	    for (iter2=copychans.begin(); iter2!=copychans.end(); iter2++) {
		Channel *c = *iter2;
		if (c != NULL)
		{
		    //fprintf(stderr,"%s F%u zero clean %s opentime %lld connect %lld\n",tintstr(),ft->fd(), c->peer().str(), (NOW-c->GetOpenTime()), zs->connect_timeout_ );
		    // Garbage collect copychans when open for long and slow upload
		    if ((NOW-c->GetOpenTime()) > zs->connect_timeout_)
		    {
			//fprintf(stderr,"%s F%u zero clean %s opentime %lld ulspeed %lf\n",tintstr(),ft->fd(), c->peer().str(), (NOW-c->GetOpenTime())/TINT_SEC, ft->GetCurrentSpeed(DDIR_UPLOAD) );
			fprintf(stderr,"%s F%u zero clean %s close slow channel\n",tintstr(),ft->fd(), c->peer().str() );
			c->Close();
			delete c;
		    }
		}
	    }
	    if (ft->GetChannels()->size() == 0)
	    {
		// Ain't go no clients left, cleanup transfer.
		delset.insert(ft);
	    }
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

void ZeroState::SetConnectTimeout(tint timeout)
{
    //fprintf(stderr,"ZeroState: SetConnectTimeout: %lld\n", timeout/TINT_SEC );
    connect_timeout_ = timeout;
}


FileTransfer * ZeroState::Find(Sha1Hash &root_hash)
{
    //fprintf(stderr,"swift: zero: Got request for %s\n",root_hash.hex().c_str() );

    //std::string file_name = "content.avi";
    std::string file_name = contentdir_+FILE_SEP+root_hash.hex();
    uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE;

    dprintf("%s #0 zero find %s from %s\n",tintstr(),file_name.c_str(), getcwd_utf8().c_str() );

    std::string reqfilename = file_name;
    int ret = file_exists_utf8(reqfilename);
    if (ret < 0 || ret == 0 || ret == 2)
        return NULL;
    reqfilename = file_name+".mbinmap";
    ret = file_exists_utf8(reqfilename);
    if (ret < 0 || ret == 0 || ret == 2)
        return NULL;
    reqfilename = file_name+".mhash";
    ret = file_exists_utf8(reqfilename);
    if (ret < 0 || ret == 0 || ret == 2)
        return NULL;

    FileTransfer *ft = new FileTransfer(file_name,root_hash,false,true,chunk_size,true);
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

