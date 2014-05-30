/*
 *  exttrack.cpp
 *
 *  Implements an external tracker client. Currently uses BT tracker protocol.
 *  If SwarmIDs are not SHA1 hashes they are hashed with a SHA1 MDC to turn
 *  them into an infohash. Only HTTP trackers supported at the moment.
 *
 *  TODO:
 *  - SwarmManager reregistrations
 *  - IETF PPSP tracker protocol
 *
 *  Created by Arno Bakker
 *  Copyright 2013-2016 Vrije Universiteit Amsterdam. All rights reserved.
 */
#include "swift.h"
#include <event2/http.h>
#include <event2/http_struct.h>
#include <sstream>

#define exttrack_debug  true


using namespace swift;


// https://wiki.theory.org/BitTorrentSpecification#peer_id
#define BT_PEER_ID_LENGTH   20 // bytes
#define BT_PEER_ID_PREFIX   "-SW1000-"

#define BT_BENCODE_STRING_SEP       ":"
#define BT_BENCODE_INT_SEP      "e"

#define BT_FAILURE_REASON_DICT_KEY  "failure reason"
#define BT_PEERS_IPv4_DICT_KEY  "5:peers"   // 5: to avoid confusion with a 'peers' list with one 6-byte entry
#define BT_INTERVAL_DICT_KEY    "interval"
#define BT_PEERS_IPv6_DICT_KEY  "6:peers6"

#define BT_INITIAL_REPORT_INTERVAL  30 // seconds


typedef enum {
    BENCODED_INT,
    BENCODED_STRING
} bencoded_type_t;


static int ParseBencodedPeers(struct evbuffer *evb, std::string key, peeraddrs_t *peerlist);
static int ParseBencodedValue(struct evbuffer *evb, struct evbuffer_ptr &startevbp, std::string key, bencoded_type_t valuetype, char **valueptr);

ExternalTrackerClient::ExternalTrackerClient(std::string url) : url_(url), report_last_time_(0), report_interval_(BT_INITIAL_REPORT_INTERVAL), reported_complete_(false)
{
    // Create PeerID
    peerid_ = new uint8_t[BT_PEER_ID_LENGTH];

    int ret = 0;
#ifdef OPENSSL
    strcpy((char *)peerid_,BT_PEER_ID_PREFIX);
    ret = RAND_bytes(&peerid_[strlen(BT_PEER_ID_PREFIX)], BT_PEER_ID_LENGTH-strlen(BT_PEER_ID_PREFIX));
#endif
    if (ret != 1) {
        // Fallback and no OPENSSL option
        char buf[32];
        std::ostringstream oss;
        oss << BT_PEER_ID_PREFIX;
        sprintf(buf,"%05d", rand());
        oss << buf;
        sprintf(buf,"%05d", rand());
        oss << buf;
        sprintf(buf,"%05d", rand());
        oss << buf;
        std::string randstr = oss.str().substr(0,BT_PEER_ID_LENGTH);
        strcpy((char *)peerid_,randstr.c_str());
    }
}

ExternalTrackerClient::~ExternalTrackerClient()
{
    if (peerid_ != NULL)
        delete peerid_;
}


int ExternalTrackerClient::Contact(ContentTransfer *transfer, std::string event, exttrack_peerlist_callback_t callback)
{
    Address myaddr = Channel::BoundAddress(Channel::default_socket());
    std::string q = CreateQuery(transfer,myaddr,event);
    if (q.length() == 0)
        return -1;
    else {
        if (event == EXTTRACK_EVENT_COMPLETED)
            reported_complete_ = true;

        report_last_time_ = NOW;

        ExtTrackCallbackRecord *callbackrec = new ExtTrackCallbackRecord(transfer->td(),callback);
        return HTTPConnect(q,callbackrec);
    }
}

