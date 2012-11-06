/*
 *  httpgw.cpp
 *  gateway for serving swift content via HTTP, libevent2 based.
 *
 *  Created by Victor Grishchenko, Arno Bakker
 *  Copyright 2010-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include "swift.h"
#include <event2/http.h>
#include <event2/bufferevent.h>
#include <sstream>



using namespace swift;

static uint32_t HTTPGW_VOD_PROGRESS_STEP_BYTES = (256*1024); // configurable
// For best performance make bigger than HTTPGW_PROGRESS_STEP_BYTES
#define HTTPGW_VOD_MAX_WRITE_BYTES	(512*1024)


#define HTTPGW_LIVE_PROGRESS_STEP_BYTES	(16*1024)
// For best performance make bigger than HTTPGW_PROGRESS_STEP_BYTES
#define HTTPGW_LIVE_MAX_WRITE_BYTES	(32*1024)


// Report swift download progress every 2^layer * chunksize bytes (so 0 = report every chunk)
// Note: for LIVE this cannot be reliably used to force a prebuffer size,
// as this gets called with a subtree of size X has been downloaded. If the
// hook-in point is in the middle of such a subtree, the call won't happen
// until the next full subtree has been downloaded, i.e. after ~1.5 times the
// prebuf has been downloaded. See HTTPGW_MIN_PREBUF_BYTES
//
#define HTTPGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER     0    // must be 0

// Arno: libevent2 has a liberal understanding of socket writability,
// that may result in tens of megabytes being cached in memory. Limit that
// amount at app level.
#define HTTPGW_MAX_OUTBUF_BYTES		(2*1024*1024)

// Arno: Minium amout of content to have download before replying to HTTP
static uint32_t HTTPGW_MIN_PREBUF_BYTES  = (256*1024); // configurable


#define HTTPGW_MAX_REQUEST 128

struct http_gw_t {
    int      id;
    uint64_t offset;
    uint64_t tosend;
    int      td;
    uint64_t lastcpoffset; // last offset at which we checkpointed
    struct evhttp_request *sinkevreq;
    struct event           *sinkevwrite;
    std::string    mfspecname; // (optional) name from multi-file spec
    std::string xcontentdur;
    std::string mimetype;
    bool     replied;
    bool     closing;
    uint64_t startoff;   // MULTIFILE: starting offset in content range of desired file
    uint64_t endoff;     // MULTIFILE: ending offset (careful, for an e.g. 100 byte interval this is 99)
    int replycode;         // HTTP status code
    int64_t  rangefirst; // First byte wanted in HTTP GET Range request or -1
    int64_t  rangelast;  // Last byte wanted in HTTP GET Range request (also 99 for 100 byte interval) or -1
    bool     foundH264NALU;

} http_requests[HTTPGW_MAX_REQUEST];


int http_gw_reqs_open = 0;
int http_gw_reqs_count = 0;
struct evhttp *http_gw_event;
struct evhttp_bound_socket *http_gw_handle;
uint32_t httpgw_chunk_size = SWIFT_DEFAULT_CHUNK_SIZE; // Copy of cmdline param
double *httpgw_maxspeed = NULL;                         // Copy of cmdline param
std::string httpgw_storage_dir="";
Address httpgw_bindaddr;

// Arno, 2010-11-30: for SwarmPlayer 3000 backend autoquit when no HTTP req is received
bool sawhttpconn = false;


http_gw_t *HttpGwFindRequestByEV(struct evhttp_request *evreq) {
    for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
        if (http_requests[httpc].sinkevreq==evreq)
            return &http_requests[httpc];
    }
    return NULL;
}

http_gw_t *HttpGwFindRequestByTD(int td) {
    for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
        if (http_requests[httpc].td==td) {
            return &http_requests[httpc];
        }
    }
    return NULL;
}

http_gw_t *HttpGwFindRequestBySwarmID(Sha1Hash &wanthash) {
    int td = swift::Find(wanthash);
    if (td < 0)
        return NULL;
    return HttpGwFindRequestByTD(td);
}


void HttpGwCloseConnection (http_gw_t* req) {
    dprintf("%s @%i http get: cleanup evreq %p\n",tintstr(),req->id, req->sinkevreq);

    struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);

    req->closing = true;

    if (!req->replied)
        evhttp_request_free(req->sinkevreq);
    else if (req->offset > req->startoff)
        evhttp_send_reply_end(req->sinkevreq); //WARNING: calls HttpGwLibeventCloseCallback
    req->sinkevreq = NULL;

    if (req->sinkevwrite != NULL)
    {
        event_free(req->sinkevwrite);
        req->sinkevwrite = NULL;
    }

    // Note: for some reason calling conn_free here prevents the last chunks
    // to be sent to the requester?
    //    evhttp_connection_free(evconn); // WARNING: calls HttpGwLibeventCloseCallback

    // Current close policy: checkpoint and DO NOT close transfer, keep on
    // seeding forever. More sophisticated clients should use CMD GW and issue
    // REMOVE.
    swift::Checkpoint(req->td);

    // Arno, 2012-05-04: MULTIFILE: once the selected file has been downloaded
    // swift will download all content that comes afterwards too. Poor man's
    // fix to avoid this: seek to end of content when HTTP done. VOD PiecePicker
    // will then no download anything. Better would be to seek to end when
    // swift partial download is done, not the serving via HTTP.
    //
    swift::Seek(req->td,swift::Size(req->td)-1,SEEK_CUR);

    //swift::Close(req->td);

    *req = http_requests[--http_gw_reqs_open];
}


void HttpGwLibeventCloseCallback(struct evhttp_connection *evconn, void *evreqvoid) {
    // Called by libevent on connection close, either when the other side closes
    // or when we close (because we call evhttp_connection_free()). To prevent
    // doing cleanup twice, we see if there is a http_gw_req that has the
    // passed evreqvoid as sinkevreq. If so, clean up, if not, ignore.
    // I.e. evhttp_request * is used as sort of request ID
    //
    fprintf(stderr,"HttpGwLibeventCloseCallback: called\n");
    http_gw_t * req = HttpGwFindRequestByEV((struct evhttp_request *)evreqvoid);
    if (req == NULL)
        dprintf("%s @-1 http closecb: conn already closed\n",tintstr() );
    else {
        dprintf("%s @%i http closecb\n",tintstr(),req->id);
        if (req->closing)
            dprintf("%s @%i http closecb: already closing\n",tintstr(), req->id);
        else
            HttpGwCloseConnection(req);
    }
}




void HttpGwWrite(int td) {

    //
    // Write to HTTP socket.
    //

    http_gw_t* req = HttpGwFindRequestByTD(td);
    if (req == NULL) {
        print_error("httpgw: MayWrite: can't find req for transfer");
        return;
    }

    // When writing first data, send reply header
    if (req->offset == req->startoff) {
        // Not just for chunked encoding, see libevent2's http.c

	dprintf("%s @%d http reply 2: %d\n",tintstr(),req->id, req->replycode );

        evhttp_send_reply_start(req->sinkevreq, req->replycode, "OK");
        req->replied = true;
    }

    // SEEKTODO: stop downloading when file complete

    // Update endoff as size becomes less fuzzy
    if (swift::Size(req->td) < req->endoff)
        req->endoff = swift::Size(req->td)-1;

    // How much can we write?
    uint64_t relcomplete = swift::SeqComplete(req->td,req->startoff);
    if (relcomplete > req->endoff)
        relcomplete = req->endoff+1-req->startoff;
    int64_t avail = relcomplete-(req->offset-req->startoff);

    dprintf("%s @%d http write: avail %lld relcomp %llu offset %llu start %llu end %llu tosend %llu\n",tintstr(),req->id, avail, relcomplete, req->offset, req->startoff, req->endoff, req->tosend );

    struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
    struct bufferevent* buffy = evhttp_connection_get_bufferevent(evconn);
    struct evbuffer *outbuf = bufferevent_get_output(buffy);

    // Arno: If sufficient data to write (avoid small increments) and out buffer
    // not filled then write to socket. Unfortunately, libevent2 has a liberal
    // understanding of socket writability that may result in tens of megabytes
    // being cached in memory. Limit that amount at app level.
    //
    if (avail > 0 && evbuffer_get_length(outbuf) < HTTPGW_MAX_OUTBUF_BYTES)
    {
        int max_write_bytes = 0;
        if (swift::ttype(req->td) == FILE_TRANSFER)
            max_write_bytes = HTTPGW_VOD_MAX_WRITE_BYTES;
        else
            max_write_bytes = HTTPGW_LIVE_MAX_WRITE_BYTES;

        // Allocate buffer to read into. TODO: let swift::Read accept evb
        char *buf = (char *)malloc(max_write_bytes);

// Arno, 2010-08-16, TODO compat
#ifdef WIN32
        uint64_t tosend = min(max_write_bytes,avail);
#else
        uint64_t tosend = std::min((int64_t)max_write_bytes,avail);
#endif
        size_t rd = swift::Read(req->td,buf,tosend,swift::GetHookinOffset(req->td)+req->offset);
        if (rd<0) {
            print_error("httpgw: MayWrite: error pread");
            HttpGwCloseConnection(req);
            free(buf);
            return;
        }

        // Construct evbuffer and send incrementally
        struct evbuffer *evb = evbuffer_new();
        int ret = 0;

        // ARNO LIVE raw H264 hack
        if (req->mimetype == "video/h264")
        {
	    if (req->offset == req->startoff)
	    {
		// Arno, 2012-10-24: When tuning into a live stream of raw H.264
		// you must
		// 1. Replay Sequence Picture Set (SPS) and Picture Parameter Set (PPS)
		// 2. Find first NALU in video stream (starts with 00 00 00 01 and next bit is 0
		// 3. Write that first NALU
		//
		// PROBLEM is that SPS and PPS contain info on video size, frame rate,
		// and stuff, so is stream specific. The hardcoded values here are
		// for H.264 640x480 15 fps 500000 bits/s obtained via Spydroid.
		//

		const char h264sps[] = { 0x00, 0x00, 0x00, 0x01, 0x27, 0x42, 0x80, 0x29, 0x8D, 0x95, 0x01, 0x40, 0x7B, 0x20 };
		const char h264pps[] = { 0x00, 0x00, 0x00, 0x01, 0x28, 0xDE, 0x09, 0x88 };

		dprintf("%s @%i http write: adding H.264 SPS and PPS\n",tintstr(),req->id );

		ret = evbuffer_add(evb,h264sps,sizeof(h264sps));
		if (ret < 0)
		    print_error("httpgw: MayWrite: error evbuffer_add H.264 SPS");
		ret = evbuffer_add(evb,h264pps,sizeof(h264pps));
		if (ret < 0)
		    print_error("httpgw: MayWrite: error evbuffer_add H.264 PPS");
	    }
        }
	else
	    req->foundH264NALU = true; // Other MIME type

        // Find first H.264 NALU
        size_t naluoffset = 0;
        if (!req->foundH264NALU && rd >= 5)
        {
            for (int i=0; i<rd-5; i++)
            {
        	// Find startcode before NALU
        	if (buf[i] == '\x00' && buf[i+1] == '\x00' && buf[i+2] == '\x00' && buf[i+3] == '\x01')
        	{
        	    char naluhead = buf[i+4];
        	    if ((naluhead & 0x80) == 0)
        	    {
        		// Found NALU
        		// http://mailman.videolan.org/pipermail/x264-devel/2007-February/002681.html
        		naluoffset = i;
        		req->foundH264NALU = true;
        		dprintf("%s @%i http write: Found H.264 NALU at %d\n",tintstr(),req->id, naluoffset );
        		break;
        	    }
        	}
            }
        }

        // Live tuned-in or VOD:
        if (req->foundH264NALU)
        {
	    // Arno, 2012-10-24: LIVE Don't change rd here, as that should be a multiple of chunks
	    ret = evbuffer_add(evb,buf+naluoffset,rd-naluoffset);
	    if (ret < 0) {
		print_error("httpgw: MayWrite: error evbuffer_add");
		evbuffer_free(evb);
		HttpGwCloseConnection(req);
		free(buf);
		return;
	    }
        }

        if (evbuffer_get_length(evb) > 0)
	    evhttp_send_reply_chunk(req->sinkevreq, evb);

        evbuffer_free(evb);
        free(buf);

        int wn = rd;
        dprintf("%s @%i http write: sent %db\n",tintstr(),req->id,wn);

        req->offset += wn;
        req->tosend -= wn;

        // PPPLUG
        swift::Seek(req->td,req->offset,SEEK_CUR);
    }

    // Arno, 2010-11-30: tosend is set to fuzzy len, so need extra/other test.
    if (req->tosend==0 || req->offset == req->endoff+1) {
        // Done; wait for outbuffer to empty
        dprintf("%s @%i http write: done, wait for buffer empty\n",tintstr(),req->id);
        if (evbuffer_get_length(outbuf) == 0) {
            dprintf("%s @%i http write: final done\n",tintstr(),req->id );
            HttpGwCloseConnection(req);
        }
    }
    else {
        // wait for data
        dprintf("%s @%i http write: waiting for data\n",tintstr(),req->id);
    }
}

void HttpGwSubscribeToWrite(http_gw_t * req);

void HttpGwLibeventMayWriteCallback(evutil_socket_t fd, short events, void *evreqvoid )
{
    //
    // HTTP socket is ready to be written to.
    //
    http_gw_t * req = HttpGwFindRequestByEV((struct evhttp_request *)evreqvoid);
    if (req == NULL)
        return;

    HttpGwWrite(req->td);

    if (swift::ttype(req->td) == FILE_TRANSFER) {

        if (swift::Complete(req->td)+HTTPGW_VOD_MAX_WRITE_BYTES >= swift::Size(req->td)) {

            // We don't get progress callback for last chunk < chunk size, nor
            // when all data is already on disk. In that case, just keep on
            // subscribing to HTTP socket writability until all data is sent.
            //
            if (req->sinkevreq != NULL) // Conn closed
                HttpGwSubscribeToWrite(req);
        }
    }
}


void HttpGwSubscribeToWrite(http_gw_t * req) {
    //
    // Subscribing to writability of the socket requires libevent2 >= 2.0.17
    // (or our backported version)
    //
    struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
    struct event_base *evbase =    evhttp_connection_get_base(evconn);
    struct bufferevent* evbufev = evhttp_connection_get_bufferevent(evconn);

    if (req->sinkevwrite != NULL)
        event_free(req->sinkevwrite); // does event_del()

    req->sinkevwrite = event_new(evbase,bufferevent_getfd(evbufev),EV_WRITE,HttpGwLibeventMayWriteCallback,req->sinkevreq);
    struct timeval t;
    t.tv_sec = 10;
    int ret = event_add(req->sinkevwrite,&t);
    //fprintf(stderr,"httpgw: HttpGwSubscribeToWrite: added event\n");
}



void HttpGwSwiftPlayingProgressCallback (int td, bin_t bin) {

    // Ready to play or playing, and subsequent HTTPGW_PROGRESS_STEP_BYTES
    // available. So subscribe to a callback when HTTP socket becomes writable
    // to write it out.

    dprintf("%s T%i http play progress\n",tintstr(),td);
    http_gw_t* req = HttpGwFindRequestByTD(td);
    if (req == NULL)
        return;

    if (req->sinkevreq == NULL) // Conn closed
        return;

    // Arno, 2011-12-20: We have new data to send, wait for HTTP socket writability
    HttpGwSubscribeToWrite(req);
}


void HttpGwSwiftPrebufferProgressCallback (int td, bin_t bin) {
    //
    // Prebuffering, and subsequent bytes of content downloaded.
    //
    // If sufficient prebuffer, next step is to subscribe to a callback for
    // writing out the reply and body.
    //
    dprintf("%s T%i http prebuf progress\n",tintstr(),td);

    http_gw_t* req = HttpGwFindRequestByTD(td);
    if (req == NULL)
    {
        dprintf("%s T%i http prebuf progress: req not found\n",tintstr(),td);
        return;
    }

    // ARNOSMPTODO: bitrate-dependent prebuffering?

    dprintf("%s T%i http prebuf progress: endoff startoff %llu endoff %llu\n",tintstr(),td, req->startoff, req->endoff);

#ifdef WIN32
    int64_t wantsize = min(req->endoff+1-req->startoff,HTTPGW_MIN_PREBUF_BYTES);
#else
    int64_t wantsize = std::min(req->endoff+1-req->startoff,(uint64_t)HTTPGW_MIN_PREBUF_BYTES);
#endif

    dprintf("%s T%i http prebuf progress: want %lld got %lld\n",tintstr(),td, wantsize, swift::SeqComplete(req->td,req->startoff) );

    if (swift::SeqComplete(req->td,req->startoff) < wantsize)
    {
        // wait for more data
        return;

    }

    // First HTTPGW_MIN_PREBUF_BYTES bytes of request received.
    swift::RemoveProgressCallback(td,&HttpGwSwiftPrebufferProgressCallback);

    int stepbytes = 0;
    if (swift::ttype(td) == FILE_TRANSFER)
        stepbytes = HTTPGW_VOD_PROGRESS_STEP_BYTES;
    else
        stepbytes = HTTPGW_LIVE_PROGRESS_STEP_BYTES;
    int progresslayer = bytes2layer(stepbytes,swift::ChunkSize(td));
    swift::AddProgressCallback(td,&HttpGwSwiftPlayingProgressCallback,progresslayer);

    //
    // We have sufficient data now, see if HTTP socket is writable so we can
    // write the HTTP reply and first part of body.
    //
    HttpGwSwiftPlayingProgressCallback(td,bin_t(0,0));
}




bool HttpGwParseContentRangeHeader(http_gw_t *req,uint64_t filesize)
{
    struct evkeyvalq *reqheaders =    evhttp_request_get_input_headers(req->sinkevreq);
    struct evkeyvalq *repheaders = evhttp_request_get_output_headers(req->sinkevreq);
    const char *contentrangecstr =evhttp_find_header(reqheaders,"Range");

    if (contentrangecstr == NULL) {
        req->rangefirst = -1;
        req->rangelast = -1;
        req->replycode = 200;
        return true;
    }
    std::string range = contentrangecstr;

    // Handle RANGE query
    bool bad = false;
    int idx = range.find("=");
    if (idx == std::string::npos)
        return false;
    std::string seek = range.substr(idx+1);

    dprintf("%s @%i http get: range request spec %s\n",tintstr(),req->id, seek.c_str() );

    if (seek.find(",") != std::string::npos) {
        // - Range header contains set, not supported at the moment
        bad = true;
    } else     {
        // Determine first and last bytes of requested range
        idx = seek.find("-");

        dprintf("%s @%i http get: range request idx %d\n", tintstr(),req->id, idx );

        if (idx == std::string::npos)
            return false;
        if (idx == 0) {
            // -444 format
            req->rangefirst = -1;
        } else {
            std::istringstream(seek.substr(0,idx)) >> req->rangefirst;
        }


        dprintf("%s @%i http get: range request first %s %lld\n", tintstr(),req->id, seek.substr(0,idx).c_str(), req->rangefirst );

        if (idx == seek.length()-1)
            req->rangelast = -1;
        else {
            // 444- format
            std::istringstream(seek.substr(idx+1)) >> req->rangelast;
        }

        dprintf("%s @%i http get: range request last %s %lld\n", tintstr(),req->id, seek.substr(idx+1).c_str(), req->rangelast );

        // Check sanity of range request
        if (filesize == -1) {
            // - No length (live)
            bad = true;
        }
        else if (req->rangefirst == -1 && req->rangelast == -1) {
            // - Invalid input
            bad = true;
        }
        else if (req->rangefirst >= (int64_t)filesize) {
            bad = true;
        }
        else if (req->rangelast >= (int64_t)filesize) {
            if (req->rangefirst == -1)     {
                // If the entity is shorter than the specified
                // suffix-length, the entire entity-body is used.
                req->rangelast = filesize-1;
            }
            else
                bad = true;
        }
    }

    if (bad) {
        // Send 416 - Requested Range not satisfiable
        std::ostringstream cross;
        if (filesize == -1)
            cross << "bytes */*";
        else
            cross << "bytes */" << filesize;
        evhttp_add_header(repheaders, "Content-Range", cross.str().c_str() );

        evhttp_send_error(req->sinkevreq,416,"Malformed range specification");
	req->replied = true;

        dprintf("%s @%i http get: invalid range %lld-%lld\n",tintstr(),req->id,req->rangefirst,req->rangelast );
        return false;
    }

    // Convert wildcards into actual values
    if (req->rangefirst != -1 && req->rangelast == -1) {
        // "100-" : byte 100 and further
        req->rangelast = filesize - 1;
    }
    else if (req->rangefirst == -1 && req->rangelast != -1) {
        // "-100" = last 100 bytes
        req->rangefirst = filesize - req->rangelast;
        req->rangelast = filesize - 1;
    }

    // Generate header
    std::ostringstream cross;
    cross << "bytes " << req->rangefirst << "-" << req->rangelast << "/" << filesize;
    evhttp_add_header(repheaders, "Content-Range", cross.str().c_str() );

    // Reply is sent when content is avail
    req->replycode = 206;

    dprintf("%s @%i http get: valid range %lld-%lld\n",tintstr(),req->id,req->rangefirst,req->rangelast );

    return true;
}


