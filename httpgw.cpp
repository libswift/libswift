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

#define HTTPGW_VOD_PROGRESS_STEP_BYTES 		(256*1024)
// For best performance make bigger than HTTPGW_PROGRESS_STEP_BYTES
#define HTTPGW_VOD_MAX_WRITE_BYTES			(512*1024)


#define HTTPGW_LIVE_PROGRESS_STEP_BYTES 	(16*1024)
// For best performance make bigger than HTTPGW_PROGRESS_STEP_BYTES
#define HTTPGW_LIVE_MAX_WRITE_BYTES			(32*1024)


// Report swift download progress every 2^layer * chunksize bytes (so 0 = report every chunk)
// Note: for LIVE this cannot be reliably used to force a prebuffer size,
// as this gets called with a subtree of size X has been downloaded. If the
// hook-in point is in the middle of such a subtree, the call won't happen
// until the next full subtree has been downloaded, i.e. after ~1.5 times the
// prebuf has been downloaded. See HTTPGW_MIN_PREBUF_BYTES
//
#define HTTPGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER 	4  // 16 KB

// Arno: libevent2 has a liberal understanding of socket writability,
// that may result in tens of megabytes being cached in memory. Limit that
// amount at app level.
#define HTTPGW_MAX_OUTBUF_BYTES			(2*1024*1024)

// Arno:
#define HTTPGW_MIN_PREBUF_BYTES			(256*1024)


#define HTTPGW_MAX_REQUEST 128

struct http_gw_t {
    int      id;
    uint64_t offset;
    uint64_t tosend;
    int      fdes;
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
size_t httpgw_chunk_size = SWIFT_DEFAULT_CHUNK_SIZE; // Copy of cmdline param
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

http_gw_t *HttpGwFindRequestByFD(int fdes) {
	for (int httpc=0; httpc<http_gw_reqs_open; httpc++) {
		if (http_requests[httpc].fdes==fdes) {
			return &http_requests[httpc];
		}
	}
	return NULL;
}

// CHECKPOINT
void HttpGwCheckpoint(int fdes) {
	// Save transfer's binmap for zero-hashcheck restart
	FileTransfer *ft = (FileTransfer *)ContentTransfer::transfer(fdes);
	if (ft == NULL)
		return;

	std::string binmap_filename = ft->hashtree()->filename();
	binmap_filename.append(".mbinmap");
	fprintf(stderr,"httpgw checkpointing %s at %lli\n", binmap_filename.c_str(), Complete(fdes));
	FILE *fp = fopen(binmap_filename.c_str(),"wb");
	if (!fp) {
		print_error("cannot open mbinmap for writing");
		return;
	}
	if (ft->hashtree()->serialize(fp) < 0)
		print_error("writing to mbinmap");
	fclose(fp);
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
	ContentTransfer *ct = ContentTransfer::transfer(req->fdes);
	if (ct != NULL) {
		if (ct->ttype() == FILE_TRANSFER)
			HttpGwCheckpoint(req->fdes);
		//else //LIVE
		//	swift::Close(req->fdes);
	}

	//swift::Close(req->fdes);

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
		dprintf("%s T%i http close conn\n",tintstr(),req->fdes);
		if (req->closing)
			dprintf("%s http conn already closing\n",tintstr() );
		else
			HttpGwCloseConnection(req);
	}
}




