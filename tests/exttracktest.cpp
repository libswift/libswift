/*
 *  bttracktest.cpp
 *
 *  TODO:
 *  * LiveTransfer
 *
 *
 *  Created by Arno Bakker
 *  Copyright 2009-2016 Vrije Universiteit Amsterdam. All rights reserved.
 *
 */
#include <gtest/gtest.h>
#include "swift.h"

#include <event2/http.h>


using namespace swift;

#define ROOTHASH_PLAINTEXT  "ArnosFileSwarm"


/*
 * Create libevent HTTP server that responds like BT tracker
 */
// A bencoded response from a real BT tracker, returning 16 compact IPv6 addrs
const unsigned char bencoded_peers[] = { 0x64, 0x38, 0x3a, 0x63, 0x6f, 0x6d, 0x70, 0x6c, 0x65, 0x74, 0x65, 0x69, 0x31, 0x36, 0x65, 0x31, \
                                         0x30, 0x3a, 0x69, 0x6e, 0x63, 0x6f, 0x6d, 0x70, 0x6c, 0x65, 0x74, 0x65, 0x69, 0x30, 0x65, 0x38, \
                                         0x3a, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x76, 0x61, 0x6c, 0x69, 0x31, 0x38, 0x30, 0x30, 0x65, 0x35, \
                                         0x3a, 0x70, 0x65, 0x65, 0x72, 0x73, 0x39, 0x36, 0x3a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f,\
                                         0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, \
                                         0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, \
                                         0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, \
                                         0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, \
                                         0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, \
                                         0x01, 0x18, 0x5a, 0x7f, 0x00, 0x00, 0x01, 0x18, 0x5a, 0x65
                                       };

// A bencoded response from a real BT tracker, returning a failure reason
const unsigned char bencoded_failure[] = { 0x64, 0x31, 0x34, 0x3a, 0x66, 0x61, 0x69, 0x6c, 0x75, 0x72, 0x65, 0x20, 0x72, 0x65, 0x61, 0x73, \
                                           0x6f, 0x6e, 0x36, 0x33, 0x3a, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74, 0x65, 0x64, 0x20, 0x64, \
                                           0x6f, 0x77, 0x6e, 0x6c, 0x6f, 0x61, 0x64, 0x20, 0x69, 0x73, 0x20, 0x6e, 0x6f, 0x74, 0x20, 0x61, \
                                           0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x7a, 0x65, 0x64, 0x20, 0x66, 0x6f, 0x72, 0x20, 0x75, 0x73, \
                                           0x65, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x74, 0x68, 0x69, 0x73, 0x20, 0x74, 0x72, 0x61, 0x63, \
                                           0x6b, 0x65, 0x72, 0x2e, 0x65
                                         };

struct evhttp *bttrack_serv_event;
struct evhttp_bound_socket *bttrack_serv_handle;
bool bttrack_serv_infohash_ok=true;


/** Called when tracker server receives HTTP request */
void BTTrackerServerRequestCallback(struct evhttp_request *evreq, void *arg)
{

    ASSERT_EQ(EVHTTP_REQ_GET,evhttp_request_get_command(evreq));

    const struct evhttp_uri *evu = evhttp_request_get_evhttp_uri(evreq);
    const char *pathcstr = evhttp_uri_get_path(evu);

    fprintf(stderr,"bttrackserv: Got GET %s\n", pathcstr);

    struct evkeyvalq qheaders;
    int ret = evhttp_parse_query(evhttp_request_get_uri(evreq),&qheaders);
    ASSERT_EQ(0,ret);

    // Check query string
    const char *infohashcstr =evhttp_find_header(&qheaders,"info_hash");

    char *decoded_infohashcstr = evhttp_uridecode(infohashcstr,0,NULL);
    ASSERT_FALSE(decoded_infohashcstr == NULL);

    Sha1Hash roothash(ROOTHASH_PLAINTEXT,strlen(ROOTHASH_PLAINTEXT));
    ret = memcmp(decoded_infohashcstr,roothash.bytes(),Sha1Hash::SIZE);
    ASSERT_EQ(0,ret);
    free(decoded_infohashcstr);

    // TODO: check other query params

    // Return either failure or set of IPv4 peers
    char *body = (char *)bencoded_peers;
    size_t bodylen = sizeof(bencoded_peers);
    if (!bttrack_serv_infohash_ok) {
        body = (char *)bencoded_failure;
        bodylen = sizeof(bencoded_failure);
    }

    struct evbuffer *evb = evbuffer_new();
    evbuffer_add(evb,body,bodylen);

    char contlenstr[1024];
    sprintf(contlenstr,PRISIZET,bodylen);

    struct evkeyvalq *headers = evhttp_request_get_output_headers(evreq);
    evhttp_add_header(headers, "Connection", "close");
    evhttp_add_header(headers, "Content-Type", "text/plain");
    evhttp_add_header(headers, "Content-Length", contlenstr);
    evhttp_add_header(headers, "Accept-Ranges", "none");

    evhttp_send_reply(evreq, 200, "OK", evb);
    evbuffer_free(evb);
}


