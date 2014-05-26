/*
 *  cmdgw.cpp
 *  command gateway for controlling swift engine via a TCP connection
 *
 *  Created by Arno Bakker, Riccado Petrocco
 *  Copyright 2010-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <event2/http.h>

#include <iostream>
#include <sstream>

using namespace swift;

// Send PLAY after receiving N bytes
#define CMDGW_MAX_PREBUF_BYTES		(256*1024)

// Report swift download progress every 2^layer * chunksize bytes (so 0 = report every chunk)
#define CMDGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER 	0 	// must be 0
#define CMDGW_PREBUF_PROGRESS_BYTE_INTERVAL_AS_LAYER 	0

// Status of the swarm download
#define DLSTATUS_ALLOCATING_DISKSPACE 	0
#define DLSTATUS_HASHCHECKING  		2
#define DLSTATUS_DOWNLOADING  		3
#define DLSTATUS_SEEDING 		4
#define DLSTATUS_STOPPED_ON_ERROR   	6

#define MAX_CMD_MESSAGE 1024

#define ERROR_NO_ERROR		0
#define ERROR_UNKNOWN_CMD	-1
#define ERROR_MISS_ARG		-2
#define ERROR_BAD_ARG		-3
#define ERROR_BAD_SWARM		-4


#define CMDGW_MAX_CLIENT 1024   // Arno: == maximum number of swarms per proc

struct cmd_gw_t {
    int         id;
    evutil_socket_t   cmdsock;
    int		td; // swift FD
    bool	moreinfo;   // whether to report detailed stats (see SETMOREINFO cmd)
    tint 	startt;	    // ARNOSMPTODO: debug speed measurements, remove
    std::string mfspecname; // MULTIFILE
    uint64_t    startoff;   // MULTIFILE: starting offset in content range of desired file
    uint64_t    endoff;     // MULTIFILE: ending offset (careful, for an e.g. 100 byte interval this is 99)
    bool        playsent;
    std::string xcontentdur;
    std::string contentlenstr;
    std::string mimetype;

} cmd_requests[CMDGW_MAX_CLIENT];


int cmd_gw_reqs_open = 0;
int cmd_gw_reqs_count = 0;
int cmd_gw_conns_open = 0;

struct evconnlistener *cmd_evlistener = NULL;
struct evbuffer *cmd_evbuffer = NULL; // Data received on cmd socket : WARNING: one for all cmd sockets

/*
 * SOCKTUNNEL
 * We added the ability for a process to tunnel data over swift's UDP socket.
 * The process should send TUNNELSEND commands over the CMD TCP socket and will
 * receive TUNNELRECV commands from swift, containing data received via UDP
 * on channel 0xffffffff.
 */
typedef enum {
	CMDGW_TUNNEL_SCAN4CRLF,
	CMDGW_TUNNEL_READTUNNEL
} cmdgw_tunnel_t;

std::vector< uint32_t > tunnel_channels_;

cmdgw_tunnel_t cmd_tunnel_state=CMDGW_TUNNEL_SCAN4CRLF;
uint32_t       cmd_tunnel_expect=0;
Address	       cmd_tunnel_dest_addr;
uint32_t       cmd_tunnel_dest_chanid;
evutil_socket_t cmd_tunnel_sock=INVALID_SOCKET;

// HTTP gateway address for PLAY cmd
Address cmd_gw_httpaddr;
uint64_t cmd_gw_livesource_disc_wnd=POPT_LIVE_DISC_WND_ALL;
popt_cont_int_prot_t cmd_gw_cipm=POPT_CONT_INT_PROT_MERKLE;

// Ric: directory containing the metadata
std::string cmd_gw_metadir;


#define cmd_gw_debug	true

tint cmd_gw_last_open=0;


// Fwd defs
void CmdGwDataCameInCallback(struct bufferevent *bev, void *ctx);
bool CmdGwReadLine(evutil_socket_t cmdsock);
void CmdGwNewRequestCallback(evutil_socket_t cmdsock, char *line);
void CmdGwProcessData(evutil_socket_t cmdsock);


void CmdGwFreeRequest(cmd_gw_t* req)
{
    req->id = -1;
    req->cmdsock = -1;
    req->td = -1;
    req->moreinfo = false;
    req->startt = 0;
    req->mfspecname = "";
    req->startoff = -1;
    req->endoff = -1;
    req->playsent = false;
    req->xcontentdur = "";
    req->mimetype = "";
}


void CmdGwCloseConnection(evutil_socket_t sock)
{
    // Close cmd connection and stop all associated downloads.
    // Doesn't remove .mhash state or content

    if (cmd_gw_debug)
       fprintf(stderr,"CmdGwCloseConnection: ENTER %d\n", sock );

    bool scanning = true;
    while (scanning)
    {
        scanning = false;
        for(int i=0; i<cmd_gw_reqs_open; i++)
        {
            cmd_gw_t* req = &cmd_requests[i];
            if (req->cmdsock==sock)
            {
                dprintf("%s @%i stopping-on-close transfer %i\n",tintstr(),req->id,req->td);
                swift::Close(req->td);

                // Remove from list and reiterate over it
                CmdGwFreeRequest(req);
                *req = cmd_requests[--cmd_gw_reqs_open];
                scanning = true;
                break;
            }
        }
    }

    if (cmd_evbuffer != NULL)
        evbuffer_free(cmd_evbuffer);

    // Arno, 2012-07-06: Close
    swift::close_socket(sock);

    cmd_gw_conns_open--;
  
    // Arno, 2012-10-11: New policy Immediate shutdown on connection close,
    // see CmdGwUpdateDLStatesCallback()
    fprintf(stderr,"cmd: Shutting down on CMD connection close\n");
    event_base_loopexit(Channel::evbase, NULL);
}


