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

#define HTTPGW_PROGRESS_STEP_BYTES 		(256*1024)
// For best performance make bigger than HTTPGW_PROGRESS_STEP_BYTES
#define HTTPGW_MAX_WRITE_BYTES			(512*1024)

// Report swift download progress every 2^layer * chunksize bytes (so 0 = report every chunk)
#define HTTPGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER 	0

// Arno: libevent2 has a liberal understanding of socket writability,
// that may result in tens of megabytes being cached in memory. Limit that
// amount at app level.
#define HTTPGW_MAX_PREBUF_BYTES			(2*1024*1024)

#define HTTPGW_MAX_REQUEST 128

struct http_gw_t {
    int      id;
    uint64_t offset;
    uint64_t tosend;
    int      transfer;
    uint64_t lastcpoffset; // last offset at which we checkpointed
    struct evhttp_request *sinkevreq;
    struct event 		  *sinkevwrite;
    std::string	mfspecname; // (optional) name from multi-file spec
    std::string xcontentdur;
    bool   closing;
    uint64_t startoff;   // MULTIFILE: starting offset in content range of desired file
    uint64_t endoff;     // MULTIFILE: ending offset (careful, for an e.g. 100 byte interval this is 99)
    int replycode;	     // HTTP status code
    int64_t  rangefirst; // First byte wanted in HTTP GET Range request or -1
    int64_t  rangelast;  // Last byte wanted in HTTP GET Range request (also 99 for 100 byte interval) or -1

} http_requests[HTTPGW_MAX_REQUEST];


int http_gw_reqs_open = 0;
int http_gw_reqs_count = 0;
struct evhttp *http_gw_event;
struct evhttp_bound_socket *http_gw_handle;
uint32_t httpgw_chunk_size = SWIFT_DEFAULT_CHUNK_SIZE; // Copy of cmdline param
double *httpgw_maxspeed = NULL;						 // Copy of cmdline param

// Arno, 2010-11-30: for SwarmPlayer 3000 backend autoquit when no HTTP req is received
bool sawhttpconn = false;


http_gw_t *HttpGwFindRequestByEV(struct evhttp_request *evreq) {
	for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
		if (http_requests[httpc].sinkevreq==evreq)
			return &http_requests[httpc];
    }
	return NULL;
}

http_gw_t *HttpGwFindRequestByTransfer(int transfer) {
	for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
		if (http_requests[httpc].transfer==transfer) {
			return &http_requests[httpc];
		}
	}
	return NULL;
}

http_gw_t *HttpGwFindRequestByRoothash(Sha1Hash &wanthash) {
	for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
		http_gw_t *req = &http_requests[httpc];
		if (req == NULL)
			continue;
		FileTransfer *ft = FileTransfer::file(req->transfer);
		if (ft == NULL)
			continue;
		if (ft->root_hash() == wanthash)
			return req;
	}
	return NULL;
}


void HttpGwCloseConnection (http_gw_t* req) {
	dprintf("%s @%i cleanup http request evreq %p\n",tintstr(),req->id, req->sinkevreq);

	struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);

	req->closing = true;
	if (req->offset > req->startoff)
		evhttp_send_reply_end(req->sinkevreq); //WARNING: calls HttpGwLibeventCloseCallback
	else
		evhttp_request_free(req->sinkevreq);

	req->sinkevreq = NULL;

	// Note: for some reason calling conn_free here prevents the last chunks
	// to be sent to the requester?
	//	evhttp_connection_free(evconn); // WARNING: calls HttpGwLibeventCloseCallback

	// Current close policy: checkpoint and DO NOT close transfer, keep on
	// seeding forever. More sophisticated clients should use CMD GW and issue
	// REMOVE.
	swift::Checkpoint(req->transfer);

	// Arno, 2012-05-04: MULTIFILE: once the selected file has been downloaded
	// swift will download all content that comes afterwards too. Poor man's
	// fix to avoid this: seek to end of content when HTTP done. Better would
	// be to seek to end when swift partial download is done, not the serving
	// via HTTP.
	swift::Seek(req->transfer,swift::Size(req->transfer)-1,SEEK_CUR);

	//swift::Close(req->transfer);


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
		dprintf("%s http conn already closed\n",tintstr() );
	else {
		dprintf("%s T%i http close conn\n",tintstr(),req->transfer);
		if (req->closing)
			dprintf("%s http conn already closing\n",tintstr() );
		else
		{
			dprintf("%s http conn being closed\n",tintstr() );
			HttpGwCloseConnection(req);
		}
	}
}




