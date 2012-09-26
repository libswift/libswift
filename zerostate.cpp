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
	std::list<Sha1Hash> delset;
    for( SwarmManager::Iterator iter = SwarmManager::GetManager().begin(); iter != SwarmManager::GetManager().end(); iter++ ) {
        if( (*iter)->IsZeroState() ) { 
            FileTransfer* ft = (*iter)->GetTransfer(false);
            if( ft ) {
                if( ft->GetChannels().size() == 0 )
                    delset.push_back( (*iter)->RootHash() );
                else
                    dprintf("%s zero clean %s has %d peers\n",tintstr(),ft->root_hash().hex().c_str(), (int)ft->GetChannels().size() );
            }
        }
    }

    // Delete 0-state FileTransfers sans peers
	std::list<Sha1Hash>::iterator deliter;
	for (deliter=delset.begin(); deliter!=delset.end(); deliter++)
	{
		dprintf("%s hash %s zero clean close\n",tintstr(),(*deliter).hex().c_str() );
		//fprintf(stderr,"%s F%u zero clean close\n",tintstr(),ft->transfer_id() );
        SwarmManager::GetManager().DeactivateSwarm( *deliter );
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


int ZeroState::Find(Sha1Hash &root_hash)
{
	//fprintf(stderr,"swift: zero: Got request for %s\n",root_hash.hex().c_str() );

	//std::string file_name = "content.avi";
	std::string file_name = contentdir_+FILE_SEP+root_hash.hex();
	uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE;

	std::string reqfilename = file_name;
    int ret = file_exists_utf8(reqfilename);
	if (ret <= 0 || ret == 2)
		return -1;
	reqfilename = file_name+".mbinmap";
    ret = file_exists_utf8(reqfilename);
	if (ret <= 0 || ret == 2)
		return -1;
	reqfilename = file_name+".mhash";
    ret = file_exists_utf8(reqfilename);
	if (ret <= 0 || ret == 2)
		return -1;

    SwarmData* swarm = SwarmManager::GetManager().AddSwarm(file_name, root_hash, Address(), false, chunk_size, true);
    if( !swarm )
        return -1;
    FileTransfer* ft = swarm->GetTransfer();
    if( !ft ) {
        SwarmData* newSwarm = SwarmManager::GetManager().ActivateSwarm(swarm->RootHash());
        if( !newSwarm )
            return swarm->Id();
        swarm = newSwarm;
        ft = swarm->GetTransfer();
        if( !ft )
            return swarm->Id();
    }
	if (ft->hashtree() == NULL || !ft->hashtree()->is_complete())
	{
		// Safety catch
		return -1;
	}
	else
  	    return swarm->Id();
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