void HttpGwFirstProgressCallback (int td, bin_t bin) {
    //
    // First bytes of content downloaded (first in absolute sense)
    // We can now determine if a request for a file inside a multi-file
    // swarm is valid, and calculate the absolute offsets of the request
    // content, taking into account HTTP Range: headers. Or return error.
    //
    // If valid, next step is to subscribe a new callback for prebuffering.
    //
    dprintf("%s T%i http first progress\n",tintstr(),td);

    // Need the first chunk
    if (swift::SeqComplete(td) == 0)
    {
        dprintf("%s T%i http first: not enough seqcomp\n",tintstr(),td );
        return;
    }

    http_gw_t* req = HttpGwFindRequestByTD(td);
    if (req == NULL)
    {
        dprintf("%s T%i http first: req not found\n",tintstr(),td);
        return;
    }

    // Protection against spurious callback
    if (req->tosend > 0)
    {
        dprintf("%s @%i http first: already set tosend\n",tintstr(),req->id);
        return;
    }

    if (req->xcontentdur == "-1")
    {
        fprintf(stderr,"httpgw: Live: hook-in at %llu\n", swift::GetHookinOffset(td) );
        dprintf("%s @%i http first: hook-in at %llu\n",tintstr(),req->id, swift::GetHookinOffset(td) );
    }

    // MULTIFILE
    // Is storage ready?
    Storage *storage = swift::GetStorage(td);
    if (storage == NULL)
    {
        dprintf("%s @%i http first: not storage object?\n",tintstr(),req->id);
        return;
    }
    if (!storage->IsReady())
    {
	dprintf("%s 2%i http first: Storage not ready, wait\n",tintstr(),req->id);
	return; // wait for some more data
    }

    /*
     * Good to go: Calculate info needed for header of HTTP reply, return
     * error if it doesn't make sense
     */
    uint64_t filesize = 0;
    if (req->mfspecname != "")
    {
        // MULTIFILE
        // Find out size of selected file
        storage_files_t sfs = storage->GetStorageFiles();
        storage_files_t::iterator iter;
        bool found = false;
        for (iter = sfs.begin(); iter < sfs.end(); iter++)
        {
            StorageFile *sf = *iter;
            if (sf->GetSpecPathName() == req->mfspecname)
            {
                found = true;
                req->startoff = sf->GetStart();
                req->endoff = sf->GetEnd();
                filesize = sf->GetSize();
                break;
            }
        }
        if (!found) {
            evhttp_send_error(req->sinkevreq,404,"Individual file not found in multi-file content.");
                        req->replied = true;
            return;
        }
    }
    else
    {
        // Single file
        req->startoff = 0;
        req->endoff = swift::Size(td)-1;
        filesize = swift::Size(td);
    }

    // Handle HTTP GET Range request, i.e. additional offset within content
    // or file. Sets some headers or sends HTTP error.
    if (req->xcontentdur == "-1") //LIVE
    {
        req->rangefirst = -1;
        req->replycode = 200;
    }
    else if (!HttpGwParseContentRangeHeader(req,filesize))
        return;

    if (req->rangefirst != -1)
    {
        // Range request
        // Arno, 2012-06-15: Oops, use startoff before mod.
        req->endoff = req->startoff + req->rangelast;
        req->startoff += req->rangefirst;
        req->tosend = req->rangelast+1-req->rangefirst;
    }
    else
    {
        req->tosend = filesize;
    }
    req->offset = req->startoff;

    // SEEKTODO: concurrent requests to same resource
    if (req->startoff != 0)
    {
        // Seek to multifile/range start
        int ret = swift::Seek(req->td,req->startoff,SEEK_SET);
        if (ret < 0) {
            evhttp_send_error(req->sinkevreq,500,"Internal error: Cannot seek to file start in range request or multi-file content.");
            req->replied = true;
            return;
        }
    }

    // Prepare rest of headers. Not actually sent till HttpGwWrite
    // calls evhttp_send_reply_start()
    //
    struct evkeyvalq *reqheaders = evhttp_request_get_output_headers(req->sinkevreq);
    //evhttp_add_header(reqheaders, "Connection", "keep-alive" );
    evhttp_add_header(reqheaders, "Connection", "close" );
    evhttp_add_header(reqheaders, "Content-Type", req->mimetype.c_str() );
    if (req->xcontentdur != "-1")
    {
        if (req->xcontentdur.length() > 0)
            evhttp_add_header(reqheaders, "X-Content-Duration", req->xcontentdur.c_str() );

        // Convert size to string
        std::ostringstream closs;
        closs << req->tosend;
        evhttp_add_header(reqheaders, "Content-Length", closs.str().c_str() );
    }
    else
    {
    	// LIVE
	// Uses chunked encoding, configured by not setting a Content-Length header
        evhttp_add_header(reqheaders, "Accept-Ranges", "none" );
    }

    dprintf("%s @%i http first: headers set, tosend %lli\n",tintstr(),req->id,req->tosend);


    // Reconfigure callbacks for prebuffering
    swift::RemoveProgressCallback(td,&HttpGwFirstProgressCallback);

    int stepbytes = 0;
    if (swift::ttype(td) == FILE_TRANSFER)
        stepbytes = HTTPGW_VOD_PROGRESS_STEP_BYTES;
    else
        stepbytes = HTTPGW_LIVE_PROGRESS_STEP_BYTES;
    int progresslayer = bytes2layer(stepbytes,swift::ChunkSize(td));
    swift::AddProgressCallback(td,&HttpGwSwiftPrebufferProgressCallback,progresslayer);

    // We have some data now, see if sufficient prebuffer to go play
    // (happens when some content already on disk, or fast DL
    //
    HttpGwSwiftPrebufferProgressCallback(td,bin_t(0,0));
}