void HttpGwMayWriteCallback (int transfer) {
	// Write some data to client

	http_gw_t* req = HttpGwFindRequestByTransfer(transfer);
	if (req == NULL) {
    	print_error("httpgw: MayWrite: can't find req for transfer");
        return;
    }

	// SEEKTODO: stop downloading when file complete

	// Update endoff as size becomes less fuzzy
	if (swift::Size(req->transfer) < req->endoff)
		req->endoff = swift::Size(req->transfer)-1;

    uint64_t relcomplete = swift::SeqComplete(req->transfer,req->startoff);
    if (relcomplete > req->endoff)
    	relcomplete = req->endoff+1-req->startoff;
    int64_t avail = relcomplete-(req->offset-req->startoff);

    dprintf("%s @%i http write: avail %lld relcomp %llu offset %llu start %llu end %llu tosend %llu\n",tintstr(),req->id, avail, relcomplete, req->offset, req->startoff, req->endoff,  req->tosend );
    //fprintf(stderr,"offset %lli seqcomp %lli comp %lli\n",req->offset, complete, swift::Complete(req->transfer) );

	struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
	struct bufferevent* buffy = evhttp_connection_get_bufferevent(evconn);
	struct evbuffer *outbuf = bufferevent_get_output(buffy);

	//fprintf(stderr,"httpgw: MayWrite avail %i bufev outbuf %i\n",complete-req->offset, evbuffer_get_length(outbuf) );

    if (avail > 0 && evbuffer_get_length(outbuf) < HTTPGW_MAX_PREBUF_BYTES)
    {
    	// Received more than I pushed to player, send data
        char buf[HTTPGW_MAX_WRITE_BYTES];
// Arno, 2010-08-16, TODO
#ifdef WIN32
        uint64_t tosend = min(HTTPGW_MAX_WRITE_BYTES,avail);
#else
        uint64_t tosend = std::min((uint64_t)HTTPGW_MAX_WRITE_BYTES,avail);
#endif
        size_t rd = swift::Read(transfer,buf,tosend,req->offset); // hope it is cached
        if (rd<0) {
        	print_error("httpgw: MayWrite: error pread");
            HttpGwCloseConnection(req);
            return;
        }

        // Construct evbuffer and send incrementally
        struct evbuffer *evb = evbuffer_new();
        int ret = evbuffer_add(evb,buf,rd);
        if (ret < 0) {
        	print_error("httpgw: MayWrite: error evbuffer_add");
        	evbuffer_free(evb);
            HttpGwCloseConnection(req);
            return;
        }

        if (req->offset == req->startoff) {
        	// Not just for chunked encoding, see libevent2's http.c
        	evhttp_send_reply_start(req->sinkevreq, req->replycode, "OK");
        }

        evhttp_send_reply_chunk(req->sinkevreq, evb);
        evbuffer_free(evb);

        int wn = rd;
        dprintf("%s @%i http sent data %ib\n",tintstr(),req->id,(int)wn);

        req->offset += wn;
        req->tosend -= wn;

        // PPPLUG
        swift::Seek(req->transfer,req->offset,SEEK_CUR);
    }

    // Arno, 2010-11-30: tosend is set to fuzzy len, so need extra/other test.
    if (req->tosend==0 || req->offset == req->endoff+1) {
    	// done; wait for new HTTP request
        dprintf("%s @%i done\n",tintstr(),req->id);
        //fprintf(stderr,"httpgw: MayWrite: done, wait for buffer empty before send_end_reply\n" );

        if (evbuffer_get_length(outbuf) == 0) {
        	//fprintf(stderr,"httpgw: MayWrite: done, buffer empty, end req\n" );
        	dprintf("%s http write: done, buffer empty, end req\n",tintstr() );
        	HttpGwCloseConnection(req);
        }
    }
    else {
    	// wait for data
        dprintf("%s @%i waiting for data\n",tintstr(),req->id);
    }
}