void HttpGwMayWriteCallback (int fdes) {
	// Write some data to client

	http_gw_t* req = HttpGwFindRequestByFD(fdes);
	if (req == NULL) {
    	print_error("httpgw: MayWrite: can't find req for transfer");
        return;
    }

    uint64_t seqcomplete = swift::SeqComplete(req->fdes);

    // Arno: Universal (live,VOD) prebuffering. Coordinate with cmdgw.cpp CMDGW_MAX_PREBUF_BYTES
    if (seqcomplete < HTTPGW_MIN_PREBUF_BYTES) {
    	fprintf(stderr,"httpgw: MayWrite: prebuffering %llu\n", seqcomplete );
    	return;
    }

    //dprintf("%s @%i http write seqcomplete %lli offset %lli\n",tintstr(),req->id, seqcomplete, req->offset);
    //fprintf(stderr,"offset %lli seqcomp %lli comp %lli\n",req->offset, seqcomplete, swift::Complete(req->fdes) );

	struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
	struct bufferevent* buffy = evhttp_connection_get_bufferevent(evconn);
	struct evbuffer *outbuf = bufferevent_get_output(buffy);

	fprintf(stderr,"httpgw: MayWrite: avail %lli bufev outbuf %i\n",seqcomplete-req->offset, evbuffer_get_length(outbuf) );

    if (seqcomplete > req->offset && evbuffer_get_length(outbuf) < HTTPGW_MAX_OUTBUF_BYTES)
    {
    	ContentTransfer *ct = ContentTransfer::transfer(fdes);
    	if (ct == NULL)
    		return;

    	int max_write_bytes = 0;
    	if (ct->ttype() == FILE_TRANSFER)
    		max_write_bytes = HTTPGW_VOD_MAX_WRITE_BYTES;
    	else
    		max_write_bytes = HTTPGW_LIVE_MAX_WRITE_BYTES;

    	// Received more than I pushed to player, send data
        char *buf = (char *)malloc(max_write_bytes);
// Arno, 2010-08-16, TODO
#ifdef WIN32
        uint64_t tosend = min(max_write_bytes,seqcomplete-req->offset);
#else
        uint64_t tosend = std::min((uint64_t)max_write_bytes,seqcomplete-req->offset);
#endif
        size_t rd = pread(req->fdes,buf,tosend,swift::LiveStart(req->fdes)+req->offset); // hope it is cached
        if (rd<0) {
        	print_error("httpgw: MayWrite: error pread");
            HttpGwCloseConnection(req);
            free(buf);
            return;
        }

        // Construct evbuffer and send incrementally
        struct evbuffer *evb = evbuffer_new();
        int ret = evbuffer_add(evb,buf,rd);
        if (ret < 0) {
        	print_error("httpgw: MayWrite: error evbuffer_add");
        	evbuffer_free(evb);
            HttpGwCloseConnection(req);
            free(buf);
            return;
        }
        //free(buf);

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
        ct->picker()->updatePlaybackPos( wn/ct->chunk_size() );
    }

    // Arno, 2010-11-30: tosend is set to fuzzy len, so need extra/other test.
    if (req->tosend==0 || req->offset ==  swift::Size(req->fdes)) {
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
		HttpGwMayWriteCallback(req->fdes);


		// Arno, 2011-12-20: No autoreschedule, let HttpGwSwiftProgressCallback do that
		//if (req->sinkevreq != NULL) // Conn closed
		//	HttpGwSubscribeToWrite(req);

		//fprintf(stderr,"GOTO WRITE %lli >= %lli\n", swift::Complete(req->fdes)+HTTPGW_MAX_WRITE_BYTES, swift::Size(req->fdes) );

    	ContentTransfer *ct = ContentTransfer::transfer(req->fdes);
    	if (ct->ttype() == FILE_TRANSFER) {

    		if (swift::Complete(req->fdes)+HTTPGW_VOD_MAX_WRITE_BYTES >= swift::Size(req->fdes)) {

    			// We don't get progress callback for last chunk < chunk size, nor
    			// when all data is already on disk. In that case, just keep on
    			// subscribing to HTTP socket writability until all data is sent.
    			if (req->sinkevreq != NULL) // Conn closed
    				HttpGwSubscribeToWrite(req);
    		}
		}
	}
}

void HttpGwSwiftProgressCallback (int fdes, bin_t bin) {
	// Subsequent HTTPGW_PROGRESS_STEP_BYTES available

	dprintf("%s T%i http more progress\n",tintstr(),fdes);
	http_gw_t* req = HttpGwFindRequestByFD(fdes);
	if (req == NULL)
		return;

	// Arno, 2011-12-20: We have new data to send, wait for HTTP socket writability
	if (req->sinkevreq != NULL) { // Conn closed
		HttpGwSubscribeToWrite(req);
	}
}


