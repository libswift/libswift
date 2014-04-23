/*
 *  swift.cpp
 *  swift the multiparty transport protocol
 *
 *  Created by Victor Grishchenko, Arno Bakker, Riccardo Petrocco
 *  Copyright 2009-2016 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include "compat.h"
#include "swift.h"
#include <cfloat>
#include <sstream>
#include <iostream>

#include <event2/http.h>
#include <event2/http_struct.h>

#include "svn-revision.h"
#include "swarmmanager.h"

using namespace swift;


// Local constants
#define RESCAN_DIR_INTERVAL	30 // seconds
#define REPORT_INTERVAL		 1 // 1 second. Use cmdgw_report_interval for larger intervals

// Arno, 2012-09-18: LIVE: Somehow Win32 works better when reading at a slower pace
#ifdef WIN32
#define LIVESOURCE_BUFSIZE    102400
//SIGNPEAKTODO revert to 100K
//#define LIVESOURCE_BUFSIZE    10240
#define LIVESOURCE_INTERVAL    TINT_SEC/2
#else
#define LIVESOURCE_BUFSIZE    102400
#define LIVESOURCE_INTERVAL    TINT_SEC/10
#endif


// Local prototypes
void usage(void)
{
    fprintf(stderr,"Usage:\n");
    fprintf(stderr,"  -h, --hash\tswarm ID of the content (root hash or public key)\n");
    fprintf(stderr,"  -f, --file\tname of file to use (root hash by default)\n");
    fprintf(stderr,"  -d, --dir\tname of directory to scan and seed\n");
    fprintf(stderr,"  -l, --listen\t[ip:|host:]port to listen to (default: random). MUST set for IPv6\n");
    fprintf(stderr,"  -t, --tracker\t[ip:|host:]port of the tracker or URL (default: none). IPv6 between [] cf. RFC2732\n");
    fprintf(stderr,"  -D, --debug\tfile name for debugging logs (default: stdout)\n");
    fprintf(stderr,"  -B\tdebugging logs to stdout (win32 hack)\n");
    fprintf(stderr,"  -p, --progress\treport transfer progress\n");
    fprintf(stderr,"  -g, --httpgw\t[ip:|host:]port to bind HTTP content gateway to (no default)\n");
    fprintf(stderr,"  -s, --statsgw\t[ip:|host:]port to bind HTTP stats listen socket to (no default)\n");
    fprintf(stderr,"  -c, --cmdgw\t[ip:|host:]port to bind CMD listen socket to (no default)\n");
    fprintf(stderr,"  -C, --cmdgwint\tcmdgw report interval in seconds\n");
    fprintf(stderr,"  -o, --destdir\tdirectory for saving data (default: none)\n");
    fprintf(stderr,"  -u, --uprate\tupload rate limit in KiB/s (default: unlimited)\n");
    fprintf(stderr,"  -y, --downrate\tdownload rate limit in KiB/s (default: unlimited)\n");
    fprintf(stderr,"  -w, --wait\tlimit running time, e.g. 1[DHMs] (default: infinite with -l, -g)\n");
    fprintf(stderr,"  -H, --checkpoint\tcreate checkpoint of file when complete for fast restart\n");
    fprintf(stderr,"  -z, --chunksize\tchunk size in bytes (default: %d)\n", SWIFT_DEFAULT_CHUNK_SIZE);
    fprintf(stderr,"  -m, --printurl\tcompose URL from tracker, file and chunksize\n");
    fprintf(stderr,"  -r, --urlfile\tfile to write URL to for --printurl\n");
    fprintf(stderr,"  -M, --multifile\tcreate multi-file spec with given files\n");
    fprintf(stderr,"  -e, --zerosdir\tdirectory with checkpointed content to serve from with zero state\n");
    fprintf(stderr,"  -i, --source\tlive source input (URL or filename or - for stdin)\n");
    fprintf(stderr,"  -k, --live\tperform live download, use with -t and -h\n");
    fprintf(stderr,"  -r filename\t to save URL to\n");
    fprintf(stderr,"  -1 filename-in-hex\ta win32 workaround for non UTF-16 popen\n");
    fprintf(stderr,"  -2 urlfilename-in-hex\ta win32 workaround for non UTF-16 popen\n");
    fprintf(stderr,"  -3 zerosdir-in-hex\ta win32 workaround for non UTF-16 popen\n");
    fprintf(stderr,"  -T time-out\tin seconds for slow zero state connections\n");
    fprintf(stderr,"  -G GTest mode\n");
    fprintf(stderr,"  -K live keypair filename\n");
    fprintf(stderr,"  -P live checkpoint filename\n");
    fprintf(stderr,"  -S content integrity protection method\n");
    fprintf(stderr,"  -a live signature algorithm\n");
    fprintf(stderr,"  -W live discard window in chunks\n");
    fprintf(stderr,"  -I live source address (used with ext tracker)\n");
}
#define quit(...) {fprintf(stderr,__VA_ARGS__); exit(1); }
int HandleSwiftSwarm(std::string filename, SwarmID &swarmid, std::string trackerurl, Address srcaddr, bool printurl, bool livestream, std::string urlfilename, double *maxspeed);
int OpenSwiftSwarm(std::string filename, SwarmID &swarmid, std::string trackerurl, Address srcaddr, bool force_check_diskvshash, uint32_t chunk_size, bool livestream, bool activate);
int OpenSwiftDirectory(std::string dirname, std::string trackerurl, bool force_check_diskvshash, uint32_t chunk_size, bool activate);
// SIGNPEAKTODO replace swarmid with generic swarm ID
int PrintURL(int td,std::string trackerurl,uint32_t chunk_size,std::string urlfilename);
void HandleLiveSource(std::string livesource_input, std::string filename, std::string keypairfilename, uint32_t chunk_size, std::string urlfilename);
void AttemptCheckpoint();

void ReportCallback(int fd, short event, void *arg);
void EndCallback(int fd, short event, void *arg);
void RescanDirCallback(int fd, short event, void *arg);
int CreateMultifileSpec(std::string specfilename, int argc, char *argv[], int argidx);

// Live stuff
void LiveSourceAttemptCreate();
void LiveSourceFileTimerCallback(int fd, short event, void *arg);
void LiveSourceHTTPResponseCallback(struct evhttp_request *req, void *arg);
void LiveSourceHTTPDownloadChunkCallback(struct evhttp_request *req, void *arg);

// Gateway stuff
bool InstallHTTPGateway( struct event_base *evbase,Address bindaddr, popt_cont_int_prot_t cipm, uint64_t disc_wnd, uint32_t chunk_size, double *maxspeed, std::string storage_dir, int32_t vod_step, int32_t min_prebuf);
bool InstallCmdGateway (struct event_base *evbase,Address cmdaddr,Address httpaddr,popt_cont_int_prot_t cipm, uint64_t disc_wnd);
bool HTTPIsSending();
#ifndef SWIFTGTEST
bool InstallStatsGateway(struct event_base *evbase,Address addr);
bool StatsQuit();
#endif
void CmdGwUpdateDLStatesCallback();

// Global variables
struct event evreport, evrescan, evend, evlivesource;
int single_td = -1;
bool file_enable_checkpoint = false;
bool file_checkpointed = false;
bool report_progress = false;
bool quiet=false;
bool exitoncomplete=false;
bool httpgw_enabled=false,cmdgw_enabled=false;
// Gertjan fix
bool do_nat_test = false;
bool generate_multifile=false;

std::string scan_dirname="";
uint32_t chunk_size = SWIFT_DEFAULT_CHUNK_SIZE;
std::string trackerurl;

// LIVE
std::string livesource_input = "";
LiveTransfer *livesource_lt = NULL;
FILE *livesource_filep=NULL;
// Arno, 2013-05-13: fread blocks till requested number of items is read, want non-blocking
int livesource_fd=-1;
struct evbuffer *livesource_evb = NULL;
uint64_t livesource_disc_wnd=POPT_LIVE_DISC_WND_ALL;
std::string livesource_checkpoint_filename = "";
bool livesource_isfile=false;
popt_cont_int_prot_t swarm_cipm=POPT_CONT_INT_PROT_MERKLE;
popt_live_sig_alg_t livesource_sigalg=DEFAULT_LIVE_SIG_ALG;

int64_t cmdgw_report_counter=0;
int64_t cmdgw_report_interval=REPORT_INTERVAL; // seconds


// UNICODE: TODO, convert to std::string carrying UTF-8 arguments. Problem is
// a string based getopt_long type parser.
int utf8main (int argc, char** argv)
{
    static struct option long_options[] =
    {
        {"hash",    required_argument, 0, 'h'},
        {"file",    required_argument, 0, 'f'},
        {"dir",     required_argument, 0, 'd'}, // SEEDDIR reuse
        {"listen",  required_argument, 0, 'l'},
        {"tracker", required_argument, 0, 't'},
        {"debug",   no_argument, 0, 'D'},
        {"progress",no_argument, 0, 'p'},
        {"httpgw",  required_argument, 0, 'g'},
        {"wait",    optional_argument, 0, 'w'},
        {"nat-test",no_argument, 0, 'N'},
        {"statsgw", required_argument, 0, 's'}, // SWIFTPROC
        {"cmdgw",   required_argument, 0, 'c'}, // SWIFTPROC
        {"destdir", required_argument, 0, 'o'}, // SWIFTPROC
        {"uprate",  required_argument, 0, 'u'}, // RATELIMIT
        {"downrate",required_argument, 0, 'y'}, // RATELIMIT
        {"checkpoint",no_argument, 0, 'H'},
        {"chunksize",required_argument, 0, 'z'}, // CHUNKSIZE
        {"printurl", no_argument, 0, 'm'},
        {"urlfile",  required_argument, 0, 'r'},  // should be optional arg to printurl, but win32 getopt don't grok
        {"multifile",required_argument, 0, 'M'}, // MULTIFILE
        {"zerosdir",required_argument, 0, 'e'},  // ZEROSTATE
        {"dummy",no_argument, 0, 'j'},  // WIN32
        {"source",required_argument, 0, 'i'}, // LIVE
        {"live",no_argument, 0, 'k'}, // LIVE
        {"cmdgwint",required_argument, 0, 'C'}, // SWIFTPROC
        {"zerostimeout",required_argument, 0, 'T'},  // ZEROSTATE
        {"test",no_argument, 0, 'G'},  // Less command line testing for GTest usage
        {"lcf",required_argument, 0, 'P'}, // LIVECHECKPOINT
        {"kpf",required_argument, 0, 'K'}, // UMT
        {"cipm",required_argument, 0, 'S'}, // PPSP
        {"lsa",required_argument, 0, 'a'}, // PPSP
        {"ldw",required_argument, 0, 'W'}, // PPSP
        {"ia",required_argument, 0, 'I'}, // EXTTRACK
        {0, 0, 0, 0}
    };

    SwarmID swarmid=SwarmID::NOSWARMID;
    std::string filename = "",destdir = "", zerostatedir="", urlfilename="", keypairfilename="";
    bool printurl=false, livestream=false, gtesting=false;
    Address bindaddr;
    Address httpaddr;
    Address statsaddr;
    Address cmdaddr;
    Address optaddr;
    Address srcaddr;
    tint wait_time = 0;
    double maxspeed[2] = {DBL_MAX,DBL_MAX};
    tint zerostimeout = TINT_NEVER;


    LibraryInit();
    Channel::evbase = event_base_new();

    std::string optargstr;
    int c,n;
    while ( -1 != (c = getopt_long (argc, argv, ":h:f:d:l:t:D:L:pg:s:c:o:u:y:z:w:BNHmM:e:r:ji:kC:1:2:3:T:GW:P:K:S:a:I:", long_options, 0)) ) {
        switch (c) {
            case 'h':
                optargstr = optarg;
                swarmid = SwarmID(optargstr);
                if (swarmid==SwarmID::NOSWARMID)
                    quit("SwarmID must be in hex symbols\n");
                break;
            case 'f':
                filename = strdup(optarg);
                break;
            case 'd':
                scan_dirname = strdup(optarg);
                break;
            case 'l':
                bindaddr = Address(optarg);
                if (bindaddr==Address())
                    quit("address must be hostname:port, ip:port or just port\n");
                wait_time = TINT_NEVER;
                break;
            case 't':
                if (strncmp(optarg,"http:",strlen("http:")) && strncmp(optarg,"https:",strlen("https:")))
                {
                    // swift tracker
                    optaddr = Address(optarg);
                    if (optaddr==Address())
                        quit("address must be hostname:port, ip:port or just port\n");

                    trackerurl = std::string(SWIFT_URI_SCHEME);
                    trackerurl += "://";
                    trackerurl += strdup(optarg);
                }
                else // External tracker.
                    trackerurl = strdup(optarg);
                break;
            case 'D':
                Channel::debug_file = optarg ? fopen_utf8(optarg,"a") : stderr;
                break;
            case 'L':
                Channel::debug_ledbat = optarg ? fopen_utf8(optarg,"a") : stderr;
                break;
            // Arno hack: get opt diff Win32 doesn't allow -D without arg
            case 'B':
                fprintf(stderr,"SETTING DEBUG TO STDOUT\n");
                Channel::debug_file = stderr;
                break;
            case 'p':
                report_progress = true;
                break;
            case 'g':
                httpgw_enabled = true;
                httpaddr = Address(optarg);
                wait_time = TINT_NEVER; // seed
                break;
            case 'w':
                if (optarg) {
                    char unit = 'u';
                    if (sscanf(optarg,"%" PRIi64 "%c",&wait_time,&unit)!=2)
                        quit("time format: 1234[umsMHD], e.g. 1M = one minute\n");

                    switch (unit) {
                        case 'D': wait_time *= 24;
                        case 'H': wait_time *= 60;
                        case 'M': wait_time *= 60;
                        case 's': wait_time *= 1000;
                        case 'm': wait_time *= 1000;
                        case 'u': break;
                        default:  quit("time format: 1234[umsMHD], e.g. 1D = one day\n");
                    }
                } else
                    wait_time = TINT_NEVER;
                break;
            case 'N': // Gertjan fix
                do_nat_test = true;
                break;
            case 's': // SWIFTPROC
                statsaddr = Address(optarg);
                if (statsaddr==Address())
                    quit("address must be hostname:port, ip:port or just port\n");
                break;
            case 'c': // SWIFTPROC
                cmdgw_enabled = true;
                cmdaddr = Address(optarg);
                if (cmdaddr==Address())
                    quit("address must be hostname:port, ip:port or just port\n");
                wait_time = TINT_NEVER; // seed
                break;
            case 'o': // SWIFTPROC
                destdir = strdup(optarg); // UNICODE
                break;
            case 'u': // RATELIMIT
                n = sscanf(optarg,"%lf",&maxspeed[DDIR_UPLOAD]);
                if (n != 1)
                    quit("uprate must be KiB/s as float\n");
                maxspeed[DDIR_UPLOAD] *= 1024.0;
                break;
            case 'y': // RATELIMIT
                n = sscanf(optarg,"%lf",&maxspeed[DDIR_DOWNLOAD]);
                if (n != 1)
                    quit("downrate must be KiB/s as float\n");
                maxspeed[DDIR_DOWNLOAD] *= 1024.0;
                break;
            case 'H': //CHECKPOINT
                file_enable_checkpoint = true;
                break;
            case 'z': // CHUNKSIZE
                n = sscanf(optarg,"%i",&chunk_size);
                if (n != 1)
                    quit("chunk size must be bytes as int\n");
                break;
            case 'm': // printurl
                printurl = true;
                quiet = true;
                wait_time = 0;
                break;
            case 'r':
               urlfilename = strdup(optarg);
               break;
            case 'M': // MULTIFILE
                filename = strdup(optarg);
                generate_multifile = true;
                break;
            case 'e': // ZEROSTATE
                zerostatedir = strdup(optarg); // UNICODE
                wait_time = TINT_NEVER; // seed
                break;
            case 'j': // WIN32
                break;
            case 'i': // LIVE
                livesource_input = strdup(optarg);
                if (swarm_cipm == POPT_CONT_INT_PROT_MERKLE)
                    swarm_cipm = POPT_CONT_INT_PROT_UNIFIED_MERKLE;
                wait_time = TINT_NEVER;
                break;
            case 'k': // LIVE
                livestream = true;
                if (swarm_cipm == POPT_CONT_INT_PROT_MERKLE)
                    swarm_cipm = POPT_CONT_INT_PROT_UNIFIED_MERKLE;
                break;
            case 'C': // SWIFTPROC
                if (sscanf(optarg,"%" SCNi64 "",&cmdgw_report_interval)!=1)
                    quit("report interval must be int\n");
                break;
            case '1': // SWIFTPROCUNICODE
                // Swift on Windows expects command line arguments as UTF-16.
                // When swift is run with Python's popen, however, popen
                // doesn't allow us to pass params in UTF-16, hence workaround.
                // Format = hex encoded UTF-8
                filename = hex2bin(strdup(optarg));
                break;
            case '2': // SWIFTPROCUNICODE
                urlfilename = hex2bin(strdup(optarg));
                break;
            case '3': // ZEROSTATE // SWIFTPROCUNICODE
                zerostatedir = hex2bin(strdup(optarg));
                break;
            case 'G': //
                gtesting = true;
                break;
            case 'W': // PPSP
                if (sscanf(optarg,"%" SCNx64 ,&livesource_disc_wnd)!=1)
                    quit("report interval must be int\n");
                break;
            case 'P':
                livesource_checkpoint_filename = strdup(optarg);
                break;
            case 'K':
                keypairfilename = strdup(optarg);
                break;
            case 'S':
        	if (!strcmp(optarg,"0"))
		    swarm_cipm = POPT_CONT_INT_PROT_NONE;
        	else if (!strcmp(optarg,"1"))
		    swarm_cipm = POPT_CONT_INT_PROT_MERKLE;
        	else if (!strcmp(optarg,"2"))
		    quit("Live content integrity protection via SIGNALL not supported.")
        	else if (!strcmp(optarg,"3"))
		    swarm_cipm = POPT_CONT_INT_PROT_UNIFIED_MERKLE;
        	else
        	    quit("Live content integrity protection must be 0,1,2,3")
        	break;
            case 'a':
        	if (!strcmp(optarg,"5"))
		    livesource_sigalg = POPT_LIVE_SIG_ALG_RSASHA1;
        	else if (!strcmp(optarg,"13"))
        	    livesource_sigalg = POPT_LIVE_SIG_ALG_ECDSAP256SHA256;
        	else if (!strcmp(optarg,"14"))
        	    livesource_sigalg = POPT_LIVE_SIG_ALG_ECDSAP384SHA384;
        	else
        	    quit("Live signature algorithm must be 5, 13 or 14")
        	break;
            case 'I':
                srcaddr = Address(optarg);
                if (srcaddr==Address())
                    quit("address must be hostname:port, ip:port or just port\n");
                break;
            case 'T': // ZEROSTATE
                double t=0.0;
                n = sscanf(optarg,"%lf",&t);
                if (n != 1)
                    quit("zerostimeout must be seconds as float\n");
                zerostimeout = t * TINT_SEC;
                break;
        }

    }   // arguments parsed


    // Change dir to destdir, if set, or to tempdir if HTTPGW
    if (destdir == "") {
        if (httpgw_enabled) {
            std::string dd = gettmpdir_utf8();
            chdir_utf8(dd);
        }
    }
    else
        chdir_utf8(destdir);

    if (httpgw_enabled)
        fprintf(stderr,"CWD %s\n",getcwd_utf8().c_str() );

    if (bindaddr!=Address()) { // seeding
        if (Listen(bindaddr)<=0)
            quit("cant listen to %s\n",bindaddr.str().c_str())
    } else if (trackerurl != "" || httpgw_enabled || cmdgw_enabled) { // leeching
        evutil_socket_t sock = INVALID_SOCKET;
        for (int i=0; i<=10; i++) {
            bindaddr = Address((uint32_t)INADDR_ANY,0);
            sock = Listen(bindaddr);
            if (sock>0)
                break;
            if (i==10)
                quit("cant listen on %s\n",bindaddr.str().c_str());
        }
        if (!quiet)
            fprintf(stderr,"swift: My listen port is %d\n", BoundAddress(sock).port() );
    }

    if (trackerurl != "" && !printurl)
        SetTracker(trackerurl);

    if (livesource_input == "" && filename != "")
    {
	int ret = file_exists_utf8(filename);
	if (ret < 1)
	    quit( "file does not exist: %s\n", filename.c_str() );
    }

#ifndef OPENSSL
    swarm_cipm = POPT_CONT_INT_PROT_NONE;
#endif

    // For easy testing: read swift URL from file
    if (livestream && urlfilename != "")
    {
	char urldata[POPT_MAX_SWARMID_SIZE*2];
	FILE *fp = fopen_utf8(urlfilename.c_str(),"rb");
        if (fp == NULL)
        {
            quit("Could not open URL file\n");
        }
        else
        {
            char *ptr = fgets(urldata,POPT_MAX_SWARMID_SIZE*2,fp);
            if (ptr == NULL)
        	quit("Error reading URL from file");
	    fclose(fp);
	    std::string urlstr(urldata);

	    parseduri_t puri;
	    if (!swift::ParseURI(urlstr,puri))
		quit("Error reading URL from file: parse error");
	    std::string swarmidhexstr = puri["swarmidhex"];
	    swarmid = SwarmID(swarmidhexstr);

            fprintf(stderr,"swift: Read swarmid: %s\n", swarmid.hex().c_str() );
        }
    }

    if (httpgw_enabled)
        InstallHTTPGateway(Channel::evbase,httpaddr,swarm_cipm,livesource_disc_wnd,chunk_size,maxspeed,"",-1,-1);
    if (cmdgw_enabled)
        InstallCmdGateway(Channel::evbase,cmdaddr,httpaddr,swarm_cipm,livesource_disc_wnd);

    // Arno, 2013-01-10: Cannot use while GTesting because statsgw is not part of
    // libswift, but considered part of an app on top.
#ifndef SWIFTGTEST
    // TRIALM36: Allow browser to retrieve stats via AJAX and as HTML page
    if (statsaddr != Address())
        InstallStatsGateway(Channel::evbase,statsaddr);
#endif
  
    // ZEROSTATE
    ZeroState *zs = ZeroState::GetInstance();
    zs->SetContentDir(zerostatedir);
    zs->SetConnectTimeout(zerostimeout);

    if ((!cmdgw_enabled || gtesting) && livesource_input == "" && zerostatedir == "")
    {
        // Seed file or dir, or create multi-spec
        int ret = -1;
        if (!generate_multifile)
        {
            if (filename != "" || !(swarmid == SwarmID::NOSWARMID)) {

                // Single file
                ret = HandleSwiftSwarm(filename,swarmid,trackerurl,srcaddr,printurl,livestream,urlfilename,maxspeed);
            }
            else if (scan_dirname != "")
            {
        	// Arno, 2012-10-01:
        	// You can use this to make a directory of swarms available.
        	// The swarms must be complete and checkpointed on disk,
        	// otherwise the main thread will spend time hashchecking them.
        	// You can open the swarms activated (fully loaded in memory)
        	// or deactivated (will be loaded into memory when peers
        	// arrive). When started deactivated but no checkpoint or
        	// incomplete, then the swarm will be activated and hashchecked.
        	//
                ret = OpenSwiftDirectory(scan_dirname,"",false,chunk_size,false); // activate = false
            }
            else
                ret = -1;
        }
        else
        {
            // MULTIFILE
            // Generate multi-file spec
            ret = CreateMultifileSpec(filename,argc,argv,optind); //optind is global var points to first non-opt cmd line argument
            if (ret < 0)
                quit("Cannot generate multi-file spec")
            else
                // Calc roothash
                ret = HandleSwiftSwarm(filename,swarmid,trackerurl,srcaddr,printurl,false,urlfilename,maxspeed);
        }

        // For testing
        if (httpgw_enabled || zerostatedir != "")
            ret = 0;

        // No file/dir nor HTTP gateway nor CMD gateway, will never know what to swarm
        if (ret == -1) {
            usage();
            quit("Don't understand command line parameters.\n")
        }
    }
    else if (livesource_input != "")
    {
        // LIVE
        // Act as live source
        HandleLiveSource(livesource_input,filename,keypairfilename,chunk_size,urlfilename);
    }
    else if (!cmdgw_enabled && !httpgw_enabled && zerostatedir == "")
        quit("Not client, not live server, not a gateway, not zero state seeder?");


    // Arno, 2012-01-04: Allow download and quit mode
    if (single_td != -1 && !(swarmid == SwarmID::NOSWARMID) && wait_time == 0) {
        wait_time = TINT_NEVER;
        exitoncomplete = true;
    }

    // End after wait_time
    if ((long)wait_time > 0) {
        evtimer_assign(&evend, Channel::evbase, EndCallback, NULL);
        evtimer_add(&evend, tint2tv(wait_time));
    }

    // Enter mainloop, if daemonizing
    if (wait_time == TINT_NEVER || (long)wait_time > 0) {
        // Arno: always, for statsgw, rate control, etc.
        evtimer_assign(&evreport, Channel::evbase, ReportCallback, NULL);
        evtimer_add(&evreport, tint2tv(REPORT_INTERVAL*TINT_SEC));

        if (scan_dirname != "") {
            evtimer_assign(&evrescan, Channel::evbase, RescanDirCallback, NULL);
            evtimer_add(&evrescan, tint2tv(RESCAN_DIR_INTERVAL*TINT_SEC));
        }

        fprintf(stderr,"swift: Mainloop\n");
        // Enter libevent mainloop
        event_base_dispatch(Channel::evbase);

        // event_base_loopexit() was called, shutting down
    }

    AttemptCheckpoint();

    // Arno, 2012-01-03: Close all transfers
    tdlist_t tds = GetTransferDescriptors();
    tdlist_t::iterator iter;
    for (iter = tds.begin(); iter != tds.end(); iter++ )
	swift::Close(*iter);

    if (Channel::debug_file && Channel::debug_file != stderr)
        fclose(Channel::debug_file);

    swift::Shutdown();

    return 0;
}


int HandleSwiftSwarm(std::string filename, SwarmID &swarmid, std::string trackerurl, Address srcaddr, bool printurl, bool livestream, std::string urlfilename, double *maxspeed)
{
    if (!(swarmid == SwarmID::NOSWARMID) && filename == "")
    {
	filename = swarmid.tofilename();
    }

    single_td = OpenSwiftSwarm(filename,swarmid,trackerurl,srcaddr,false,chunk_size,livestream,true); //activate always
    if (single_td < 0)
        quit("swift: cannot open swarm with %s",filename.c_str());
    if (printurl)
    {
	if (swift::Complete(single_td) == 0)
	    quit("cannot open empty file %s",filename.c_str());

	int ret = PrintURL(single_td,trackerurl,chunk_size,urlfilename);
	if (ret < 0)
	    return -1;

        // Arno, 2012-01-04: LivingLab: Create checkpoint such that content
        // can be copied to scanned dir and quickly loaded
        swift::Checkpoint(single_td);
    }
    else
    {
        printf("Root hash: %s\n", swift::GetSwarmID(single_td).hex().c_str());
        fflush(stdout); // For testing
    }

    // RATELIMIT
    swift::SetMaxSpeed(single_td,DDIR_DOWNLOAD,maxspeed[DDIR_DOWNLOAD]);
    swift::SetMaxSpeed(single_td,DDIR_UPLOAD,maxspeed[DDIR_UPLOAD]);

    return single_td;
}


int OpenSwiftSwarm(std::string filename, SwarmID &swarmid, std::string trackerurl, Address srcaddr, bool force_check_diskvshash, uint32_t chunk_size, bool livestream, bool activate)
{
    if (!quiet)
	fprintf(stderr,"swift: parsedir: Opening %s\n", filename.c_str());

    // When called by OpenSwiftDirectory, the swarm may already be open. In
    // that case, swift::Open() cheaply returns the same transfer descriptor.

    // Client mode: regular or live download
    int td = -1;
    if (!livestream)
        td = swift::Open(filename,swarmid,trackerurl,force_check_diskvshash,swarm_cipm,false,activate,chunk_size);
    else
        td = swift::LiveOpen(filename,swarmid,trackerurl,srcaddr,swarm_cipm,livesource_disc_wnd,chunk_size);
    return td;
}


int OpenSwiftDirectory(std::string dirname, std::string trackerurl, bool force_check_diskvshash, uint32_t chunk_size, bool activate)
{
    DirEntry *de = opendir_utf8(dirname);
    if (de == NULL)
        return -1;

    while(1)
    {
        if (!(de->isdir_ || de->filename_.rfind(".mhash") != std::string::npos || de->filename_.rfind(".mbinmap") != std::string::npos))
        {
            // Not dir, or metafile
            std::string path = dirname;
            path.append(FILE_SEP);
            path.append(de->filename_);
            SwarmID swarmid = SwarmID::NOSWARMID;
            int td = OpenSwiftSwarm(path,swarmid,trackerurl,Address(),force_check_diskvshash,chunk_size,false,activate);
            if (td >= 0) // Support case where dir of content has not been pre-hashchecked and checkpointed
                Checkpoint(td);
        }

        DirEntry *newde = readdir_utf8(de);
        delete de;
        de = newde;
        if (de == NULL)
            break;
    }
    return 1;
}



int CleanSwiftDirectory(std::string dirname)
{
    tdlist_t delset;
    tdlist_t tds = swift::GetTransferDescriptors();
    tdlist_t::iterator iter;
    for (iter = tds.begin(); iter != tds.end(); iter++)
    {
	int td = *iter;
	std::string filename = swift::GetOSPathName(td);
        fprintf(stderr,"swift: clean: Checking %s\n", filename.c_str() );
        int res = file_exists_utf8( filename );
        if (res == 0) {
            fprintf(stderr,"swift: clean: Missing %s\n", filename.c_str() );
            delset.push_back(td);
	}
    }
    for (iter=delset.begin(); iter!=delset.end(); iter++)
    {
	int td = *iter;
        fprintf(stderr,"swift: clean: Deleting transfer %d\n", td );
        swift::Close(td);
    }

    return 1;
}


int PrintURL(int td,std::string trackerurl,uint32_t chunk_size,std::string urlfilename)
{
    FILE *fp = stdout;
    if (urlfilename != "")
    {
        fp = fopen_utf8(urlfilename.c_str(),"wb");
        if (!fp)
        {
            print_error("cannot open file to write tswift URL to");
            quit("cannot open URL file %s",urlfilename.c_str());
        }
    }

    std::ostringstream oss;
    if (trackerurl == "")
    {
	oss << SWIFT_URI_SCHEME;
	oss << ":";
    }
    else
	oss << trackerurl;
    oss << "/" << swift::GetSwarmID(td).hex();
    if (chunk_size != SWIFT_DEFAULT_CHUNK_SIZE)
       oss << "$" << chunk_size;
    oss << "\n";

    std::stringbuf *pbuf=oss.rdbuf();
    if (pbuf == NULL)
    {
        print_error("cannot create URL");
        return -1;
    }
    int ret = 0;
    ret = fprintf(fp,"%s", pbuf->str().c_str());
    if (ret <0)
        print_error("cannot write URL");

    if (fp != stdout)
        fclose(fp);

    return ret;
}



void OpenSSLGenerateCallback(int p)
{
    fprintf(stderr,"GenKey %d ", p);
}


void HandleLiveSource(std::string livesource_input, std::string filename, std::string keypairfilename, uint32_t chunk_size, std::string urlfilename)
{
    // LIVE
    // Server mode: read from http source or pipe or file
    livesource_evb = evbuffer_new();


    // Try reading keypair from filename, if not generate one, and save.
    KeyPair *keypairptr=NULL;
    if (keypairfilename != "")
    {
	keypairptr = KeyPair::ReadPrivateKey(keypairfilename);
	if (keypairptr == NULL)
	    fprintf(stderr,"swift: Could not read keypair from filename, creating new one\n");
    }
    if (keypairptr == NULL)
    {
	keypairptr = KeyPair::Generate(livesource_sigalg, SWIFT_RSA_DEFAULT_KEYSIZE, OpenSSLGenerateCallback );
	fprintf(stderr,"swift: siglen is %" PRIu16 "\n", keypairptr->GetSigSizeInBytes() );
    }
    if (keypairfilename != "")
    {
	int ret = keypairptr->WritePrivateKey(keypairfilename);
	if (ret < 0)
	    fprintf(stderr,"swift: Could not write keypair to filename, continuing\n");
    }


    // Determine input
    std::string httpscheme = "http:";
    std::string pipescheme = "pipe:";
    if (livesource_input.substr(0,httpscheme.length()) != httpscheme)
    {
        // Source is file or pipe
        if (livesource_input == "-")
            livesource_fd = 0; // aka read from shell pipe

        else if (livesource_input.substr(0,pipescheme.length()) == pipescheme) {
            // Source is program output
            std::string program = livesource_input.substr(pipescheme.length());
#ifdef WIN32
            livesource_filep = _popen( program.c_str(), "rb" );
            if (livesource_filep == NULL)
                quit("live: file: popen failed" );

            fprintf(stderr,"live: pipe: Reading from %s\n", program.c_str() );
#else
            quit("live: pipe as source not yet implemented on non-win32");
#endif
        }
        else {
            // Source is file
            // Arno, 2013-05-13: fread blocks till requested number of items is read, want non-blocking
            livesource_fd = open_utf8(livesource_input.c_str(),ROOPENFLAGS,S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

            if (livesource_fd == -1)
                quit("live: Could not open source input");
            livesource_isfile = true;
        }

        if (filename == "")
            filename = "storage.dat";

        // Create swarm
        livesource_lt = swift::LiveCreate(filename,*keypairptr,livesource_checkpoint_filename,swarm_cipm,livesource_disc_wnd,SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN,chunk_size); // SIGNPEAKTODO

        // Periodically create chunks by reading from source
        evtimer_assign(&evlivesource, Channel::evbase, LiveSourceFileTimerCallback, NULL);
        evtimer_add(&evlivesource, tint2tv(TINT_SEC));
    }
    else
    {
        // Source is HTTP server
        std::string httpservname,httppath;
        int httpport=80;

        std::string httpprefix= "http://";
        std::string schemeless = livesource_input.substr(httpprefix.length());
        int sidx = schemeless.find("/");
        if (sidx == -1)
            quit("No path in live source input URL");
        httppath = schemeless.substr(sidx);
        std::string server = schemeless.substr(0,sidx);
        sidx = server.find(":");
        if (sidx != -1)
        {
            httpservname = server.substr(0,sidx);
            std::string portstr = server.substr(sidx+1);
            std::istringstream(portstr) >> httpport;
        }
        else
            httpservname = server;

        fprintf(stderr,"live: http: Reading from serv %s port %d path %s\n", httpservname.c_str(), httpport, httppath.c_str() );

        // Create swarm
        livesource_lt = swift::LiveCreate(filename,*keypairptr,livesource_checkpoint_filename,swarm_cipm,livesource_disc_wnd,SWIFT_DEFAULT_LIVE_NCHUNKS_PER_SIGN,chunk_size); // SIGNPEAKTODO

        // Create HTTP client
        struct evhttp_connection *cn = evhttp_connection_base_new(Channel::evbase, NULL, httpservname.c_str(), httpport);
        struct evhttp_request *req = evhttp_request_new(LiveSourceHTTPResponseCallback, NULL);
        evhttp_request_set_chunked_cb(req,LiveSourceHTTPDownloadChunkCallback);

        // Make request to server
        evhttp_make_request(cn, req, EVHTTP_REQ_GET,httppath.c_str());
        evhttp_add_header(req->output_headers, "Host", httpservname.c_str());
    }

    // Easy testing: write swarmID to file as URL. Can be read by client using -r
    if (urlfilename != "")
        PrintURL(livesource_lt->td(),"",chunk_size,urlfilename);

    report_progress = true;
    single_td = livesource_lt->td();
}



void AttemptCheckpoint()
{
    if (swift::ttype(single_td) == FILE_TRANSFER && file_enable_checkpoint && !file_checkpointed && swift::IsComplete(single_td))
    {
	std::string binmap_filename = swift::GetOSPathName(single_td);
	binmap_filename.append(".mbinmap");
	fprintf(stderr,"swift: Complete, checkpointing %s\n", binmap_filename.c_str() );

	if (swift::Checkpoint(single_td) >= 0)
	    file_checkpointed = true;
    }
}


void ReportCallback(int fd, short event, void *arg) {
    // Called every second to print/calc some stats
    // Arno, 2012-05-24: Why-oh-why, update NOW
    Channel::Time();

    if (single_td  >= 0)
    {
        if (report_progress) {
            fprintf(stderr,
                "%s %" PRIi64 " of %" PRIi64 " (seq %" PRIi64 ") %" PRIi64 " dgram %" PRIi64 " bytes up, "    \
                "%" PRIi64 " dgram %" PRIi64 " bytes down\n",
                IsComplete(single_td ) ? "DONE" : "done",
                Complete(single_td), Size(single_td), SeqComplete(single_td),
                Channel::global_dgrams_up, Channel::global_raw_bytes_up,
                Channel::global_dgrams_down, Channel::global_raw_bytes_down );

            double up = swift::GetCurrentSpeed(single_td,DDIR_UPLOAD);
            double dw = swift::GetCurrentSpeed(single_td,DDIR_DOWNLOAD);

            if (Size(single_td)) {
                fprintf(stderr, "     %" PRIi64 "%%", Complete(single_td)*100/Size(single_td));
                fprintf(stderr, "\tTransfer complete");
                if (!IsComplete(single_td ))
                    fprintf(stderr, "\tin: %.2f min.", (Size(single_td)-Complete(single_td))/(dw*60));
            }
            else
                fprintf(stderr, "\t0%%");

            fprintf(stderr, "\n");

            if (up/1048576 > 1)
                fprintf(stderr,"upload %.2f MB/s (%lf B/s)\n", up/(1<<20), up);
            else
                fprintf(stderr,"upload %.2f KB/s (%lf B/s)\n", up/(1<<10), up);
            if (dw/1048576 > 1)
                fprintf(stderr,"dwload %.2f MB/s (%lf B/s)\n", dw/(1<<20), dw);
            else
                fprintf(stderr,"dwload %.2f KB/s (%lf B/s)\n", dw/(1<<10), dw);
        }

        AttemptCheckpoint();

        if (exitoncomplete && swift::IsComplete(single_td))
            // Download and stop mode
            event_base_loopexit(Channel::evbase, NULL);
    }
    else if (report_progress) {
        bool allComplete = true;
        uint64_t complete = 0;
        uint64_t size = 0;
        uint64_t seqcomplete = 0;
        int nactive=0,nloaded=0;

        tdlist_t tds = swift::GetTransferDescriptors();
        tdlist_t::iterator iter;
        nloaded = tds.size();
        for (iter = tds.begin(); iter != tds.end(); iter++)
        {
            int td = *iter;
            if (!swift::IsComplete(td))
                allComplete = false;
            complete += swift::Complete(td);
            seqcomplete += swift::SeqComplete(td);
            size += swift::Size(td);

            ContentTransfer *ct = swift::GetActivatedTransfer(td);
            if (ct != NULL)
        	nactive++;
            double up = swift::GetCurrentSpeed(td,DDIR_UPLOAD);
            if (up/1048576 > 1)
                fprintf(stderr,"%d: upload %.2f MB/s\t", td, up/(1<<20));
            else
                fprintf(stderr,"%d: upload %.2f KB/s\t", td, up/(1<<10));
        }
        /*
        fprintf(stderr,
            "%s %" PRIu64 " of %" PRIu64 " (seq %" PRIu64 ") %" PRIi64 " dgram %" PRIi64 " bytes up, " \
            "%" PRIi64 " dgram %" PRIi64 " bytes down\n",
            allComplete ? "DONE" : "done",
            complete, size, seqcomplete,
            Channel::global_dgrams_up, Channel::global_raw_bytes_up,
            Channel::global_dgrams_down, Channel::global_raw_bytes_down );
        */
        fprintf(stderr,"swift: loaded %d active %d\n", nloaded, nactive );


    }
    if (httpgw_enabled)
    {
        //fprintf(stderr,".");

        // ARNOSMPTODO: Restore fail behaviour when used in SwarmPlayer 3000.
        if (!HTTPIsSending()) {
            // TODO
            //event_base_loopexit(Channel::evbase, NULL);
            return;
        }
    }