bool swift::ParseURI(std::string uri,parseduri_t &map)
{
    //
    // Format: tswift://tracker:port/roothash-in-hex/filename$chunksize@duration
    // where the server part, filename, chunksize and duration may be optional
    //
    std::string scheme="";
    std::string server="";
    std::string path="";
    if (uri.substr(0,((std::string)SWIFT_URI_SCHEME).length()) == SWIFT_URI_SCHEME)
    {
        // scheme present
        scheme = SWIFT_URI_SCHEME;
        int sidx = uri.find("//");
        if (sidx != std::string::npos)
        {
            // server part present
            int eidx = uri.find("/",sidx+2);
            server = uri.substr(sidx+2,eidx-(sidx+2));
            path = uri.substr(eidx);
        }
        else
            path = uri.substr(((std::string)SWIFT_URI_SCHEME).length()+1);
    }
    else
        path = uri;


    std::string hashstr="";
    std::string filename="";
    std::string modstr="";

    int sidx = path.find("/",1);
    int midx = path.find("$",1);
    if (midx == std::string::npos)
        midx = path.find("@",1);
    if (sidx == std::string::npos && midx == std::string::npos) {
        // No multi-file, no modifiers
        hashstr = path.substr(1,path.length());
    } else if (sidx != std::string::npos && midx == std::string::npos) {
        // multi-file, no modifiers
        hashstr = path.substr(1,sidx-1);
        filename = path.substr(sidx+1,path.length()-sidx);
    } else if (sidx == std::string::npos && midx != std::string::npos) {
        // No multi-file, modifiers
        hashstr = path.substr(1,midx-1);
        modstr = path.substr(midx,path.length()-midx);
    } else {
        // multi-file, modifiers
        hashstr = path.substr(1,sidx-1);
        filename = path.substr(sidx+1,midx-(sidx+1));
        modstr = path.substr(midx,path.length()-midx);
    }


    std::string durstr="";
    std::string chunkstr="";
    sidx = modstr.find("@");
    if (sidx == std::string::npos)
    {
        durstr = "";
        if (modstr.length() > 1)
            chunkstr = modstr.substr(1);
    }
    else
    {
        if (sidx == 0)
        {
            // Only durstr
            chunkstr = "";
            durstr = modstr.substr(sidx+1);
        }
        else
        {
            chunkstr = modstr.substr(1,sidx-1);
            durstr = modstr.substr(sidx+1);
        }
    }

    map.insert(stringpair("scheme",scheme));
    map.insert(stringpair("server",server));
    map.insert(stringpair("path",path));
    // Derivatives
    map.insert(stringpair("hash",hashstr));
    map.insert(stringpair("filename",filename));
    map.insert(stringpair("chunksizestr",chunkstr));
    map.insert(stringpair("durationstr",durstr));

    return true;
}