bool InstallBTTrackerTestServer(struct event_base *evbase, Address bindaddr)
{
    /* Create a new evhttp object to handle requests. */
    bttrack_serv_event = evhttp_new(evbase);
    if (!bttrack_serv_event) {
        print_error("test: evhttp_new failed");
        return false;
    }

    /* Install callback for all requests */
    evhttp_set_gencb(bttrack_serv_event, BTTrackerServerRequestCallback, NULL);

    /* Now we tell the evhttp what port to listen on */
    bttrack_serv_handle = evhttp_bind_socket_with_handle(bttrack_serv_event, bindaddr.ipstr().c_str(), bindaddr.port());
    if (!bttrack_serv_handle) {
        print_error("test: evhttp_bind_socket_with_handle failed");
        return false;
    }
}



bool tracker_called=false;
bool tracker_response_valid=false;

/** Called by ExternalTrackerClient when results come in from the server */
void tracker_callback(int td, std::string status, uint32_t interval, peeraddrs_t peerlist)
{
    tracker_called = true;

    if (status == "") {
        fprintf(stderr,"test: Status OK int %" PRIu32 " npeers %" PRIu32 "\n", interval, peerlist.size());
        if (interval == 1800 && peerlist.size() == 16)
            tracker_response_valid=true;
    } else {
        fprintf(stderr,"test: Status failed: %s\n", status.c_str());

        int sidx = status.find("Requested download is not authorized for use with this tracker.");
        if (sidx != std::string::npos)
            tracker_response_valid=true;
    }

    // Break event loop entered in test.
    event_base_loopbreak(Channel::evbase);
}


TEST(TBTTrack,FileTransferEncodeRequestResponseOK)
{

    tracker_called = false;
    tracker_response_valid=false;
    bttrack_serv_infohash_ok = true;

    Sha1Hash roothash(ROOTHASH_PLAINTEXT,strlen(ROOTHASH_PLAINTEXT));

    fprintf(stderr,"test: Roothash %s\n", roothash.hex().c_str());

    FileTransfer ft(481, "storage.dat", roothash, true, POPT_CONT_INT_PROT_NONE, 1024, false);

    ExternalTrackerClient bt("http://127.0.0.1:8921/announce");
    bt.Contact(&ft,"started",tracker_callback);

    fprintf(stderr,"swift: Mainloop\n");

    // Enter libevent mainloop
    event_base_dispatch(Channel::evbase);
    // Broken by HTTP reply

    ASSERT_TRUE(tracker_called);
    ASSERT_TRUE(tracker_response_valid);
}


TEST(TBTTrack,FileTransferEncodeRequestResponseFailure)
{

    tracker_called = false;
    tracker_response_valid=false;
    bttrack_serv_infohash_ok = false;

    Sha1Hash roothash(ROOTHASH_PLAINTEXT,strlen(ROOTHASH_PLAINTEXT));

    fprintf(stderr,"test: Roothash %s\n", roothash.hex().c_str());

    FileTransfer ft(481, "storage.dat", roothash, true, POPT_CONT_INT_PROT_NONE, 1024, false);

    ExternalTrackerClient bt("http://127.0.0.1:8921/announce");
    bt.Contact(&ft,"started",tracker_callback);

    fprintf(stderr,"swift: Mainloop\n");

    // Enter libevent mainloop
    event_base_dispatch(Channel::evbase);
    // Broken by HTTP reply

    ASSERT_TRUE(tracker_called);
    ASSERT_TRUE(tracker_response_valid);
}



int main(int argc, char** argv)
{

    swift::LibraryInit();
    Channel::evbase = event_base_new();
    Address bindaddr("0.0.0.0:6234");
    if (swift::Listen(bindaddr)<=0)
        fprintf(stderr,"test: Failed swift::Listen\n");

    // Create tracker server
    InstallBTTrackerTestServer(Channel::evbase, Address("0.0.0.0:8921"));

    testing::InitGoogleTest(&argc, argv);
    Channel::debug_file = stdout;
    int ret = RUN_ALL_TESTS();
    return ret;
}