#ifndef SWIFTGTEST
    if (StatsQuit())
    {
        // SwarmPlayer 3000: User click "Quit" button in webUI.
        struct timeval tv;
        tv.tv_sec = 1;
        int ret = event_base_loopexit(Channel::evbase,&tv);
    }
#endif

    // SWIFTPROC
    if (cmdgw_report_interval == 1 || ((cmdgw_report_counter % cmdgw_report_interval) == 0))
        CmdGwUpdateDLStatesCallback();

    cmdgw_report_counter++;

    evtimer_add(&evreport, tint2tv(REPORT_INTERVAL*TINT_SEC));
}


void EndCallback(int fd, short event, void *arg) {
    // Called when wait timer expires == fixed time daemon
    event_base_loopexit(Channel::evbase, NULL);
}


void RescanDirCallback(int fd, short event, void *arg) {

    // SEEDDIR
    // Rescan dir: CAREFUL: this is blocking, better prepare .m* files first
    // by running swift separately and then copy content + *.m* to scanned dir,
    // such that a fast restore from checkpoint is done.
    //
    OpenSwiftDirectory(scan_dirname,trackerurl,false,chunk_size,false); // activate = false

    CleanSwiftDirectory(scan_dirname);

    evtimer_add(&evrescan, tint2tv(RESCAN_DIR_INTERVAL*TINT_SEC));
}