cmd_gw_t* CmdGwFindRequestByFD(int td)
{
    for(int i=0; i<cmd_gw_reqs_open; i++)
        if (cmd_requests[i].td==td)
            return cmd_requests+i;
    return NULL;
}

cmd_gw_t* CmdGwFindRequestBySwarmID(SwarmID &swarmid)
{
    int td = swift::Find(swarmid);
    if (td < 0)
	return NULL;
    else
	return CmdGwFindRequestByFD(td);
}


void CmdGwGotCHECKPOINT(SwarmID &swarmid)
{
    // Checkpoint the specified download
    if (cmd_gw_debug)
        fprintf(stderr,"cmd: GotCHECKPOINT: %s\n",swarmid.hex().c_str());

    cmd_gw_t* req = CmdGwFindRequestBySwarmID(swarmid);
    if (req == NULL)
        return;

    swift::Checkpoint(req->td);
}


void CmdGwGotREMOVE(SwarmID &swarmid, bool removestate, bool removecontent)
{
    // Remove the specified download
    if (cmd_gw_debug)
        fprintf(stderr,"cmd: GotREMOVE: %s %d %d\n",swarmid.hex().c_str(),removestate,removecontent);

    cmd_gw_t* req = CmdGwFindRequestBySwarmID(swarmid);
    if (req == NULL)
    {
	if (cmd_gw_debug)
	    fprintf(stderr,"cmd: GotREMOVE: %s not found, bad swarm?\n",swarmid.hex().c_str());
	return;
    }
    dprintf("%s @%i remove transfer %i\n",tintstr(),req->id,req->td);

    // Arno: schaap moved cleanup to SwarmManager
    swift::Close(req->td, removestate, removecontent);

    CmdGwFreeRequest(req);
    *req = cmd_requests[--cmd_gw_reqs_open];
}


void CmdGwGotMAXSPEED(SwarmID &swarmid, data_direction_t ddir, double speed)
{
    // Set maximum speed on the specified download
    //fprintf(stderr,"cmd: GotMAXSPEED: %s %d %lf\n",swarmid.hex().c_str(),ddir,speed);

    cmd_gw_t* req = CmdGwFindRequestBySwarmID(swarmid);
    if (req == NULL)
    	return;
    swift::SetMaxSpeed(req->td, ddir, speed);
}


void CmdGwGotSETMOREINFO(SwarmID &swarmid, bool enable)
{
    cmd_gw_t* req = CmdGwFindRequestBySwarmID(swarmid);
    if (req == NULL)
        return;
    req->moreinfo = enable;
}

void CmdGwGotPEERADDR(SwarmID &swarmid, Address &peer)
{
    cmd_gw_t* req = CmdGwFindRequestBySwarmID(swarmid);
    if (req == NULL)
    	return;
    swift::AddPeer(peer, swarmid);
}



void CmdGwSendINFOHashChecking(evutil_socket_t cmdsock, SwarmID &swarmid)
{
    // Send INFO DLSTATUS_HASHCHECKING message.

    char cmd[MAX_CMD_MESSAGE];
    sprintf(cmd,"INFO %s %d %" PRIi64 "/%" PRIi64 " %lf %lf %" PRIu32 " %" PRIu32 "\r\n",swarmid.hex().c_str(),DLSTATUS_HASHCHECKING,(uint64_t)0,(uint64_t)0,0.0,0.0,0,0);

    //fprintf(stderr,"cmd: SendINFO: %s", cmd);
    send(cmdsock,cmd,strlen(cmd),0);
}


void CmdGwSendINFO(cmd_gw_t* req, int dlstatus)
{
    // Send INFO message.
    //if (cmd_gw_debug)
    //    fprintf(stderr,"cmd: SendINFO: F%d initdlstatus %d\n", req->td, dlstatus );

    SwarmID swarmid = swift::GetSwarmID(req->td);
    if (swarmid == SwarmID::NOSWARMID)
	    return; // Arno: swarm deleted, ignore

    uint64_t size = swift::Size(req->td);
    uint64_t complete = 0;
    if (swift::ttype(req->td) == LIVE_TRANSFER)
        complete = swift::SeqComplete(req->td,swift::GetHookinOffset(req->td));
    else
        complete = swift::Complete(req->td);
    if (size > 0 && size == complete)
        dlstatus = DLSTATUS_SEEDING;
    if (!swift::IsOperational(req->td))
        dlstatus = DLSTATUS_STOPPED_ON_ERROR;

    // schaap FIXME: Are those active leechers and seeders, or potential
    // leechers and seeders? In the latter case, get cached values when cached
    // peers have been implemented.
    uint32_t numleech = swift::GetNumLeechers(req->td);
    uint32_t numseeds = swift::GetNumSeeders(req->td);
    double dlspeed = swift::GetCurrentSpeed(req->td,DDIR_DOWNLOAD);
    double ulspeed = swift::GetCurrentSpeed(req->td,DDIR_UPLOAD);

    char cmd[MAX_CMD_MESSAGE];
    sprintf(cmd,"INFO %s %d %" PRIu64 "/%" PRIu64 " %lf %lf %" PRIu32 " %" PRIu32 "\r\n",swarmid.hex().c_str(),dlstatus,complete,size,dlspeed,ulspeed,numleech,numseeds);

    send(req->cmdsock,cmd,strlen(cmd),0);

    // MORESTATS
    if (req->moreinfo) {
        // Send detailed ul/dl stats in JSON format.

        std::ostringstream oss;
        oss.setf(std::ios::fixed,std::ios::floatfield);
        oss.precision(5);
    	channels_t::iterator iter;
    	channels_t *peerchans = NULL;
    	ContentTransfer *ct = swift::GetActivatedTransfer(req->td);
        if (ct)
            peerchans = ct->GetChannels();

        oss << "MOREINFO" << " " << swarmid.hex() << " ";

        double tss = (double)Channel::Time() / 1000000.0L;
        oss << "{\"timestamp\":\"" << tss << "\", ";
        oss << "\"channels\":";
        oss << "[";
        if (peerchans != NULL)
        {
            for (iter=peerchans->begin(); iter!=peerchans->end(); iter++) {
            Channel *c = *iter;
            if (c == NULL)
                continue;

            if (iter!=peerchans->begin())
                oss << ", ";
            oss << "{";
            oss << "\"ip\": \"" << c->peer().ipstr() << "\", ";
            oss << "\"port\": " << c->peer().port() << ", ";
            oss << "\"raw_bytes_up\": " << c->raw_bytes_up() << ", ";
            oss << "\"raw_bytes_down\": " << c->raw_bytes_down() << ", ";
            oss << "\"bytes_up\": " << c->bytes_up() << ", ";
            oss << "\"bytes_down\": " << c->bytes_down() << " ";
            oss << "}";
            }
        }
        oss << "], ";
        oss << "\"raw_bytes_up\": " << Channel::global_raw_bytes_up << ", ";
        oss << "\"raw_bytes_down\": " << Channel::global_raw_bytes_down << ", ";
        oss << "\"bytes_up\": " << Channel::global_bytes_up << ", ";
        oss << "\"bytes_down\": " << Channel::global_bytes_down << " ";
        oss << "}";

        oss << "\r\n";

        std::stringbuf *pbuf=oss.rdbuf();
        size_t slen = strlen(pbuf->str().c_str());
        send(req->cmdsock,pbuf->str().c_str(),slen,0);
    }
}


