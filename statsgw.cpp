/*
 *  statsgw.cpp
 *  HTTP server for showing some DL stats via SwarmPlayer 3000's webUI,
 *  libevent based
 *
 *  Created by Victor Grishchenko, Arno Bakker
 *  Copyright 2010-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#include "swift.h"
#include <event2/http.h>

using namespace swift;

int statsgw_reqs_count = 0;


uint64_t statsgw_last_down;
uint64_t statsgw_last_up;
tint statsgw_last_time = 0;
bool statsgw_quit_process=false;
struct evhttp *statsgw_event;
struct evhttp_bound_socket *statsgw_handle;


const char *top_page = "<!doctype html> \
<html style=\"font-family:Verdana,Arial,Helvetica,sans-serif;font-size:14px;background-color:white;\"> \
<head> \
	<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /> \
	<meta http-equiv=\"cache-control\" content=\"Private\" /> \
	<meta http-equiv=\"Refresh\" content=\"2;url=http://127.0.0.1:6876/webUI\" /> \
	<title>Swift Web Interface</title> \
</head> \
<body style=\"font-family:Verdana,Arial,Helvetica,sans-serif;font-size:14px;background-color:white;padding:20px; margin:20px;\"> \
<div style=\"padding:40;\"> \
<div style=\"width:400; float:left; padding:20px\"> \
<h1 style=\"font-size: 20px;\"> Swift swarms: </h1>";

const char *swarm_page_templ = " \
<h2 style=\"font-size: 18px; padding-top: 10px; padding-left: 20px; margin-bottom: 0px; color:#30bf00;\">Root hash: %s</h2> \
   <ul style=\"padding-left: 40px;\"> \
    <li>Progress:       %d%c \
    <li>Download speed: %d KB/s \
    <li>Upload speed:   %d KB/s \
</ul>";


const char *bottom_page = " \
<button style=\"color:white; background-color:#4f84dc; width:80;height:50; font-size:18px; font-weight:bold; text-shadow: #6374AB 2px 2px 2px;\" \
onClick=\"window.location='http://127.0.0.1:6876/webUI/exit';\">Quit Swift</button> \
</div> \
</div> \
</body> \
</html>";


const char *exit_page = "<!doctype html> \
<html style=\"font-family:Verdana,Arial,Helvetica,sans-serif;font-size:14px;background-color:white;\"> \
<head> \
	<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /> \
	<meta http-equiv=\"cache-control\" content=\"Private\" /> \
	<title>Swift Web Interface</title> \
</head> \
<body style=\"font-family:Verdana,Arial,Helvetica,sans-serif;font-size:14px;background-color:white;padding:20px; margin:20px;\"> \
<div style=\"padding:40;\"> \
<div style=\"width:400; float:left; padding:20px\"> \
<h1 style=\"font-size: 20px;\"> Swift is no longer running. </h1> \
</div> \
</div> \
</body> \
</html>";


static void StatsGwNewRequestCallback (struct evhttp_request *evreq, void *arg);


void StatsExitCallback(struct evhttp_request *evreq)
{
    char contlenstr[1024];
    sprintf(contlenstr,"%i",strlen(exit_page));
    struct evkeyvalq *headers = evhttp_request_get_output_headers(evreq);
    evhttp_add_header(headers, "Connection", "close" );
    evhttp_add_header(headers, "Content-Type", "text/html" );
    evhttp_add_header(headers, "Content-Length", contlenstr );
    evhttp_add_header(headers, "Accept-Ranges", "none" );

    // Construct evbuffer and send via chunked encoding
    struct evbuffer *evb = evbuffer_new();
    int ret = evbuffer_add(evb,exit_page,strlen(exit_page));
    if (ret < 0) {
    	print_error("statsgw: ExitCallback: error evbuffer_add");
        return;
    }

    evhttp_send_reply(evreq, 200, "OK", evb);
    evbuffer_free(evb);
}


bool StatsQuit()
{
    return statsgw_quit_process;
}


void StatsOverviewCallback(struct evhttp_request *evreq)
{
    tint nu = NOW;
    uint64_t down = Channel::global_raw_bytes_down;
    uint64_t up = Channel::global_raw_bytes_up;

    int dspeed = 0, uspeed = 0;
    tint tdiff = (nu - statsgw_last_time)/1000000;
    if (tdiff > 0) {
	dspeed = (int)(((down-statsgw_last_down)/1024) / tdiff);
	uspeed = (int)(((up-statsgw_last_up)/1024) / tdiff);
    }
    //statsgw_last_down = down;
    //statsgw_last_up = up;


    char bodystr[102400];
    strcpy(bodystr,"");
    strcat(bodystr,top_page);

    for( SwarmManager::Iterator it = SwarmManager::GetManager().begin(); it != SwarmManager::GetManager().end(); it++ ) {
        int id = (*it)->Id();
        uint64_t total = (int)swift::Size(id);
        uint64_t down  = (int)swift::Complete(id);
        int perc = (int)((down * 100) / total);

        char roothashhexstr[256];
        sprintf(roothashhexstr,"%s", SwarmID(id).hex().c_str() );

        char templ[1024];
        sprintf(templ,swarm_page_templ,roothashhexstr, perc, '%', dspeed, uspeed );
        strcat(bodystr,templ);
    }

    strcat(bodystr,bottom_page);

    char contlenstr[1024];
    sprintf(contlenstr,"%i",strlen(bodystr));
    struct evkeyvalq *headers = evhttp_request_get_output_headers(evreq);
    evhttp_add_header(headers, "Connection", "close" );
    evhttp_add_header(headers, "Content-Type", "text/html" );
    evhttp_add_header(headers, "Content-Length", contlenstr );
    evhttp_add_header(headers, "Accept-Ranges", "none" );

    // Construct evbuffer and send via chunked encoding
    struct evbuffer *evb = evbuffer_new();
    int ret = evbuffer_add(evb,bodystr,strlen(bodystr));
    if (ret < 0) {
	print_error("statsgw: OverviewCallback: error evbuffer_add");
	return;
    }

    evhttp_send_reply(evreq, 200, "OK", evb);
    evbuffer_free(evb);
}


void StatsGetSpeedCallback(struct evhttp_request *evreq)
{
    if (statsgw_last_time == 0)
    {
 	statsgw_last_time = NOW-1000000;
    }

    tint nu = Channel::Time();
    uint64_t down = Channel::global_raw_bytes_down;
    uint64_t up = Channel::global_raw_bytes_up;

    int dspeed = 0, uspeed = 0;
    tint tdiff = (nu - statsgw_last_time)/1000000;
    if (tdiff > 0) {
        dspeed = (int)(((down-statsgw_last_down)/1024) / tdiff);
        uspeed = (int)(((up-statsgw_last_up)/1024) / tdiff);
    }
    statsgw_last_down = down;
    statsgw_last_up = up;
    statsgw_last_time = nu;

    // Arno: PDD+ wants content speeds too
    double contentdownspeed = 0.0, contentupspeed = 0.0;
    uint32_t nleech=0,nseed=0;
    for( SwarmManager::Iterator it = SwarmManager::GetManager().begin(); it != SwarmManager::GetManager().end(); it++ ) {
        FileTransfer* ft = (*it)->GetTransfer(false);
        if( ft ) {
    		contentdownspeed += ft->GetCurrentSpeed(DDIR_DOWNLOAD);
    		contentupspeed += ft->GetCurrentSpeed(DDIR_UPLOAD);
    		nleech += ft->GetNumLeechers();
    		nseed += ft->GetNumSeeders();
    	}
        // TODO: Are these active leechers and seeders, or potential seeders and leechers? In the latter case these can be retrieved when cached peers are implemented
    }
    int cdownspeed = (int)(contentdownspeed/1024.0);
    int cupspeed = (int)(contentupspeed/1024.0);

    char speedstr[1024];
    sprintf(speedstr,"{\"downspeed\": %d, \"success\": \"true\", \"upspeed\": %d, \"cdownspeed\": %d, \"cupspeed\": %d, \"nleech\": %d, \"nseed\": %d}", dspeed, uspeed, cdownspeed, cupspeed, nleech, nseed );

    char contlenstr[1024];
    sprintf(contlenstr,"%i",strlen(speedstr));
    struct evkeyvalq *headers = evhttp_request_get_output_headers(evreq);
    evhttp_add_header(headers, "Connection", "close" );
    evhttp_add_header(headers, "Content-Type", "application/json" );
    evhttp_add_header(headers, "Content-Length", contlenstr );
    evhttp_add_header(headers, "Accept-Ranges", "none" );

    // Construct evbuffer and send via chunked encoding
    struct evbuffer *evb = evbuffer_new();
    int ret = evbuffer_add(evb,speedstr,strlen(speedstr));
    if (ret < 0) {
        print_error("statsgw: GetSpeedCallback: error evbuffer_add");
        return;
    }

    evhttp_send_reply(evreq, 200, "OK", evb);
    evbuffer_free(evb);
}


void StatsGwNewRequestCallback (struct evhttp_request *evreq, void *arg) {

    dprintf("%s @%i http new request\n",tintstr(),statsgw_reqs_count);
    statsgw_reqs_count++;

    if (evhttp_request_get_command(evreq) != EVHTTP_REQ_GET) {
        return;
    }

    // Parse URI
    const char *uri = evhttp_request_get_uri(evreq);
    //struct evkeyvalq *headers =   evhttp_request_get_input_headers(evreq);
    //const char *contentrangestr =evhttp_find_header(headers,"Content-Range");

    fprintf(stderr,"statsgw: GOT %s\n", uri);

    if (strstr(uri,"get_speed_info") != NULL)
    {
       StatsGetSpeedCallback(evreq);
    }
    else if (!strncmp(uri,"/webUI/exit",strlen("/webUI/exit")) || statsgw_quit_process)
    {
       statsgw_quit_process = true;
       StatsExitCallback(evreq);
    }
    else if (!strncmp(uri,"/webUI",strlen("/webUI")))
    {
       StatsOverviewCallback(evreq);
    }
}


bool InstallStatsGateway (struct event_base *evbase,Address bindaddr) {
   // Arno, 2011-10-04: From libevent's http-server.c example

   /* Create a new evhttp object to handle requests. */
   statsgw_event = evhttp_new(evbase);
   if (!statsgw_event) {
      print_error("statsgw: evhttp_new failed");
      return false;
   }

   /* Install callback for all requests */
   evhttp_set_gencb(statsgw_event, StatsGwNewRequestCallback, NULL);

   /* Now we tell the evhttp what port to listen on */
   statsgw_handle = evhttp_bind_socket_with_handle(statsgw_event, bindaddr.ipv4str(), bindaddr.port());
   if (!statsgw_handle) {
      print_error("statsgw: evhttp_bind_socket_with_handle failed");
      return false;
   }

   return true;
}