// MULTIFILE
typedef std::vector<std::pair<std::string,int64_t> >    filelist_t;
int CreateMultifileSpec(std::string specfilename, int argc, char *argv[], int argidx)
{
    fprintf(stderr,"CreateMultiFileSpec: %s nfiles %d\n", specfilename.c_str(), argc-argidx );

    filelist_t    filelist;

    // MULTIFILE TODO: if arg is a directory, include all files

    // 1. Make list of files
    for (int i=argidx; i<argc; i++)
    {
        std::string pathname = argv[i];
        int64_t fsize = file_size_by_path_utf8(pathname);
        if( fsize < 0)
        {
            fprintf(stderr,"cannot open file in multi-spec list: %s\n", pathname.c_str() );
            print_error("cannot open file in multi-spec list" );
            return fsize;
        }

        // TODO: strip off common path from source pathnames
        // TODO: convert path separator to standard
        std::string pathstr = pathname; // TODO: UTF8-encode
        filelist.push_back(std::make_pair(pathstr,fsize));
    }

    // 2. Files in multi-file spec must be sorted, such that creating a swarm
    // from the same set of files results in the same swarm.
    sort(filelist.begin(), filelist.end());


    // 3. Create spec body
    std::ostringstream specbody;

    filelist_t::iterator iter;
    for (iter = filelist.begin(); iter < filelist.end(); iter++)
    {
        specbody << Storage::os2specpn( (*iter).first );
        specbody << " ";
        specbody << (*iter).second << "\n";
    }

    // 4. Calc specsize
    int specsize = Storage::MULTIFILE_PATHNAME.size()+1+0+1+specbody.str().size();
    char numstr[100];
    sprintf(numstr,"%d",specsize);
    char numstr2[100];
    sprintf(numstr2,"%" PRIu64 "",specsize+strlen(numstr));
    if (strlen(numstr) == strlen(numstr2))
        specsize += strlen(numstr);
    else
        specsize += strlen(numstr)+(strlen(numstr2)-strlen(numstr));

    // 5. Create spec as string
    std::ostringstream spec;
    spec << Storage::MULTIFILE_PATHNAME;
    spec << " ";
    spec << specsize;
    spec << "\n";
    spec << specbody.str();

    fprintf(stderr,"spec: <%s>\n", spec.str().c_str() );

    // 6. Write to specfile
    FILE *fp = fopen_utf8(specfilename.c_str(),"wb");
    int ret = fwrite(spec.str().c_str(),sizeof(char),spec.str().length(),fp);
    if (ret < 0)
        print_error("cannot write multi-file spec");
    fclose(fp);

    return ret;
}