void CmdGwSendPLAY(cmd_gw_t *req)
{
    // Send PLAY message to user
    if (cmd_gw_debug)
        fprintf(stderr,"cmd: SendPLAY: %d\n", req->td);

    SwarmID swarmid = swift::GetSwarmID(req->td);

    std::ostringstream oss;
    oss << "PLAY ";
    oss << swarmid.hex() << " ";
    oss << "http://";
    oss << cmd_gw_httpaddr.str();
    oss << "/";
    oss << swarmid.hex();
    // Arno, 2013-09-25: Only append params that need to be echoed back
    // by the HTTPGW, because the Transfer already exists.
    bool added=false;
    if (req->xcontentdur != "")
    {
	if (!added)
	    oss << "?";
	oss << "cd=" << req->xcontentdur;
	added = true;
    }
    if (req->mimetype != "")
    {
	if (!added)
	    oss << "?";
	else
	    oss << "&";
	oss << "mt=" << req->mimetype;
	added = true;
    }
    // Arno FIXME: Should be replaced with swift::SetSize() method that stores value from URL.
    if (req->contentlenstr != "")
    {
	if (!added)
	    oss << "?";
	else
	    oss << "&";
	oss << "cl=" << req->contentlenstr;
	added = true;
    }
    oss << "\r\n";

    std::stringbuf *pbuf=oss.rdbuf();
    if (cmd_gw_debug)
        fprintf(stderr,"cmd: SendPlay: %s", pbuf->str().c_str());
    size_t slen = strlen(pbuf->str().c_str());
    send(req->cmdsock,pbuf->str().c_str(),slen,0);
}


void CmdGwSendERRORBySocket(evutil_socket_t cmdsock, std::string msg, const SwarmID& swarmid=SwarmID::NOSWARMID)
{
     std::string cmd = "ERROR ";
     cmd += swarmid.hex();
     cmd += " ";
     cmd += msg;
     cmd += "\r\n";

     if (cmd_gw_debug)
         fprintf(stderr,"cmd: SendERROR: %s\n", cmd.c_str() );

     char *wire = strdup(cmd.c_str());
     send(cmdsock,wire,strlen(wire),0);
     free(wire);
}


/*
 * For VOD and Live, wait until PREBUF_BYTES are in before sending PLAY.
 */
void CmdGwSwiftPrebufferProgressCallback (int td, bin_t bin)
{
    //
    // Subsequent bytes of content downloaded
    //
    //if (cmd_gw_debug)
    //    fprintf(stderr,"cmd: SwiftPrebuffProgress: %d\n", td );

    cmd_gw_t* req = CmdGwFindRequestByFD(td);
    if (req == NULL)
        return;

    uint64_t wantsize = std::min(req->endoff+1-req->startoff,(uint64_t)CMDGW_MAX_PREBUF_BYTES);

    //if (cmd_gw_debug)
    //   fprintf(stderr,"cmd: SwiftPrebuffProgress: want %" PRIu64 " got %" PRIu64 "\n", swift::SeqComplete(req->td,req->startoff), wantsize );


    if (swift::SeqComplete(req->td,req->startoff) >= wantsize)
    {
        // First CMDGW_MAX_PREBUF_BYTES bytes received via swift,
        // tell user to PLAY
        // ARNOSMPTODO: bitrate-dependent prebuffering?
        //if (cmd_gw_debug)
        //    fprintf(stderr,"cmd: SwiftPrebufferProgress: Prebuf done %d\n", td );

        swift::RemoveProgressCallback(td,&CmdGwSwiftPrebufferProgressCallback);

        CmdGwSendPLAY(req);
    }
    // wait for prebuffer
}


/*
 * For single file static content, install a new callback that checks whether
 * we have enough data prebuffered. For multifile, wait till the first chunks
 * containing the multi-file spec have been loaded, then set download pointer
 * to the desired file (via swift::Seek) and then wait till enough data is
 * prebuffered (via CmdGwSwiftPrebufferingProcessCallback).
 */

