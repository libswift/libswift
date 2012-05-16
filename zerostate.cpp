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
    for(int i=0; i<FileTransfer::files.size(); i++)
    {
        if (FileTransfer::files[i] && FileTransfer::files[i]->IsZeroState())
        {
        	FileTransfer *ft = FileTransfer::files[i];
        	if (ft->GetChannels().size() == 0)
        	{
        		// Ain't go no clients, cleanup.
        		delset.insert(ft);
        	}
        	else
        		dprintf("%s #%u zero clean %s has %d peers\n",tintstr(),ft->root_hash().hex().c_str(), ft->GetChannels().size() );
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
	//std::string file_name = "content.avi";
	std::string file_name = contentdir_+FILE_SEP+root_hash.hex();
	uint32_t chunk_size=SWIFT_DEFAULT_CHUNK_SIZE;

	fprintf(stderr,"swift: zero: Got request for %s\n",root_hash.hex().c_str() );

	FileTransfer *ft = new FileTransfer(file_name,root_hash,false,chunk_size,true);
	return ft;
}



void Channel::RecvZeroState(struct evbuffer *evb)
{
	while (evbuffer_get_length(evb)) {
        uint8_t type = evbuffer_remove_8(evb);

		fprintf(stderr,"ZeroStatePeer::Recv GOT %d\n", type);

		int ret = -1;
        switch (type) {
            case SWIFT_HANDSHAKE: OnHandshake(evb); break;
            case SWIFT_DATA:      OnDataZeroState(evb); break;
            case SWIFT_HAVE:      OnHaveZeroState(evb); break;
            case SWIFT_ACK:       OnAck(evb); break;
            case SWIFT_HASH:      OnHashZeroState(evb); break;
            case SWIFT_HINT:      OnHint(evb); break;
            case SWIFT_PEX_ADD:   OnPexAddZeroState(evb); break;
            case SWIFT_PEX_REQ:   OnPexReqZeroState(evb); break;
            case SWIFT_RANDOMIZE: OnRandomize(evb); break;
            default:
                dprintf("%s #%u zero ?msg id unknown %i\n",tintstr(),id_,(int)type);
        }
    }
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