void LiveSourceFileTimerCallback(int fd, short event, void *arg) {

    char buf[LIVESOURCE_BUFSIZE];

    Channel::Time();
    //fprintf(stderr,"%s live: file: timer\n", tintstr() );

    if (livesource_isfile)
    {
	// Little hack to facilitate testing of source restarts by ensuring
	// we start reading where left off.
	uint64_t seekoff = livesource_lt->GetSourceWriteOffset();
	if (seekoff > 0)
	{
	    int ret = file_seek(livesource_fd,seekoff);
	    if (ret < 0)
		print_error("live: file: seek in input");
	}
	livesource_isfile = false; // bad bad thing
    }

    int nread = -1;
    if (livesource_filep != NULL)
        nread = fread(buf,sizeof(char),sizeof(buf),livesource_filep);
    else
        nread = read(livesource_fd,buf,sizeof(buf));
    //fprintf(stderr,"%s live: file: read returned %d\n", tintstr(), nread );
    if (nread <= -1)
        print_error("error reading from live source");
    else if (nread > 0)
    {
        int ret = evbuffer_add(livesource_evb,buf,nread);
        if (ret < 0)
            print_error("live: file: error evbuffer_add");

        LiveSourceAttemptCreate();
    }

    // Reschedule
    evtimer_add(&evlivesource, tint2tv(LIVESOURCE_INTERVAL));
}