void CmdGwSwiftVODFirstProgressCallback (int td, bin_t bin)
{
    //
    // First bytes of content downloaded (first in absolute sense)
    //
    if (cmd_gw_debug)
        fprintf(stderr,"cmd: SwiftFirstProgress: %d\n", td );

    cmd_gw_t* req = CmdGwFindRequestByFD(td);
    if (req == NULL)
        return;

    if (swift::ttype(td) == LIVE_TRANSFER)
    {
        // Shouldn't happen for LIVE
        swift::RemoveProgressCallback(td,&CmdGwSwiftVODFirstProgressCallback);
        return;
    }

    // VOD from here
    Storage *storage = swift::GetStorage(td);
    if (storage == NULL)
	return;
    if (!storage->IsReady()) {
        // Wait until (multi-file) storage is ready
        return;
    }

    swift::RemoveProgressCallback(td,&CmdGwSwiftVODFirstProgressCallback);

    if (req->mfspecname == "")
    {
        // Single file
        req->startoff = 0;
        req->endoff = swift::Size(req->td)-1;

        CmdGwSwiftPrebufferProgressCallback(req->td,bin_t(0,0)); // in case file on disk
        if (!req->playsent)
            swift::AddProgressCallback(td,&CmdGwSwiftPrebufferProgressCallback,CMDGW_PREBUF_PROGRESS_BYTE_INTERVAL_AS_LAYER);
    }
    else
    {
        // MULTIFILE
        // Have spec, seek to wanted file

        storage_files_t sfs = storage->GetStorageFiles();
        storage_files_t::iterator iter;
        bool found = false;
        for (iter = sfs.begin(); iter < sfs.end(); iter++)
        {
            StorageFile *sf = *iter;
            if (sf->GetSpecPathName() == req->mfspecname)
            {
                if (cmd_gw_debug)
                    fprintf(stderr,"cmd: SwiftFirstProgress: Seeking to multifile %s for %d\n", req->mfspecname.c_str(), td );

                int ret = swift::Seek(req->td,sf->GetStart(),SEEK_SET);
                if (ret < 0)
                {
                    CmdGwSendERRORBySocket(req->cmdsock,"Error seeking to file in multi-file content.");
                    return;
                }
                found = true;
                req->startoff = sf->GetStart();
                req->endoff = sf->GetEnd();
                CmdGwSwiftPrebufferProgressCallback(req->td,bin_t(0,0)); // in case file on disk
                swift::AddProgressCallback(td,&CmdGwSwiftPrebufferProgressCallback,CMDGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER);
                break;
            }
        }
        if (!found) 
        {
            if (cmd_gw_debug)
                fprintf(stderr,"cmd: SwiftFirstProgress: Error file not found %d\n", td );

	        CmdGwSendERRORBySocket(req->cmdsock,"Individual file not found in multi-file content.",GetSwarmID(req->td));
	        return;
	    }
    }

}


void CmdGwSwiftErrorCallback (evutil_socket_t cmdsock)
{
    // Error on swift socket callback

    const char *response = "ERROR Swift Engine Problem\r\n";
    send(cmdsock,response,strlen(response),0);

    //swift::close_socket(sock);
}

void CmdGwSwiftAllocatingDiskspaceCallback (int td, bin_t bin)
{
    //if (cmd_gw_debug)
    //    fprintf(stderr,"cmd: CmdGwSwiftAllocatingDiskspaceCallback: ENTER %d\n", td );

    // Called before swift starts reserving diskspace.
    cmd_gw_t* req = CmdGwFindRequestByFD(td);
    if (req == NULL)
        return;

    CmdGwSendINFO(req,DLSTATUS_ALLOCATING_DISKSPACE);
}



void CmdGwUpdateDLStateCallback(cmd_gw_t* req)
{
    // Periodic callback, tell user INFO
    CmdGwSendINFO(req,DLSTATUS_DOWNLOADING);
}


int icount=0;

void CmdGwUpdateDLStatesCallback()
{
    // Called by swift main approximately every second
    // Loop over all swarms
    for(int i=0; i<cmd_gw_reqs_open; i++)
    {
        cmd_gw_t* req = &cmd_requests[i];
        CmdGwUpdateDLStateCallback(req);
    }
}



void CmdGwDataCameInCallback(struct bufferevent *bev, void *ctx)
{
    // Turn TCP stream into lines deliniated by \r\n

    evutil_socket_t cmdsock = bufferevent_getfd(bev);
    //if (cmd_gw_debug)
    //    fprintf(stderr,"CmdGwDataCameIn: ENTER %d\n", cmdsock );

    struct evbuffer *inputevbuf = bufferevent_get_input(bev);

    int inlen = evbuffer_get_length(inputevbuf);

    int ret = evbuffer_add_buffer(cmd_evbuffer,inputevbuf);
    if (ret == -1) {
        CmdGwCloseConnection(cmdsock);
        return;
    }

    int totlen = evbuffer_get_length(cmd_evbuffer);

    //if (cmd_gw_debug)
    //    fprintf(stderr,"cmdgw: TCPDataCameIn: State %d, got %d new bytes, have %d want %d\n", (int)cmd_tunnel_state, inlen, totlen, cmd_tunnel_expect );

    CmdGwProcessData(cmdsock);
}