void HttpGwLibeventMayWriteCallback(evutil_socket_t fd, short events, void *evreqvoid );

void HttpGwSubscribeToWrite(http_gw_t * req) {
	struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
	struct event_base *evbase =	evhttp_connection_get_base(evconn);
	struct bufferevent* evbufev = evhttp_connection_get_bufferevent(evconn);

	if (req->sinkevwrite != NULL)
		event_free(req->sinkevwrite); // FAXME: clean in CloseConn

	req->sinkevwrite = event_new(evbase,bufferevent_getfd(evbufev),EV_WRITE,HttpGwLibeventMayWriteCallback,req->sinkevreq);
	struct timeval t;
	t.tv_sec = 10;
	int ret = event_add(req->sinkevwrite,&t);
	//fprintf(stderr,"httpgw: HttpGwSubscribeToWrite: added event\n");
}


void HttpGwLibeventMayWriteCallback(evutil_socket_t fd, short events, void *evreqvoid )
{
	//fprintf(stderr,"httpgw: MayWrite: %d events %d evreq is %p\n", fd, events, evreqvoid);

	http_gw_t * req = HttpGwFindRequestByEV((struct evhttp_request *)evreqvoid);
	if (req != NULL) {
		//fprintf(stderr,"httpgw: MayWrite: %d events %d httpreq is %p\n", fd, events, req);
		HttpGwMayWriteCallback(req->transfer);


		// Arno, 2011-12-20: No autoreschedule, let HttpGwSwiftProgressCallback do that
		//if (req->sinkevreq != NULL) // Conn closed
		//	HttpGwSubscribeToWrite(req);

		//fprintf(stderr,"GOTO WRITE %lli >= %lli\n", swift::Complete(req->transfer)+HTTPGW_MAX_WRITE_BYTES, swift::Size(req->transfer) );

		if (swift::Complete(req->transfer)+HTTPGW_MAX_WRITE_BYTES >= swift::Size(req->transfer)) {

        	// We don't get progress callback for last chunk < chunk size, nor
        	// when all data is already on disk. In that case, just keep on
			// subscribing to HTTP socket writability until all data is sent.
			if (req->sinkevreq != NULL) // Conn closed
				HttpGwSubscribeToWrite(req);
		}
	}
}

void HttpGwSwiftProgressCallback (int transfer, bin_t bin) {
	// Subsequent HTTPGW_PROGRESS_STEP_BYTES available

	dprintf("%s T%i http more progress\n",tintstr(),transfer);
	http_gw_t* req = HttpGwFindRequestByTransfer(transfer);
	if (req == NULL)
		return;

	// Arno, 2011-12-20: We have new data to send, wait for HTTP socket writability
	if (req->sinkevreq != NULL) { // Conn closed
		HttpGwSubscribeToWrite(req);
	}
}