/** IP in myaddr currently unused */
std::string ExternalTrackerClient::CreateQuery(ContentTransfer *transfer, Address myaddr, std::string event)
{
    Sha1Hash infohash;

    // Should be per swarm, now using global upload, just to monitor sharing activity
    uint64_t uploaded = Channel::global_bytes_up;
    uint64_t downloaded = swift::SeqComplete(transfer->td());
    uint64_t left = 0;
    if (transfer->ttype() == FILE_TRANSFER) {
        infohash = transfer->swarm_id().roothash();
        if (downloaded > swift::Size(transfer->td()))
            left = 0;
        else
            left = swift::Size(transfer->td()) - downloaded;

        // "No completed is sent if the file was complete when started. "
        // http://www.bittorrent.org/beps/bep_0003.html
        if (event == EXTTRACK_EVENT_STARTED && left == 0)
            reported_complete_ = true;
    } else {
        SwarmPubKey spubkey = transfer->swarm_id().spubkey();
        infohash = Sha1Hash(spubkey.bits(),spubkey.length());
        left = 0x7fffffffffffffffULL;
    }

    // See
    // http://www.bittorrent.org/beps/bep_0003.html
    // https://wiki.theory.org/BitTorrent_Tracker_Protocol

    char *esc = NULL;
    std::ostringstream oss;

    oss << "info_hash=";
    esc = evhttp_uriencode((const char *)infohash.bytes(),Sha1Hash::SIZE,false);
    if (esc == NULL)
        return "";
    oss << esc;
    free(esc);
    oss << "&";

    oss << "peer_id=";
    esc = evhttp_uriencode((const char *)peerid_,BT_PEER_ID_LENGTH,false);
    if (esc == NULL)
        return "";
    oss << esc;
    free(esc);
    oss << "&";

    // ip= currently unused

    oss << "port=";
    oss << myaddr.port();
    oss << "&";

    oss << "uploaded=";
    oss << uploaded;
    oss << "&";

    oss << "downloaded=";
    oss << downloaded;
    oss << "&";

    oss << "left=";
    oss << left;
    oss << "&";

    // Request compacted peerlist, is most common http://www.bittorrent.org/beps/bep_0023.html
    oss << "compact=1";

    if (event.length() > 0) {
        oss << "&";
        oss << "event=";
        oss << event;
    }

    return oss.str();
}


static void ExternalTrackerClientHTTPResponseCallback(struct evhttp_request *req, void *callbackrecvoid)
{
    if (exttrack_debug)
        fprintf(stderr,"exttrack: Callback: ENTER %p\n", callbackrecvoid);

    ExtTrackCallbackRecord *callbackrec = (ExtTrackCallbackRecord *)callbackrecvoid;
    if (callbackrec == NULL)
        return;
    if (callbackrec->callback_ == NULL)
        return;


    if (req->response_code != HTTP_OK) {
        callbackrec->callback_(callbackrec->td_,"Unexpected HTTP Response Code",0,peeraddrs_t());
        delete callbackrec;
        return;
    }

    // Create a copy of the response, as we do destructive parsing
    struct evbuffer *evb = evhttp_request_get_input_buffer(req);
    size_t copybuflen = evbuffer_get_length(evb);
    char *copybuf = new char[copybuflen];
    int ret = evbuffer_remove(evb,copybuf,copybuflen);
    if (ret < 0) {
        delete copybuf;

        callbackrec->callback_(callbackrec->td_,"Could not read HTTP body",0,peeraddrs_t());
        delete callbackrec;
        return;
    }

    //fprintf(stderr,"exttrack: Raw response <%s>\n", copybuf );


    evb = evbuffer_new();
    evbuffer_add(evb,copybuf,copybuflen);

    // Find "failure reason" string following BT spec
    struct evbuffer_ptr startevbp = evbuffer_search(evb,BT_FAILURE_REASON_DICT_KEY,strlen(BT_FAILURE_REASON_DICT_KEY),NULL);
    if (startevbp.pos != -1) {
        char *valuebytes = NULL;
        std::string errorstr;
        int ret = ParseBencodedValue(evb,startevbp,BT_FAILURE_REASON_DICT_KEY,BENCODED_STRING,&valuebytes);
        if (ret < 0) {
            errorstr = "Error parsing tracker response: failure reason";
        } else {
            errorstr = "Tracker responded: "+std::string(valuebytes);
        }
        callbackrec->callback_(callbackrec->td_,errorstr,0,peeraddrs_t());
        delete callbackrec;

        evbuffer_free(evb);
        delete copybuf;
        free(valuebytes);
        return; // failure case done
    }
    evbuffer_free(evb);

    // If not failure, find tracker report interval
    uint32_t interval=0;

    evb = evbuffer_new();
    evbuffer_add(evb,copybuf,copybuflen);
    startevbp = evbuffer_search(evb,BT_INTERVAL_DICT_KEY,strlen(BT_INTERVAL_DICT_KEY),NULL);
    if (startevbp.pos != -1) {
        char *valuebytes = NULL;
        std::string errorstr="";
        int ret = ParseBencodedValue(evb,startevbp,BT_INTERVAL_DICT_KEY,BENCODED_INT,&valuebytes);
        if (ret < 0) {
            delete copybuf;
            evbuffer_free(evb);

            callbackrec->callback_(callbackrec->td_,"Error parsing tracker response: interval",0,peeraddrs_t());
            delete callbackrec;
            return;
        } else {
            ret = sscanf(valuebytes,"%" PRIu32 "",&interval);
            if (ret != 1) {
                free(valuebytes);
                delete copybuf;
                evbuffer_free(evb);

                callbackrec->callback_(callbackrec->td_,"Error parsing tracker response: interval",0,peeraddrs_t());
                delete callbackrec;
                return;
            }

            free(valuebytes);
            if (exttrack_debug)
                fprintf(stderr,"exttrack: Got interval %" PRIu32 "\n", interval);
        }
    }
    evbuffer_free(evb);


    // If not failure, find peers key whose value is compact IPv4 addresses
    // http://www.bittorrent.org/beps/bep_0023.html
    peeraddrs_t peerlist;

    evb = evbuffer_new();
    evbuffer_add(evb,copybuf,copybuflen);
    ret = ParseBencodedPeers(evb, BT_PEERS_IPv4_DICT_KEY,&peerlist);
    if (ret < 0) {
        delete copybuf;
        evbuffer_free(evb);

        callbackrec->callback_(callbackrec->td_,"Error parsing tracker response: peerlist",interval,peeraddrs_t());
        delete callbackrec;
        return;
    }
    evbuffer_free(evb);

    if (exttrack_debug)
        fprintf(stderr,"btrack: Got " PRISIZET " IPv4 peers\n", peerlist.size());

    // If not failure, find peers key whose value is compact IPv6 addresses
    // http://www.bittorrent.org/beps/bep_0007.html
    evb = evbuffer_new();
    evbuffer_add(evb,copybuf,copybuflen);
    ret = ParseBencodedPeers(evb, BT_PEERS_IPv6_DICT_KEY,&peerlist);
    if (ret < 0) {
        delete copybuf;
        evbuffer_free(evb);

        callbackrec->callback_(callbackrec->td_,"Error parsing tracker response: peerlist",interval,peeraddrs_t());
        delete callbackrec;
        return;
    }
    evbuffer_free(evb);

    if (exttrack_debug)
        fprintf(stderr,"btrack: Got " PRISIZET " peers total\n", peerlist.size());

    // Report success
    callbackrec->callback_(callbackrec->td_,"",interval,peerlist);

    delete copybuf;
    delete callbackrec;
}


