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
    char*    xcontentdur;
    bool   closing;

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

void HttpGwCloseConnection (http_gw_t* req) {
	dprintf("%s @%i cleanup http request evreq %p\n",tintstr(),req->id, req->sinkevreq);

	struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);

	dprintf("%s @%i cleanup: before send reply\n",tintstr(),req->id);

	req->closing = true;
	if (req->offset > 0)
		evhttp_send_reply_end(req->sinkevreq); //WARNING: calls HttpGwLibeventCloseCallback
	else
		evhttp_request_free(req->sinkevreq);

	dprintf("%s @%i cleanup: reset evreq\n",tintstr(),req->id);
	req->sinkevreq = NULL;
	dprintf("%s @%i cleanup: after reset evreq\n",tintstr(),req->id);

	// Note: for some reason calling conn_free here prevents the last chunks
	// to be sent to the requester?
	//	evhttp_connection_free(evconn); // WARNING: calls HttpGwLibeventCloseCallback

	// Current close policy: checkpoint and DO NOT close transfer, keep on
	// seeding forever. More sophisticated clients should use CMD GW and issue
	// REMOVE.
	swift::Checkpoint(req->transfer);

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
			HttpGwCloseConnection(req);
	}
}




void HttpGwMayWriteCallback (int transfer) {
	// Write some data to client

	http_gw_t* req = HttpGwFindRequestByTransfer(transfer);
	if (req == NULL) {
    	print_error("httpgw: MayWrite: can't find req for transfer");
        return;
    }

    uint64_t complete = swift::SeqComplete(req->transfer);

    //dprintf("%s @%i http write complete %lli offset %lli\n",tintstr(),req->id, complete, req->offset);
    //fprintf(stderr,"offset %lli seqcomp %lli comp %lli\n",req->offset, complete, swift::Complete(req->transfer) );

	struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
	struct bufferevent* buffy = evhttp_connection_get_bufferevent(evconn);
	struct evbuffer *outbuf = bufferevent_get_output(buffy);

	//fprintf(stderr,"httpgw: MayWrite avail %i bufev outbuf %i\n",complete-req->offset, evbuffer_get_length(outbuf) );

    if (complete > req->offset && evbuffer_get_length(outbuf) < HTTPGW_MAX_PREBUF_BYTES)
    {
    	// Received more than I pushed to player, send data
        char buf[HTTPGW_MAX_WRITE_BYTES];
// Arno, 2010-08-16, TODO
#ifdef WIN32
        uint64_t tosend = min(HTTPGW_MAX_WRITE_BYTES,complete-req->offset);
#else
        uint64_t tosend = std::min((uint64_t)HTTPGW_MAX_WRITE_BYTES,complete-req->offset);
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

        if (req->offset == 0) {
        	// Not just for chunked encoding, see libevent2's http.c
        	evhttp_send_reply_start(req->sinkevreq, 200, "OK");
        }

        evhttp_send_reply_chunk(req->sinkevreq, evb);
        evbuffer_free(evb);

        int wn = rd;
        dprintf("%s @%i http sent data %ib\n",tintstr(),req->id,(int)wn);

        req->offset += wn;
        req->tosend -= wn;

        // PPPLUG
    	FileTransfer *ft = FileTransfer::file(transfer);
    	if (ft == NULL)
    		return;
        ft->picker().updatePlaybackPos( wn/ft->file().chunk_size() );
    }

    // Arno, 2010-11-30: tosend is set to fuzzy len, so need extra/other test.
    if (req->tosend==0 || req->offset ==  swift::Size(req->transfer)) {
    	// done; wait for new HTTP request
        dprintf("%s @%i done\n",tintstr(),req->id);
        //fprintf(stderr,"httpgw: MayWrite: done, wait for buffer empty before send_end_reply\n" );

        if (evbuffer_get_length(outbuf) == 0) {
        	//fprintf(stderr,"httpgw: MayWrite: done, buffer empty, end req\n" );
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


void HttpGwFirstProgressCallback (int transfer, bin_t bin) {
	// First chunk of data available
	dprintf("%s T%i http first progress\n",tintstr(),transfer);

	if (!bin.contains(bin_t(0,0))) // need the first chunk
        return;

	swift::RemoveProgressCallback(transfer,&HttpGwFirstProgressCallback);
    int progresslayer = bytes2layer(HTTPGW_PROGRESS_STEP_BYTES,swift::ChunkSize(transfer));
    swift::AddProgressCallback(transfer,&HttpGwSwiftProgressCallback,progresslayer);

	http_gw_t* req = HttpGwFindRequestByTransfer(transfer);
	if (req == NULL)
		return;
	if (req->tosend==0) { // FIXME states
		uint64_t filesize = swift::Size(transfer);
		char filesizestr[256];
		sprintf(filesizestr,"%lli",filesize);

		struct evkeyvalq *headers = evhttp_request_get_output_headers(req->sinkevreq);
		//evhttp_add_header(headers, "Connection", "keep-alive" );
		evhttp_add_header(headers, "Connection", "close" );
		evhttp_add_header(headers, "Content-Type", "video/ogg" );
		evhttp_add_header(headers, "X-Content-Duration",req->xcontentdur );
		evhttp_add_header(headers, "Content-Length", filesizestr );
		evhttp_add_header(headers, "Accept-Ranges", "none" );

		req->tosend = filesize;
		dprintf("%s @%i headers_sent size %lli\n",tintstr(),req->id,filesize);

		/*
		 * Arno, 2011-10-17: Swift ProgressCallbacks are only called when
		 * the data is downloaded, not when it is already on disk. So we need
		 * to handle the situation where all or part of the data is already
		 * on disk. Subscribing to writability of the socket works,
		 * but requires libevent2 >= 2.1 (or our backported version)
		 */
		HttpGwSubscribeToWrite(req);
    }
}


void HttpGwNewRequestCallback (struct evhttp_request *evreq, void *arg) {

    dprintf("%s @%i http new request\n",tintstr(),http_gw_reqs_count+1);

    if (evhttp_request_get_command(evreq) != EVHTTP_REQ_GET) {
            return;
    }
	sawhttpconn = true;

    // Parse URI
    const char *uri = evhttp_request_get_uri(evreq);
    //struct evkeyvalq *headers =	evhttp_request_get_input_headers(evreq);
    //const char *contentrangestr =evhttp_find_header(headers,"Content-Range");

    char *tokenuri = (char *)malloc(strlen(uri)+1);
    strcpy(tokenuri,uri);
    char * hashch=strtok(tokenuri,"/"), hash[41];
    while (hashch && (1!=sscanf(hashch,"%40[0123456789abcdefABCDEF]",hash) || strlen(hash)!=40))
        hashch = strtok(NULL,"/");
    free(tokenuri);
    if (strlen(hash)!=40) {
    	evhttp_send_error(evreq,400,"Path must be root hash in hex, 40 bytes.");
        return;
    }
    char *xcontentdur = NULL;
    if (strlen(uri) > 42) {
    	xcontentdur = (char *)malloc(strlen(uri)-42+1);
    	strcpy(xcontentdur,&uri[42]);
    }
    else
    	xcontentdur = (char *)"0";
    dprintf("%s @%i demands %s %s\n",tintstr(),http_gw_reqs_open+1,hash,xcontentdur);

    // initiate transmission
    Sha1Hash root_hash = Sha1Hash(true,hash);
    int transfer = swift::Find(root_hash);
    if (transfer==-1) {
        transfer = swift::Open(hash,root_hash,Address(),false,httpgw_chunk_size); // ARNOTODO: allow for chunk size to be set via URL?
        dprintf("%s @%i trying to HTTP GET swarm %s that has not been STARTed\n",tintstr(),http_gw_reqs_open+1,hash);

        // Arno, 2011-12-20: Only on new transfers, otherwise assume that CMD GW
        // controls speed
        FileTransfer *ft = FileTransfer::file(transfer);
        ft->SetMaxSpeed(DDIR_DOWNLOAD,httpgw_maxspeed[DDIR_DOWNLOAD]);
        ft->SetMaxSpeed(DDIR_UPLOAD,httpgw_maxspeed[DDIR_UPLOAD]);
    }

    // Record request
    http_gw_t* req = http_requests + http_gw_reqs_open++;
    req->id = ++http_gw_reqs_count;
    req->sinkevreq = evreq;
    req->xcontentdur = xcontentdur;
    req->offset = 0;
    req->tosend = 0;
    req->transfer = transfer;
    req->lastcpoffset = 0;
    req->sinkevwrite = NULL;
    req->closing = false;

    fprintf(stderr,"httpgw: Opened %s\n",hash);

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