void LiveSourceHTTPResponseCallback(struct evhttp_request *req, void *arg)
{
    const char *new_location = NULL;
    switch(req->response_code)
    {
        case HTTP_OK:
            fprintf(stderr,"live: http: GET OK\n");
            break;

        case HTTP_MOVEPERM:
        case HTTP_MOVETEMP:
            new_location = evhttp_find_header(req->input_headers, "Location");
            fprintf(stderr,"live: http: GET REDIRECT %s\n", new_location );
            break;
        default:
            fprintf(stderr,"live: http: GET ERROR %d\n", req->response_code );
            event_base_loopexit(Channel::evbase, 0);
            return;
    }

    // LIVETODO: already reply data here?
    //evbuffer_add_buffer(ctx->buffer, req->input_buffer);
}


void LiveSourceHTTPDownloadChunkCallback(struct evhttp_request *req, void *arg)
{
    int length = evbuffer_get_length(req->input_buffer);
    fprintf(stderr,"live: http: read %d bytes\n", length );

    // Create chunks of chunk_size()
    int ret = evbuffer_add_buffer(livesource_evb,req->input_buffer);
    if (ret < 0)
        print_error("live: http: error evbuffer_add");

    LiveSourceAttemptCreate();
}


void LiveSourceAttemptCreate()
{
    if (evbuffer_get_length(livesource_evb) > livesource_lt->chunk_size())
    {
        size_t nchunklen = livesource_lt->chunk_size() * (size_t)(evbuffer_get_length(livesource_evb)/livesource_lt->chunk_size());
        uint8_t *chunks = evbuffer_pullup(livesource_evb, nchunklen);
        int nwrite = swift::LiveWrite(livesource_lt, chunks, nchunklen);
        if (nwrite < -1)
            print_error("live: create: error");

        int ret = evbuffer_drain(livesource_evb, nchunklen);
        if (ret < 0)
            print_error("live: create: error evbuffer_drain");
    }
}