void CmdGwProcessData(evutil_socket_t cmdsock)
{
    // Process CMD data in the cmd_evbuffer

    if (cmd_tunnel_state == CMDGW_TUNNEL_SCAN4CRLF)
    {
        bool ok=false;
        do
        {
            ok = CmdGwReadLine(cmdsock);
            if (ok && cmd_tunnel_state == CMDGW_TUNNEL_READTUNNEL)
                break;
        } while (ok);
    }
    // Not else!
    if (cmd_tunnel_state == CMDGW_TUNNEL_READTUNNEL)
    {
        // Got "TUNNELSEND addr size\r\n" command, now read
        // size bytes, i.e., cmd_tunnel_expect bytes.

        if (cmd_gw_debug)
            fprintf(stderr,"cmdgw: procTCPdata: tunnel state, got " PRISIZET ", want %d\n", evbuffer_get_length(cmd_evbuffer), cmd_tunnel_expect );

        if (evbuffer_get_length(cmd_evbuffer) >= cmd_tunnel_expect)
        {
            // We have all the tunneled data
            CmdGwTunnelSendUDP(cmd_evbuffer);

            // Process any remaining commands that came after the tunneled data
            CmdGwProcessData(cmdsock);
        }
    }
}


bool CmdGwReadLine(evutil_socket_t cmdsock)
{
    // Parse cmd_evbuffer for lines, and call NewRequest when found

    size_t rd=0;
    char *cmd = evbuffer_readln(cmd_evbuffer,&rd, EVBUFFER_EOL_CRLF_STRICT);
    if (cmd != NULL)
    {
        CmdGwNewRequestCallback(cmdsock,cmd);
        free(cmd);
        return true;
    }
    else
        return false;
}

int CmdGwHandleCommand(evutil_socket_t cmdsock, char *copyline);


void CmdGwNewRequestCallback(evutil_socket_t cmdsock, char *line)
{
    // New command received from user

    // CMD request line
    char *copyline = new char[strlen(line)+1];
    strcpy(copyline,line);

    int ret = CmdGwHandleCommand(cmdsock,copyline);
    if (ret < 0) {
        dprintf("cmd: Error processing command %s\n", line );
        std::string msg = "";
        if (ret == ERROR_UNKNOWN_CMD)
            msg = "unknown command";
	else if (ret == ERROR_MISS_ARG)
            msg = "missing parameter";
	else if (ret == ERROR_BAD_ARG)
	    msg = "bad parameter";
	// BAD_SWARM already sent, and not fatal

	if (msg != "")
	{
	    CmdGwSendERRORBySocket(cmdsock,msg);
	    CmdGwCloseConnection(cmdsock);
	}
    }

    delete copyline;
}