void HttpGwNewRequestCallback (struct evhttp_request *evreq, void *arg) {

    dprintf("%s @%i http get: new request\n",tintstr(),http_gw_reqs_count+1);

    if (evhttp_request_get_command(evreq) != EVHTTP_REQ_GET) {
        return;
    }
    sawhttpconn = true;

    // 1. Get swift URI
    std::string uri = evhttp_request_get_uri(evreq);
    struct evkeyvalq *reqheaders = evhttp_request_get_input_headers(evreq);

    dprintf("%s @%i http get: new request %s\n",tintstr(),http_gw_reqs_count+1,uri.c_str() );

    // Arno, 2012-04-19: libevent adds "Connection: keep-alive" to reply headers
    // if there is one in the request headers, even if a different Connection
    // reply header has already been set. And we don't do persistent conns here.
    //
    evhttp_remove_header(reqheaders,"Connection"); // Remove Connection: keep-alive

    // 2. Parse swift URI
    std::string hashstr = "", mfstr="", durstr="", chunksizestr = "";
    if (uri.length() <= 1)     {
        evhttp_send_error(evreq,400,"Path must be root hash in hex, 40 bytes.");
        return;
    }
    parseduri_t puri;
    if (!swift::ParseURI(uri,puri))
    {
        evhttp_send_error(evreq,400,"Path format is /roothash-in-hex/filename$chunksize@duration");
        return;
    }
    hashstr = puri["hash"];
    mfstr = puri["filename"];
    durstr = puri["durationstr"];
    chunksizestr = puri["chunksizestr"];

    // Handle LIVE
    std::string mimetype = "video/mp2t";
    if (hashstr.substr(hashstr.length()-5) == ".h264")
    {
	// LIVESOURCE=ANDROID
	hashstr = hashstr.substr(0,40); // strip .h264
	durstr = "-1";
	mimetype = "video/h264";
    }
    else if (hashstr.length() > 40 && hashstr.substr(hashstr.length()-2) == "-1")
    {
	// Arno, 2012-06-15: LIVE: VLC can't take @-1 as in URL, so workaround
        hashstr = hashstr.substr(0,40);
        durstr = "-1";
        mimetype = "video/mp2t";
    }
    else if (durstr.length() > 0 && durstr != "-1")
    {
	// Used in SwarmPlayer 3000
	mimetype = "video/ogg";
    }

    dprintf("%s @%i http get: demands %s mf %s dur %s mime %s\n",tintstr(),http_gw_reqs_open+1,hashstr.c_str(),mfstr.c_str(),durstr.c_str(), mimetype.c_str() );

    uint32_t chunksize=httpgw_chunk_size; // default externally configured
    if (chunksizestr.length() > 0)
        std::istringstream(chunksizestr) >> chunksize;


    // 3. Check for concurrent requests, currently not supported.
    Sha1Hash swarm_id = Sha1Hash(true,hashstr.c_str());
    http_gw_t *existreq = HttpGwFindRequestBySwarmID(swarm_id);
    if (existreq != NULL)
    {
        evhttp_send_error(evreq,409,"Conflict: server does not support concurrent requests to same swarm.");
        return;
    }

    // ANDROID
    std::string filename = "";
    if (httpgw_storage_dir == "") {
    	filename = hashstr;
    }
    else {
    	filename = httpgw_storage_dir;
    	filename += hashstr;
    }

    // 4. Initiate transfer, activating FileTransfer if needed
    bool activate=true;
    int td = swift::Find(swarm_id,activate);
    if (td == -1) {
        // LIVE
        if (durstr != "-1") {
            td = swift::Open(filename,swarm_id,Address(),false,true,false,activate,chunksize);
        }
        else {
            td = swift::LiveOpen(filename,swarm_id,Address(),false,chunksize);
        }

        // Arno, 2011-12-20: Only on new transfers, otherwise assume that CMD GW
        // controls speed
        swift::SetMaxSpeed(td,DDIR_DOWNLOAD,httpgw_maxspeed[DDIR_DOWNLOAD]);
        swift::SetMaxSpeed(td,DDIR_UPLOAD,httpgw_maxspeed[DDIR_UPLOAD]);
    }

    // 5. Record request
    http_gw_t* req = http_requests + http_gw_reqs_open++;
    req->id = ++http_gw_reqs_count;
    req->sinkevreq = evreq;

    // Replace % escaped chars to 8-bit values as part of the UTF-8 encoding
    char *decodedmf = evhttp_uridecode(mfstr.c_str(), 0, NULL);
    req->mfspecname = std::string(decodedmf);
    free(decodedmf);
    req->xcontentdur = durstr;
    req->mimetype = mimetype;
    req->offset = 0;
    req->tosend = 0;
    req->td = td;
    req->lastcpoffset = 0;
    req->sinkevwrite = NULL;
    req->closing = false;
    req->replied = false;
    req->startoff = 0;
    req->endoff = 0;
    req->foundH264NALU = false;

    fprintf(stderr,"httpgw: Opened %s dur %s\n",hashstr.c_str(), durstr.c_str() );

    // We need delayed replying, so take ownership.
    // See http://code.google.com/p/libevent-longpolling/source/browse/trunk/main.c
    // Careful: libevent docs are broken. It doesn't say that evhttp_send_reply_send
    // actually calls evhttp_request_free, i.e. releases ownership for you.
    //
    evhttp_request_own(evreq);

    // Register callback for connection close
    struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
    evhttp_connection_set_closecb(evconn,HttpGwLibeventCloseCallback,req->sinkevreq);

    if (swift::SeqComplete(td) > 0) {
        /*
         * Arno, 2011-10-17: Swift ProgressCallbacks are only called when
         * the data is downloaded, not when it is already on disk. So we need
         * to handle the situation where all or part of the data is already
         * on disk.
         */
        HttpGwFirstProgressCallback(td,bin_t(0,0));
    } else {
        swift::AddProgressCallback(td,&HttpGwFirstProgressCallback,HTTPGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER);
    }
}