#ifdef _WIN32

// UTF-16 version of app entry point for console Windows-apps
int wmain( int wargc, wchar_t *wargv[ ], wchar_t *envp[ ] )
{
    char **utf8args = (char **)malloc(wargc*sizeof(char *));
    for (int i=0; i<wargc; i++)
    {
        //std::wcerr << "wmain: orig " << wargv[i] << std::endl;
        std::string utf8c = utf16to8(wargv[i]);
        utf8args[i] = strdup(utf8c.c_str());
    }
    return utf8main(wargc,utf8args);
}

// UTF-16 version of app entry point for non-console Windows apps
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    int wargc=0;
    fprintf(stderr,"wWinMain: enter\n");
    // Arno, 2012-05-30: TODO: add dummy first arg, because getopt eats the first
    // the argument when it is a non-console app. Currently done with -j dummy arg.
    LPWSTR* wargv = CommandLineToArgvW(pCmdLine, &wargc );
    return wmain(wargc,wargv,NULL);
}

#else

#ifndef SWIFTGTEST

// UNIX version of app entry point for console apps
int main(int argc, char *argv[])
{
    // TODO: Convert to UTF-8 if locale not UTF-8
    return utf8main(argc,argv);
}

#else

#include <gtest/gtest.h>

int copyargc;
char **copyargv;


TEST(CoverageTest,CoverageMain)
{
    fprintf(stderr,"swift: Entering Coverage main\n");
  
    // TODO: Convert to UTF-8 if locale not UTF-8
    utf8main(copyargc,copyargv);
    
    fprintf(stderr,"swift: Leaving Coverage main\n");
}

// UNIX version of app entry point for GTest coverage
int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);

    // Google has removed Gtest specific args.
    // Copy to pass to real main in TEST.
    copyargc = argc;
    copyargv = new char *[copyargc];
    for (int i=0; i<copyargc; i++)
    {
	copyargv[i] = new char[strlen(argv[i])+1];
	strcpy(copyargv[i],argv[i]);
    }

    return RUN_ALL_TESTS();
}

#endif // SWIFTGTEST

#endif