void HttpGwFirstProgressCallback (int fdes, bin_t bin) {
	// First chunk of data available
	dprintf("%s T%i http first progress\n",tintstr(),fdes);


	fprintf(stderr,"httpgw: FirstProgres: hook-in at %lli\n", swift::LiveStart(fdes) );


	//LIVETODO: reinstate?
	//if (!bin.contains(bin_t(0,0))) // need the first chunk
    //    return;

	swift::RemoveProgressCallback(fdes,&HttpGwFirstProgressCallback);

	ContentTransfer *ct = ContentTransfer::transfer(fdes);
	int stepbytes = 0;
	if (ct->ttype() == FILE_TRANSFER)
		stepbytes = HTTPGW_VOD_PROGRESS_STEP_BYTES;
	else
		stepbytes = HTTPGW_LIVE_PROGRESS_STEP_BYTES;
    int progresslayer = bytes2layer(stepbytes,swift::ChunkSize(fdes));

    fprintf(stderr,"httpgw: FirstProgres: progresslayer %d\n", progresslayer );

    swift::AddProgressCallback(fdes,&HttpGwSwiftProgressCallback,progresslayer);

	http_gw_t* req = HttpGwFindRequestByFD(fdes);
	if (req == NULL)
		return;
	if (req->tosend==0) { // FIXME states
		uint64_t filesize = swift::Size(fdes);
		char filesizestr[256];
		// LIVE
		if (!strcmp(req->xcontentdur,"-1"))
			strcpy(filesizestr,"-1");
		else
			sprintf(filesizestr,"%lli",filesize);

		struct evkeyvalq *headers = evhttp_request_get_output_headers(req->sinkevreq);
		//evhttp_add_header(headers, "Connection", "keep-alive" );
		evhttp_add_header(headers, "Connection", "close" );
		//evhttp_add_header(headers, "Content-Type", "video/ogg" );
		evhttp_add_header(headers, "Content-Type", "video/mp2t" );
		evhttp_add_header(headers, "X-Content-Duration",req->xcontentdur );

		//LIVE TEST TEST TEST
		evhttp_add_header(headers, "Content-Length", filesizestr );
		//evhttp_add_header(headers, "Content-Length", "41623650" );
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
		//HttpGwSubscribeToWrite(req);
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


    fprintf(stderr,"httpgw: URI is %s\n", uri );

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
    int fdes = swift::Find(root_hash);
    if (fdes==-1) {
    	// LIVE
        if (strcmp(xcontentdur,"-1")) {
        	fdes = swift::Open(hash,root_hash,Address(),false,httpgw_chunk_size); // ARNOTODO: allow for chunk size to be set via URL?
        }
        else {
        	fdes = swift::LiveOpen(hash,root_hash,Address(),false,httpgw_chunk_size);
        }
        dprintf("%s @%i trying to HTTP GET swarm %s that has not been STARTed\n",tintstr(),http_gw_reqs_open+1,hash);

        // Arno, 2011-12-20: Only on new transfers, otherwise assume that CMD GW
        // controls speed
        ContentTransfer *ct = ContentTransfer::transfer(fdes);
        ct->SetMaxSpeed(DDIR_DOWNLOAD,httpgw_maxspeed[DDIR_DOWNLOAD]);
        ct->SetMaxSpeed(DDIR_UPLOAD,httpgw_maxspeed[DDIR_UPLOAD]);
    }

    // Record request
    http_gw_t* req = http_requests + http_gw_reqs_open++;
    req->id = ++http_gw_reqs_count;
    req->sinkevreq = evreq;
    req->xcontentdur = xcontentdur;
    req->offset = 0;
    req->tosend = 0;
    req->fdes = fdes;
    req->lastcpoffset = 0;
    req->sinkevwrite = NULL;
    req->closing = false;

    fprintf(stderr,"httpgw: Opened %s dur %s\n",hash,xcontentdur);

    // We need delayed replying, so take ownership.
    // See http://code.google.com/p/libevent-longpolling/source/browse/trunk/main.c
	// Careful: libevent docs are broken. It doesn't say that evhttp_send_reply_send
	// actually calls evhttp_request_free, i.e. releases ownership for you.
	//
    evhttp_request_own(evreq);

    // Register callback for connection close
    struct evhttp_connection *evconn = evhttp_request_get_connection(req->sinkevreq);
    evhttp_connection_set_closecb(evconn,HttpGwLibeventCloseCallback,req->sinkevreq);


    fprintf(stderr,"httpgw: Size is %lli\n", swift::Size(fdes) );

    if (swift::Size(fdes) > 0) {
        HttpGwFirstProgressCallback(fdes,bin_t(0,0));
    } else {
        swift::AddProgressCallback(fdes,&HttpGwFirstProgressCallback,HTTPGW_FIRST_PROGRESS_BYTE_INTERVAL_AS_LAYER);
    }
}


bool InstallHTTPGateway (struct event_base *evbase,Address bindaddr, size_t chunk_size, double *maxspeed) {
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
	return true;

	//LIVETODO

	if (http_gw_reqs_open > 0)
	{
		FileTransfer *ft = (FileTransfer *)ContentTransfer::transfer(http_requests[http_gw_reqs_open-1].fdes);
		if (ft != NULL) {
			fprintf(stderr,"httpgw: upload %lf\n",ft->GetCurrentSpeed(DDIR_UPLOAD)/1024.0);
			fprintf(stderr,"httpgw: dwload %lf\n",ft->GetCurrentSpeed(DDIR_DOWNLOAD)/1024.0);
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
			if ( swift::Size(http_requests[httpc].fdes) == 0 || \
				 (http_requests[httpc].offset == lastoffset &&
				 http_requests[httpc].offset != swift::Size(http_requests[httpc].fdes) && \
			     swift::Complete(http_requests[httpc].fdes) == lastcomplete && \
			     swift::Complete(http_requests[httpc].fdes) != swift::Size(http_requests[httpc].fdes)))
			{
				fprintf(stderr,"http alive: no progress, quiting\n");
				//getchar();
				return false;
			}

			/*
			if (http_requests[httpc].offset == swift::Size(http_requests[httpc].fdes))
			{
				// TODO: seed for a while.
				fprintf(stderr,"http alive: data delivered to client, quiting\n");
				return false;
			}
			*/

			lastoffset = http_requests[httpc].offset;
			lastcomplete = swift::Complete(http_requests[httpc].fdes);
	    }
		test_time = NOW;

	    return true;
	}
	else
		return true;
}