bool InstallHTTPGateway( struct event_base *evbase,Address bindaddr, uint32_t chunk_size, double *maxspeed, std::string storage_dir, int32_t vod_step, int32_t min_prebuf ) {
    // Arno, 2011-10-04: From libevent's http-server.c example

    // Arno, 2012-10-16: Made configurable for ANDROID
    httpgw_storage_dir = storage_dir;
    if (vod_step != -1)
	HTTPGW_VOD_PROGRESS_STEP_BYTES = vod_step;
    if (min_prebuf != -1)
	HTTPGW_MIN_PREBUF_BYTES = min_prebuf;

    /* Create a new evhttp object to handle requests. */
    http_gw_event = evhttp_new(evbase);
    if (!http_gw_event) {
        print_error("httpgw: evhttp_new failed");
        return false;
    }

    /* Install callback for all requests */
    evhttp_set_gencb(http_gw_event, HttpGwNewRequestCallback, NULL);

    /* Now we tell the evhttp what port to listen on */
    http_gw_handle = evhttp_bind_socket_with_handle(http_gw_event, bindaddr.ipv4str(), bindaddr.port());
    if (!http_gw_handle) {
        print_error("httpgw: evhttp_bind_socket_with_handle failed");
        return false;
    }

    httpgw_chunk_size = chunk_size;
    // Arno, 2012-11-1: Make copy.
    httpgw_maxspeed = new double[2];
    for (int d=0; d<2; d++)
	httpgw_maxspeed[d] = maxspeed[d];
    httpgw_bindaddr = bindaddr;
    return true;
}