int CmdGwHandleCommand(evutil_socket_t cmdsock, char *copyline)
{
    char *method=NULL,*paramstr = NULL;
    char * token = strchr(copyline,' '); // split into CMD PARAM
    if (token != NULL) {
        *token = '\0';
        paramstr = token+1;
    }
    else
        paramstr = (char*)"";

    method = copyline;

    if (cmd_gw_debug)
        fprintf(stderr,"cmd: GOT %s %s\n", method, paramstr);

    char *savetok = NULL;
    if (!strcmp(method,"START"))
    {
        // New START request
        //fprintf(stderr,"cmd: START: new request %i\n",cmd_gw_reqs_count+1);

        // Format: START url destdir [metadir]\r\n
        // Arno, 2012-04-13: See if URL followed by storagepath, and metadir for seeding
        std::string pstr = paramstr;
        std::string url="",storagepath="", metadir="";
        int sidx = pstr.find(" ");
        if (sidx == std::string::npos)
        {
            // No storage path or metadir
            url = pstr;
            storagepath = "";
        }
        else
        {
            url = pstr.substr(0,sidx);
            std::string qstr = pstr.substr(sidx+1);
            sidx = qstr.find(" ");
            if (sidx == std::string::npos)
            {
                // Only storage path
                storagepath = qstr;
            }
            else
            {
                // Storage path and metadir
                storagepath = qstr.substr(0,sidx);
                metadir = qstr.substr(sidx+1);
            }
        }

        // If no metadir in command, but one is set on swift command line use latter.
        if (cmd_gw_metadir.compare("") && !metadir.compare(""))
            metadir = cmd_gw_metadir;
        if (metadir.length() > 0)
        {
            if (metadir.substr(metadir.length()-std::string(FILE_SEP).length()).compare(FILE_SEP))
                metadir += FILE_SEP;
        }

        // Parse URL
        parseduri_t puri;
        if (!swift::ParseURI(url,puri))
        {
            dprintf("cmd: START: cannot parse uri %s\n", url.c_str() );
            return ERROR_BAD_ARG;
        }

        SwarmMeta sm;
        // Set configured defaults for CMDGW
        sm.cont_int_prot_ = cmd_gw_cipm;
        sm.live_disc_wnd_ = cmd_gw_livesource_disc_wnd;
        // Convert parsed URI to config values
        std::string errorstr = URIToSwarmMeta(puri,&sm);
        if (errorstr != "")
        {
            dprintf("cmd: START: Error parsing URI: %s\n", errorstr.c_str() );
            return ERROR_BAD_ARG;
        }

        std::string trackerstr = puri["server"];
        std::string swarmidhexstr = puri["swarmidhex"];
        std::string mfstr = puri["filename"];
        std::string bttrackerurl = puri["et"];
        std::string durstr = puri["cd"];

        dprintf("cmd: START: %s with tracker %s chunksize %i duration %d storage %s metadir %s\n",swarmidhexstr.c_str(),trackerstr.c_str(),sm.chunk_size_,sm.cont_dur_,storagepath.c_str(),metadir.c_str());

        // Handle tracker
        // External tracker via URL param
        std::string trackerurl = "";
        if (trackerstr == "" && bttrackerurl == "")
        {
            trackerstr = Channel::trackerurl;
            if (trackerstr == "")
            {
                dprintf("cmd: START: tracker address must be URL server as hostname:port or ip:port, or set via ?bt=\n");
                return ERROR_BAD_ARG;
            }
        }
        // not else
        if (bttrackerurl == "")
        {
            trackerurl = SWIFT_URI_SCHEME;
            trackerurl += "://";
            trackerurl += trackerstr;
        }


        // initiate transmission
        SwarmID swarmid(swarmidhexstr);

        // Arno, 2012-06-12: Check for duplicate requests
        cmd_gw_t* req = CmdGwFindRequestBySwarmID(swarmid);
        if (req != NULL)
        {
            dprintf("cmd: START: request for given root hash already exists\n");
            return ERROR_BAD_ARG;
        }

        // Send INFO DLSTATUS_HASHCHECKING
        CmdGwSendINFOHashChecking(cmdsock,swarmid);

        // ARNOSMPTODO: disable/interleave hashchecking at startup

        // ARNOTODO: Allow for deactivated swarms. Needs cheap tracker registration
        bool activate=true;
        int td = swift::Find(swarmid,activate);
        if (td==-1) {
            std::string filename;
            if (storagepath != "")
                filename = storagepath;
            else
                filename = swarmidhexstr;

            if (durstr != "-1")
                td = swift::Open(filename,swarmid,trackerurl,false,sm.cont_int_prot_,false,activate,sm.chunk_size_,metadir);
            else
                td = swift::LiveOpen(filename,swarmid,trackerurl,sm.injector_addr_,sm.cont_int_prot_,sm.live_disc_wnd_,sm.chunk_size_);
            if (td == -1) {
            	CmdGwSendERRORBySocket(cmdsock,"bad swarm",swarmid);
            	return ERROR_BAD_SWARM;
            }
        }

        // RATELIMIT
        // swift::SetMaxSpeed(td,DDIR_DOWNLOAD,512*1024);

        // All is well, register req
        req = cmd_requests + cmd_gw_reqs_open++;
        req->id = ++cmd_gw_reqs_count;
        req->cmdsock = cmdsock;
        req->td = td;
        req->startt = usec_time();
        req->mfspecname = mfstr;
        req->playsent = false;
        req->xcontentdur = puri["cd"];
        req->contentlenstr = puri["cl"];
        req->mimetype = puri["mt"];

        dprintf("%s @%i start transfer %d\n",tintstr(),req->id,req->td);


        //if (cmd_gw_debug)
	//    fprintf(stderr,"cmd: Already on disk is %" PRIu64 "/%" PRIu64 "\n", swift::Complete(td), swift::Size(td));

        // Set progress callbacks
        if (swift::ttype(req->td) == FILE_TRANSFER)
        {
            // MULTIFILE
            uint64_t minsize=CMDGW_MAX_PREBUF_BYTES;

            Storage *storage = swift::GetStorage(req->td);
            if (storage == NULL)
            {
                dprintf("cmd: START: cannot get storage td %d\n", req->td );
                CmdGwSendERRORBySocket(cmdsock,"bad swarm",swarmid);
                return ERROR_BAD_SWARM;
            }
            storage_files_t sfs = storage->GetStorageFiles();
            if (sfs.size() > 0)
                minsize = sfs[0]->GetSize();
            else if (swift::SeqComplete(td) > 0) // Arno, 2013-01-08: Support small files
                minsize = std::min(swift::Size(td),minsize);

            // Wait for first chunk, so we can handle MULTIFILE, then
            // wait for prebuffering and then send PLAY to user.
            // ARNOSMPTODO: OUTOFORDER: breaks with out-of-order download

            if (swift::SeqComplete(td) >= minsize)
            {
                CmdGwSwiftVODFirstProgressCallback(td,bin_t(0,0));
                CmdGwSendINFO(req, DLSTATUS_DOWNLOADING);
            }
            else
            {
                swift::AddProgressCallback(td,&CmdGwSwiftVODFirstProgressCallback,CMDGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER);
            }

            storage->AddOneTimeAllocationCallback(CmdGwSwiftAllocatingDiskspaceCallback);
        }
        else
        {
            // LIVE
            // Wait for prebuffering and then send PLAY to user
            swift::AddProgressCallback(td,&CmdGwSwiftPrebufferProgressCallback,CMDGW_PREBUF_PROGRESS_BYTE_INTERVAL_AS_LAYER);
        }
    }
    else if (!strcmp(method,"REMOVE"))
    {
        // REMOVE roothash removestate removecontent\r\n
        bool removestate = false, removecontent = false;

        token = strtok_r(paramstr," ",&savetok); //
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *swarmidhexcstr = token;
        token = strtok_r(NULL," ",&savetok);      // removestate
        if (token == NULL)
            return ERROR_MISS_ARG;
        removestate = !strcmp(token,"1");
        token = strtok_r(NULL,"",&savetok);       // removecontent
        if (token == NULL)
            return ERROR_MISS_ARG;
        removecontent = !strcmp(token,"1");

        std::string swarmidhexstr(swarmidhexcstr);
        SwarmID swarmid(swarmidhexstr);
        CmdGwGotREMOVE(swarmid,removestate,removecontent);
    }
    else if (!strcmp(method,"MAXSPEED"))
    {
        // MAXSPEED roothash direction speed-float-kb/s\r\n
        data_direction_t ddir;
        double speed;

        token = strtok_r(paramstr," ",&savetok); //
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *swarmidhexcstr = token;
        token = strtok_r(NULL," ",&savetok);      // direction
        if (token == NULL)
            return ERROR_MISS_ARG;
        ddir = !strcmp(token,"DOWNLOAD") ? DDIR_DOWNLOAD : DDIR_UPLOAD;
        token = strtok_r(NULL,"",&savetok);       // speed
        if (token == NULL)
            return ERROR_MISS_ARG;
        int n = sscanf(token,"%lf",&speed);
        if (n == 0) {
            dprintf("cmd: MAXSPEED: speed is not a float\n");
            return ERROR_MISS_ARG;
        }
        std::string swarmidhexstr(swarmidhexcstr);
        SwarmID swarmid(swarmidhexstr);
        CmdGwGotMAXSPEED(swarmid,ddir,speed*1024.0);
    }
    else if (!strcmp(method,"CHECKPOINT"))
    {
        // CHECKPOINT roothash\r\n
        char *swarmidhexcstr = paramstr;
        std::string swarmidhexstr(swarmidhexcstr);
        SwarmID swarmid(swarmidhexstr);
        CmdGwGotCHECKPOINT(swarmid);
    }
    else if (!strcmp(method,"SETMOREINFO"))
    {
        // GETMOREINFO roothash toggle\r\n
        token = strtok_r(paramstr," ",&savetok); // hash
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *swarmidhexcstr = token;
        token = strtok_r(NULL," ",&savetok);      // bool
        if (token == NULL)
            return ERROR_MISS_ARG;
        bool enable = (bool)!strcmp(token,"1");

        std::string swarmidhexstr(swarmidhexcstr);
        SwarmID swarmid(swarmidhexstr);
        CmdGwGotSETMOREINFO(swarmid,enable);
    }
    else if (!strcmp(method,"SHUTDOWN"))
    {
        CmdGwCloseConnection(cmdsock);
        // Tell libevent to stop processing events
        event_base_loopexit(Channel::evbase, NULL);
    }
    else if (!strcmp(method,"TUNNELSUBSCRIBE"))
    {
        uint32_t channel;
        int n = sscanf(paramstr,"%08x",&channel);
        if (n != 1)
            return ERROR_BAD_ARG;
        if (!channel)
            return ERROR_BAD_ARG;
        // check if the channel is allowed (doesn't interfere with normal swift)
        if (channel < SWIFT_MAX_INCOMING_CONNECTIONS + SWIFT_MAX_OUTGOING_CONNECTIONS)
            return ERROR_BAD_ARG;
        // check if the channel is already reserved
        if (CmdGwTunnelCheckChannel(channel))
            return ERROR_BAD_ARG;

        tunnel_channels_.push_back(channel);
        if (cmd_gw_debug)
            fprintf(stderr,"cmdgw: Subscribed %" PRIu32 " as a new tunneling channel\n", channel );
    }
    else if (!strcmp(method,"TUNNELUNSUBSCRIBE"))
    {
        uint32_t channel;
        int n = sscanf(paramstr,"%08x",&channel);
        if (n != 1)
            return ERROR_BAD_ARG;
        if (!channel)
            return ERROR_BAD_ARG;
        // check if the channel actually exists
        if (CmdGwTunnelCheckChannel(channel)) {
            tunnel_channels_.erase(std::remove(tunnel_channels_.begin(), tunnel_channels_.end(), channel), tunnel_channels_.end());
            if (cmd_gw_debug)
                fprintf(stderr,"cmdgw: Unsubscribed channel %" PRIu32 "\n", channel );
        }
        else if (cmd_gw_debug)
            fprintf(stderr,"cmdgw: Unsubscribing channel %" PRIu32 " failed: No such channel subscribed\n", channel );
    }
    else if (!strcmp(method,"TUNNELSEND"))
    {
        token = strtok_r(paramstr,"/",&savetok); // dest addr
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *addrstr = token;
        token = strtok_r(NULL," ",&savetok);      // channel
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *chanstr = token;
        token = strtok_r(NULL," ",&savetok);      // size
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *sizestr = token;

        cmd_tunnel_dest_addr = Address(addrstr);
        int n = sscanf(chanstr,"%08x",&cmd_tunnel_dest_chanid);
        if (n != 1)
            return ERROR_BAD_ARG;
        n = sscanf(sizestr,"%" PRIu32 "",&cmd_tunnel_expect);
        if (n != 1)
            return ERROR_BAD_ARG;

        cmd_tunnel_state = CMDGW_TUNNEL_READTUNNEL;

        if (cmd_gw_debug)
            fprintf(stderr,"cmdgw: Want tunnel %d bytes to %s\n", cmd_tunnel_expect, cmd_tunnel_dest_addr.str().c_str() );
    }
    else if (!strcmp(method,"PEERADDR"))
    {
        // PEERADDR roothash addrstr\r\n
        token = strtok_r(paramstr," ",&savetok); // hash
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *swarmidhexcstr = token;
        token = strtok_r(NULL," ",&savetok);      // bool
        if (token == NULL)
            return ERROR_MISS_ARG;
        char *addrstr = token;

        Address peer(addrstr);
        std::string swarmidhexstr(swarmidhexcstr);
        SwarmID swarmid(swarmidhexstr);
        CmdGwGotPEERADDR(swarmid,peer);
    }
    else
    {
        return ERROR_UNKNOWN_CMD;
    }

    return ERROR_NO_ERROR;
}