bool HttpGwParseContentRangeHeader(http_gw_t *req,uint64_t filesize)
{
    struct evkeyvalq *reqheaders =	evhttp_request_get_input_headers(req->sinkevreq);
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

	dprintf("%s @%i http range request spec %s\n",tintstr(),req->id, seek.c_str() );

	if (seek.find(",") != std::string::npos) {
		// - Range header contains set, not supported at the moment
		bad = true;
	} else 	{
		// Determine first and last bytes of requested range
		idx = seek.find("-");

		dprintf("%s @%i http range request idx %d\n", tintstr(),req->id, idx );

		if (idx == std::string::npos)
			return false;
		if (idx == 0) {
			// -444 format
			req->rangefirst = -1;
		} else {
			std::istringstream(seek.substr(0,idx)) >> req->rangefirst;
		}


		dprintf("%s @%i http range request first %s %lld\n", tintstr(),req->id, seek.substr(0,idx).c_str(), req->rangefirst );

		if (idx == seek.length()-1)
			req->rangelast = -1;
		else {
			// 444- format
			std::istringstream(seek.substr(idx+1)) >> req->rangelast;
		}

		dprintf("%s @%i http range request last %s %lld\n", tintstr(),req->id, seek.substr(idx+1).c_str(), req->rangelast );

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
			if (req->rangefirst == -1) 	{
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

		dprintf("%s @%i http invalid range %lld-%lld\n",tintstr(),req->id,req->rangefirst,req->rangelast );
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

	dprintf("%s @%i http valid range %lld-%lld\n",tintstr(),req->id,req->rangefirst,req->rangelast );

	return true;
}


void HttpGwFirstProgressCallback (int transfer, bin_t bin) {
	// First chunk of data available
	dprintf("%s T%i http first progress\n",tintstr(),transfer);

	// Need the first chunk
	if (swift::SeqComplete(transfer) == 0)
	{
		dprintf("%s T%i first: not enough seqcomp\n",tintstr(),transfer );
        return;
	}

	http_gw_t* req = HttpGwFindRequestByTransfer(transfer);
	if (req == NULL)
	{
		dprintf("%s T%i first: req not found\n",tintstr(),transfer );
		return;
	}

	// MULTIFILE
	// Is storage ready?
	FileTransfer *ft = FileTransfer::file(req->transfer);
	if (ft == NULL) {
		dprintf("%s T%i first: FileTransfer not found\n",tintstr(),transfer );
    	evhttp_send_error(req->sinkevreq,500,"Internal error: Content not found although downloading it.");
    	return;
	}
	if (!ft->GetStorage()->IsReady())
	{
		dprintf("%s T%i first: Storage not ready\n",tintstr(),transfer );
		return; // wait for some more data
	}

	// Protection against spurious callback
	if (req->tosend > 0)
	{
		dprintf("%s T%i first: already set tosend\n",tintstr(),transfer );
		return;
	}

	// Good to go. Reconfigure callbacks
	swift::RemoveProgressCallback(transfer,&HttpGwFirstProgressCallback);
    int progresslayer = bytes2layer(HTTPGW_PROGRESS_STEP_BYTES,swift::ChunkSize(transfer));
    swift::AddProgressCallback(transfer,&HttpGwSwiftProgressCallback,progresslayer);

    // Send header of HTTP reply
	uint64_t filesize = 0;
	if (req->mfspecname != "")
	{
		// MULTIFILE
		// Find out size of selected file
		storage_files_t sfs = ft->GetStorage()->GetStorageFiles();
		storage_files_t::iterator iter;
		bool found = false;
		for (iter = sfs.begin(); iter < sfs.end(); iter++)
		{
			StorageFile *sf = *iter;

			dprintf("%s T%i first: mf: comp <%s> <%s>\n",tintstr(),transfer, sf->GetSpecPathName().c_str(), req->mfspecname.c_str() );

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
			return;
		}
	}
	else
	{
		// Single file
		req->startoff = 0;
		req->endoff = swift::Size(req->transfer)-1;
		filesize = swift::Size(transfer);
	}

	// Handle HTTP GET Range request, i.e. additional offset within content or file
	if (!HttpGwParseContentRangeHeader(req,filesize))
		return;

	if (req->rangefirst != -1)
	{
		// Range request
		req->startoff += req->rangefirst;
		req->endoff = req->startoff + req->rangelast;
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
		int ret = swift::Seek(req->transfer,req->startoff,SEEK_SET);
		if (ret < 0) {
			evhttp_send_error(req->sinkevreq,500,"Internal error: Cannot seek to file start in range request or multi-file content.");
			return;
		}
	}

	// Convert size to string
	std::ostringstream closs;
	closs << req->tosend;

	struct evkeyvalq *reqheaders = evhttp_request_get_output_headers(req->sinkevreq);
	//evhttp_add_header(reqheaders, "Connection", "keep-alive" );
	evhttp_add_header(reqheaders, "Connection", "close" );
	evhttp_add_header(reqheaders, "Content-Type", "video/ogg" );
	if (req->xcontentdur.length() > 0)
		evhttp_add_header(reqheaders, "X-Content-Duration", req->xcontentdur.c_str() );
	evhttp_add_header(reqheaders, "Content-Length", closs.str().c_str() );
	//evhttp_add_header(reqheaders, "Accept-Ranges", "none" );

	dprintf("%s @%i headers sent, size %lli\n",tintstr(),req->id,req->tosend);

	/*
	 * Arno, 2011-10-17: Swift ProgressCallbacks are only called when
	 * the data is downloaded, not when it is already on disk. So we need
	 * to handle the situation where all or part of the data is already
	 * on disk. Subscribing to writability of the socket works,
	 * but requires libevent2 >= 2.1 (or our backported version)
	 */
	HttpGwSubscribeToWrite(req);
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

    dprintf("%s @%i http new request\n",tintstr(),http_gw_reqs_count+1);

    if (evhttp_request_get_command(evreq) != EVHTTP_REQ_GET) {
            return;
    }
	sawhttpconn = true;

    // 1. Get URI
	// Format: /roothash[/multi-file][@duration]
	// ARNOTODO: allow for chunk size to be set via URL?
    std::string uri = evhttp_request_get_uri(evreq);

    struct evkeyvalq *reqheaders =	evhttp_request_get_input_headers(evreq);

    // Arno, 2012-04-19: libevent adds "Connection: keep-alive" to reply headers
    // if there is one in the request headers, even if a different Connection
    // reply header has already been set. And we don't do persistent conns here.
    //
    evhttp_remove_header(reqheaders,"Connection"); // Remove Connection: keep-alive

    // 2. Parse URI
    std::string hashstr = "", mfstr="", durstr="";

    if (uri.length() == 1)     {
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

    dprintf("%s @%i demands %s %s %s\n",tintstr(),http_gw_reqs_open+1,hashstr.c_str(),mfstr.c_str(),durstr.c_str() );


    // 3. Check for concurrent requests, currently not supported.
    Sha1Hash root_hash = Sha1Hash(true,hashstr.c_str());
    http_gw_t *existreq = HttpGwFindRequestByRoothash(root_hash);
    if (existreq != NULL)
    {
    	evhttp_send_error(evreq,409,"Conflict: server does not support concurrent requests to same swarm.");
    	return;
    }

    // 4. Initiate transfer
    int transfer = swift::Find(root_hash);
    if (transfer==-1) {
        transfer = swift::Open(hashstr,root_hash,Address(),false,httpgw_chunk_size);
        dprintf("%s @%i trying to HTTP GET swarm %s that has not been STARTed\n",tintstr(),http_gw_reqs_open+1,hashstr.c_str());

        // Arno, 2011-12-20: Only on new transfers, otherwise assume that CMD GW
        // controls speed
        FileTransfer *ft = FileTransfer::file(transfer);
        ft->SetMaxSpeed(DDIR_DOWNLOAD,httpgw_maxspeed[DDIR_DOWNLOAD]);
        ft->SetMaxSpeed(DDIR_UPLOAD,httpgw_maxspeed[DDIR_UPLOAD]);
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
	req->offset = 0;
    req->tosend = 0;
    req->transfer = transfer;
    req->lastcpoffset = 0;
    req->sinkevwrite = NULL;
    req->closing = false;
    req->startoff = 0;
    req->endoff = 0;

    fprintf(stderr,"httpgw: Opened %s\n",hashstr.c_str());

    // We need delayed replying, so take ownership.
    // See http://code.google.com/p/libevent-longpolling/source/browse/trunk/main.c
	// Careful: libevent docs are broken. It doesn't say that evhttp_send_reply_send
	// actually calls evhttp_request_free, i.e. releases ownership for you.
	//
    evhttp_request_own(evreq);

    // Register callback for connection close
    struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
    evhttp_connection_set_closecb(evconn,HttpGwLibeventCloseCallback,req->sinkevreq);

    if (swift::Size(transfer)) {
        HttpGwFirstProgressCallback(transfer,bin_t(0,0));
    } else {
        swift::AddProgressCallback(transfer,&HttpGwFirstProgressCallback,HTTPGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER);
    }
}


bool InstallHTTPGateway (struct event_base *evbase,Address bindaddr, uint32_t chunk_size, double *maxspeed) {
	// Arno, 2011-10-04: From libevent's http-server.c example

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
	httpgw_maxspeed = maxspeed;
	return true;
}


uint64_t lastoffset=0;
uint64_t lastcomplete=0;
tint test_time = 0;

/** For SwarmPlayer 3000's HTTP failover. We should exit if swift isn't
 * delivering such that the extension can start talking HTTP to the backup.
 */
bool HTTPIsSending()
{
	if (http_gw_reqs_open > 0)
	{
		FileTransfer *ft = FileTransfer::file(http_requests[http_gw_reqs_open-1].transfer);
		if (ft != NULL) {
			fprintf(stderr,"httpgw: upload %lf\n",ft->GetCurrentSpeed(DDIR_UPLOAD)/1024.0);
			fprintf(stderr,"httpgw: dwload %lf\n",ft->GetCurrentSpeed(DDIR_DOWNLOAD)/1024.0);
			fprintf(stderr,"httpgw: seqcmp %llu\n", swift::SeqComplete(http_requests[http_gw_reqs_open-1].transfer));
		}
	}
    return true;

    // TODO: reactivate when used in SwiftTransport / SwarmPlayer 3000.

	if (test_time == 0)
	{
		test_time = NOW;
		return true;
	}

	if (NOW > test_time+5*1000*1000)
	{
		fprintf(stderr,"http alive: httpc count is %d\n", http_gw_reqs_open );

		if (http_gw_reqs_open == 0 && !sawhttpconn)
		{
			fprintf(stderr,"http alive: no HTTP activity ever, quiting\n");
			return false;
		}
		else
			sawhttpconn = true;

	    for (int httpc=0; httpc<http_gw_reqs_open; httpc++)
	    {

	    	/*
	    	if (http_requests[httpc].offset >= 100000)
	    	{
	    		fprintf(stderr,"http alive: 100K sent, quit\n");
				return false;
	    	}
	    	else
	    	{
	    		fprintf(stderr,"http alive: sent %lli\n", http_requests[httpc].offset );
	    		return true;
	    	}
	    	*/

			// If
			// a. don't know anything about content (i.e., size still 0) or
			// b. not sending to HTTP client and not at end, and
			//    not downloading from P2P and not at end
			// then stop.
			if ( swift::Size(http_requests[httpc].transfer) == 0 || \
				 (http_requests[httpc].offset == lastoffset &&
				 http_requests[httpc].offset != swift::Size(http_requests[httpc].transfer) && \
			     swift::Complete(http_requests[httpc].transfer) == lastcomplete && \
			     swift::Complete(http_requests[httpc].transfer) != swift::Size(http_requests[httpc].transfer)))
			{
				fprintf(stderr,"http alive: no progress, quiting\n");
				//getchar();
				return false;
			}

			/*
			if (http_requests[httpc].offset == swift::Size(http_requests[httpc].transfer))
			{
				// TODO: seed for a while.
				fprintf(stderr,"http alive: data delivered to client, quiting\n");
				return false;
			}
			*/

			lastoffset = http_requests[httpc].offset;
			lastcomplete = swift::Complete(http_requests[httpc].transfer);
	    }
		test_time = NOW;

	    return true;
	}
	else
		return true;
}