int ExternalTrackerClient::HTTPConnect(std::string query,ExtTrackCallbackRecord *callbackrec)
{
    std::string fullurl = url_+"?"+query;

    if (exttrack_debug)
        fprintf(stderr,"exttrack: HTTPConnect: %s\n", fullurl.c_str());

    struct evhttp_uri *evu = evhttp_uri_parse(fullurl.c_str());
    if (evu == NULL)
        return -1;

    int fullpathlen = strlen(evhttp_uri_get_path(evu))+strlen("?")+strlen(evhttp_uri_get_query(evu));
    char *fullpath = new char[fullpathlen+1];
    strcpy(fullpath,evhttp_uri_get_path(evu));
    strcat(fullpath,"?");
    strcat(fullpath,evhttp_uri_get_query(evu));

    //fprintf(stderr,"exttrack: HTTPConnect: Composed fullpath %s\n", fullpath );

    // Create HTTP client
    struct evhttp_connection *cn = evhttp_connection_base_new(Channel::evbase, NULL, evhttp_uri_get_host(evu), evhttp_uri_get_port(evu));
    struct evhttp_request *req = evhttp_request_new(ExternalTrackerClientHTTPResponseCallback, (void *)callbackrec);

    // Make request to server
    evhttp_make_request(cn, req, EVHTTP_REQ_GET, fullpath);
    evhttp_add_header(req->output_headers, "Host", evhttp_uri_get_host(evu));

    delete fullpath;
    evhttp_uri_free(evu);

    return 0;
}