void CmdGwEventCameInCallback(struct bufferevent *bev, short events, void *ctx)
{
    if (events & BEV_EVENT_ERROR)
        print_error("cmdgw: Error from bufferevent");
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
    {
        // Called when error on cmd connection
        evutil_socket_t cmdsock = bufferevent_getfd(bev);
        CmdGwCloseConnection(cmdsock);
        bufferevent_free(bev);
    }
}


void CmdGwNewConnectionCallback(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    // New TCP connection on cmd listen socket

    fprintf(stderr,"cmd: Got new cmd connection %i\n",fd);

    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(bev, CmdGwDataCameInCallback, NULL, CmdGwEventCameInCallback, NULL);
    bufferevent_enable(bev, EV_READ|EV_WRITE);

    // One buffer for all cmd connections, reset
    if (cmd_evbuffer != NULL)
        evbuffer_free(cmd_evbuffer);
    cmd_evbuffer = evbuffer_new();

    // SOCKTUNNEL: assume 1 command connection
    cmd_tunnel_sock = fd;

    cmd_gw_conns_open++;
}


void CmdGwListenErrorCallback(struct evconnlistener *listener, void *ctx)
{
    // libevent got error on cmd listener

    fprintf(stderr,"CmdGwListenErrorCallback: Something wrong with CMDGW\n" );
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    char errmsg[1024];
    sprintf(errmsg, "cmdgw: Got a fatal error %d (%s) on the listener.\n", err, evutil_socket_error_to_string(err));

    print_error(errmsg);
    dprintf("%s @0 closed cmd gateway\n",tintstr());

    evconnlistener_free(cmd_evlistener);
}