/*
 * ANDROID
 * Return progress info in "x/y" format. This is returned to the Java Activity
 * which uses it to update the progress bar. Currently x is not the number of
 * bytes downloaded, but the number of bytes written to the HTTP connection.
 */
std::string HttpGwGetProgressString(Sha1Hash swarmid)
{
    std::stringstream rets;
    //rets << "httpgw: ";

    int td = swift::Find(swarmid);
    if (td==-1)
	rets << "0/0";
    else
    {
	http_gw_t* req = HttpGwFindRequestByTD(td);
	if (req == NULL)
	    rets << "0/0";
	else
	{
	    //rets << swift::SeqComplete(td);
	    rets << req->offset;
	    rets << "/";
	    rets << swift::Size(req->td);
	}
    }

    return rets.str();
}

// ANDROID
// Arno: dummy place holder
std::string HttpGwStatsGetSpeedCallback(Sha1Hash swarmid)
{
    int dspeed = 0, uspeed = 0;
    uint32_t nleech=0,nseed=0;
    int statsgw_last_down=0, statsgw_last_up=0;

    int td = swift::Find(swarmid);
    if (td !=-1)
    {
	http_gw_t* req = HttpGwFindRequestByTD(td);
	if (req != NULL)
	{
	    statsgw_last_down = req->offset;
	    dspeed = (int)(GetCurrentSpeed(td,DDIR_DOWNLOAD)/1024.0);
	    uspeed = (int)(GetCurrentSpeed(td,DDIR_UPLOAD)/1024.0);
	}

	statsgw_last_up = swift::SeqComplete(td);
    }
    std::stringstream ss;
    ss << dspeed;
    ss << "/";
    ss << uspeed;
    ss << "/";
    ss << nleech;
    ss << "/";
    ss << nseed;
    ss << "/";
    //ss << statsgw_last_down;
    ss << statsgw_last_down;
    ss << "/";
    ss << statsgw_last_up;

    return ss.str();
}



/** For SwarmPlayer 3000's HTTP failover. We should exit if swift isn't
 * delivering such that the extension can start talking HTTP to the backup.
 */
bool HTTPIsSending()
{
    if (http_gw_reqs_open > 0)
    {
	int td = http_requests[http_gw_reqs_open-1].td;
	fprintf(stderr,"httpgw: upload %lf\n",swift::GetCurrentSpeed(td,DDIR_UPLOAD)/1024.0);
	fprintf(stderr,"httpgw: dwload %lf\n",swift::GetCurrentSpeed(td,DDIR_DOWNLOAD)/1024.0);
    }
    return true;
}