static int ParseBencodedPeers(struct evbuffer *evb, std::string key, peeraddrs_t *peerlist)
{
    struct evbuffer_ptr startevbp = evbuffer_search(evb,key.c_str(),key.length(),NULL);
    if (startevbp.pos != -1) {
        char *valuebytes = NULL;
        std::string errorstr;
        int ret = ParseBencodedValue(evb,startevbp,key,BENCODED_STRING,&valuebytes);
        if (ret < 0)
            return -1;

        int peerlistenclen = ret;
        //fprintf(stderr,"exttrack: Peerlist encoded len %d\n", peerlistenclen );

        // Decompact addresses
        struct evbuffer *evb2 = evbuffer_new();
        evbuffer_add(evb2,valuebytes,peerlistenclen);


        int family=AF_INET;
        int enclen=6;
        if (key == BT_PEERS_IPv6_DICT_KEY) {
            family = AF_INET6;
            enclen = 18;
        }
        for (int i=0; i<peerlistenclen/enclen; i++) {
            // Careful: if PPSPP on the wire encoding changes, we can't use
            // this one anymore.
            Address addr = evbuffer_remove_pexaddr(evb2,family);
            peerlist->push_back(addr);

            if (exttrack_debug)
                fprintf(stderr,"exttrack: Peerlist parsed %d %s\n", i, addr.str().c_str());
        }
        evbuffer_free(evb2);

        return 0;
    } else
        return 0; // Could be attempt to look for IPv6 peers in IPv4 only dict.
}


/** Failure reported, extract string from bencoded dictionary */
static int ParseBencodedValue(struct evbuffer *evb, struct evbuffer_ptr &startevbp, std::string key, bencoded_type_t valuetype, char **valueptr)
{
    //fprintf(stderr,"exttrack: Callback: key %s starts at " PRISIZET "\n", key.c_str(), startevbp.pos );

    size_t pastkeypos = startevbp.pos+key.length();
    if (valuetype == BENCODED_INT)
        pastkeypos++; // skip 'i'

    //fprintf(stderr,"exttrack: Callback: key ends at " PRISIZET "\n", pastkeypos );

    int ret = evbuffer_ptr_set(evb, &startevbp, pastkeypos, EVBUFFER_PTR_SET);
    if (ret < 0)
        return -1;

    std::string separator = BT_BENCODE_STRING_SEP;
    if (valuetype == BENCODED_INT)
        separator = BT_BENCODE_INT_SEP;

    // Find separator to determine string len
    struct evbuffer_ptr endevbp = evbuffer_search(evb,separator.c_str(),strlen(separator.c_str()),&startevbp);
    if (endevbp.pos == -1)
        return -1;

    //fprintf(stderr,"exttrack: Callback: separator at " PRISIZET " key len %d\n", endevbp.pos, key.length() );

    size_t intcstrlen = endevbp.pos - startevbp.pos;

    //fprintf(stderr,"exttrack: Callback: value length " PRISIZET "\n", intcstrlen );

    size_t strpos = endevbp.pos+1;

    //fprintf(stderr,"exttrack: Callback: value starts at " PRISIZET "\n", strpos );

    ret = evbuffer_ptr_set(evb, &startevbp, strpos, EVBUFFER_PTR_SET);
    if (ret < 0)
        return -1;

    // Remove all before
    ret = evbuffer_drain(evb,strpos-1-intcstrlen);
    if (ret < 0)
        return -1;

    char *intcstr = new char[intcstrlen+1];
    intcstr[intcstrlen] = '\0';
    ret = evbuffer_remove(evb,intcstr,intcstrlen);
    if (ret < 0) {
        delete intcstr;
        return -1;
    }


    //fprintf(stderr,"exttrack: Callback: Length value string %s\n", intcstr );

    if (valuetype == BENCODED_INT) {
        *valueptr = intcstr;
        return intcstrlen;
    }
    // For strings carry on

    int slen=0;
    ret = sscanf(intcstr,"%d", &slen);
    delete intcstr;
    if (ret != 1)
        return -1;

    //fprintf(stderr,"exttrack: Callback: Length value int %d\n", slen );

    // Drain colon
    ret = evbuffer_drain(evb,1);
    if (ret < 0)
        return -1;

    // NOTE: actual bytes may also contain '\0', C string is for convenience when it isn't pure binary.
    char *valuecstr = new char[slen+1];
    valuecstr[slen] = '\0';
    ret = evbuffer_remove(evb,valuecstr,slen);
    if (ret < 0) {
        delete valuecstr;
        return -1;
    } else {
        //fprintf(stderr,"exttrack: Callback: Value string <%s>\n", valuecstr );

        *valueptr = valuecstr;
        // do not delete valuecstr;
        return slen;
    }
}