bool InstallCmdGateway (struct event_base *evbase,Address cmdaddr,Address httpaddr,popt_cont_int_prot_t cipm, uint64_t disc_wnd, std::string metadir)
{
    // Allocate libevent listener for cmd connections
    // From http://www.wangafu.net/~nickm/libevent-book/Ref8_listener.html

    fprintf(stderr,"cmdgw: Creating new TCP listener on addr %s\n", cmdaddr.str().c_str() );
  
    const struct sockaddr_storage sin = (sockaddr_storage)cmdaddr;

    cmd_evlistener = evconnlistener_new_bind(evbase, CmdGwNewConnectionCallback, NULL,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
        (const struct sockaddr *)&sin, cmdaddr.get_family_sockaddr_length());
    if (!cmd_evlistener) {
        print_error("Couldn't create listener");
        return false;
    }
    evconnlistener_set_error_cb(cmd_evlistener, CmdGwListenErrorCallback);

    cmd_gw_httpaddr = httpaddr;
    cmd_gw_cipm=cipm;
    cmd_gw_livesource_disc_wnd = disc_wnd;
    cmd_gw_metadir = metadir;

    cmd_evbuffer = evbuffer_new();

    return true;
}



// SOCKTUNNEL
bool swift::CmdGwTunnelCheckChannel(uint32_t channel) {
    // returns true is the channel is used for tunneling messages through channels
    for (std::vector<uint32_t>::iterator it = tunnel_channels_.begin(); it != tunnel_channels_.end(); ++it)
        if (*it == channel)
            return true;
    return false;
}


void swift::CmdGwTunnelUDPDataCameIn(Address srcaddr, uint32_t srcchan, struct evbuffer* evb)
{
    // Message received on UDP socket, forward over TCP conn.

    if (cmd_gw_debug)
        fprintf(stderr,"cmdgw: TunnelUDPData:DataCameIn " PRISIZET " bytes from %s/%08x\n", evbuffer_get_length(evb), srcaddr.str().c_str(), srcchan );

    /*
     *  Format:
     *  TUNNELRECV ip:port/hexchanid nbytes\r\n
     *  <bytes>
     */

    std::ostringstream oss;
    oss << "TUNNELRECV " << srcaddr.str();
    oss << "/" << std::hex << srcchan;
    oss << " " << std::dec << evbuffer_get_length(evb) << "\r\n";

    std::stringbuf *pbuf=oss.rdbuf();
    size_t slen = strlen(pbuf->str().c_str());
    send(cmd_tunnel_sock,pbuf->str().c_str(),slen,0);

    slen = evbuffer_get_length(evb);
    uint8_t *data = evbuffer_pullup(evb,slen);
    send(cmd_tunnel_sock,(const char *)data,slen,0);

    evbuffer_drain(evb,slen);
}


void swift::CmdGwTunnelSendUDP(struct evbuffer *evb)
{
    // Received data from TCP connection, send over UDP to specified dest
    cmd_tunnel_state = CMDGW_TUNNEL_SCAN4CRLF;

    if (cmd_gw_debug)
        fprintf(stderr,"cmdgw: sendudp:");

    struct evbuffer *sendevbuf = evbuffer_new();

    // Add channel id. Currently always CMDGW_TUNNEL_DEFAULT_CHANNEL_ID=0xffffffff
    // but we may add a TUNNELSUBSCRIBE command later to allow the allocation
    // of different channels for different TCP clients.
    int ret = evbuffer_add_32be(sendevbuf, cmd_tunnel_dest_chanid);
    if (ret < 0)
    {
        evbuffer_drain(evb,cmd_tunnel_expect);
        evbuffer_free(sendevbuf);
        fprintf(stderr,"cmdgw: sendudp :can't copy prefix to sendbuf!");
        return;
    }
    ret = evbuffer_remove_buffer(evb, sendevbuf, cmd_tunnel_expect);
    if (ret < 0)
    {
        evbuffer_drain(evb,cmd_tunnel_expect);
        evbuffer_free(sendevbuf);
        fprintf(stderr,"cmdgw: sendudp :can't copy to sendbuf!");
        return;
    }
    if (Channel::sock_count != 1)
    {
        fprintf(stderr,"cmdgw: sendudp: no single UDP socket!");
        evbuffer_free(sendevbuf);
        return;
    }
    evutil_socket_t sock = Channel::sock_open[Channel::sock_count-1].sock;

    Channel::SendTo(sock,cmd_tunnel_dest_addr,sendevbuf);

    evbuffer_free(sendevbuf);
}
