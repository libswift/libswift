/*
 *  NativeLib.cpp
 *  Java interface to swift for use in Android.
 *
 *  Arno: Because the swift:: interface is not thread safe, some calls are
 *  rescheduled on the thread calling NativeLib.Mainloop() and their results
 *  are asynchronously retrievable via NativeLib.asyncGetResult().
 *
 *  Created by Riccardo Petrocco, Arno Bakker
 *  Copyright 2010-2014 Delft University of Technology. All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "compat.h"
#include "swift.h"
// jni header file
#include "com_tudelft_triblerdroid_swift_NativeLib.h"
#include <sstream>
#include <map>

using namespace swift;

// httpgw.cpp functions
bool InstallHTTPGateway( struct event_base *evbase,Address bindaddr, uint32_t chunk_size, double *maxspeed, std::string storage_dir, int32_t vod_step, int32_t min_prebuf );
bool HTTPIsSending();
std::string HttpGwGetProgressString(Sha1Hash swarmid);
std::string HttpGwStatsGetSpeedCallback(Sha1Hash swarmid);

// Local functions
// Libevent* functions are executed by Mainloop thread,
void LibeventKeepaliveCallback(int fd, short event, void *arg);
void LibeventOpenCallback(int fd, short event, void *arg);
void LibeventCloseCallback(int fd, short event, void *arg);
void LibeventGetHTTPProgressCallback(int fd, short event, void *arg);
void LibeventGetStatsCallback(int fd, short event, void *arg);
void LibeventLiveAddCallback(int fd, short event, void *arg);

// Global variables
bool enginestarted = false;
uint32_t chunk_size = SWIFT_DEFAULT_CHUNK_SIZE;
double maxspeed[2] = {DBL_MAX,DBL_MAX};
struct event evkeepalive;

// for Live
LiveTransfer *livesource_lt = NULL;
struct evbuffer *livesource_evb = NULL;


// for async calls
class AsyncParams
{
  public:
    int      	callid_;
    Sha1Hash 	swarmid_;
    Address 	tracker_;
    std::string filename_;
    char 	*data_;
    int		datalen_;
    bool 	removestate_;
    bool 	removecontent_;

    AsyncParams(Sha1Hash &swarmid, Address &tracker, std::string filename) :
	callid_(-1), swarmid_(swarmid), tracker_(tracker), filename_(filename),
	data_(NULL), datalen_(-1), removestate_(false), removecontent_(false)
    {
    }

    AsyncParams(Sha1Hash &swarmid) :
	callid_(-1), swarmid_(swarmid), tracker_(""), filename_(""),
	data_(NULL), datalen_(-1), removestate_(false), removecontent_(false)
    {
    }

    AsyncParams(char *data, int datalen) :
	callid_(-1), swarmid_(Sha1Hash::ZERO), tracker_(""), filename_(""),
	data_(data), datalen_(datalen), removestate_(false), removecontent_(false)
    {
    }

    AsyncParams(Sha1Hash &swarmid, bool removestate, bool removecontent) :
	callid_(-1), swarmid_(swarmid), tracker_(""), filename_(""),
	data_(NULL), datalen_(-1), removestate_(removestate), removecontent_(removecontent)
    {
    }

    ~AsyncParams()
    {
	if (data_ != NULL)
	    delete data_;
    }
};


typedef std::map<int,std::string>  intstringmap_t;

pthread_mutex_t         asyncMutex = PTHREAD_MUTEX_INITIALIZER;
int 			asyncCallID=481; // protected by mutex
intstringmap_t		asyncResMap;    // protected by mutex



JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_Init(JNIEnv * env, jobject obj, jstring jlistenaddr, jstring jhttpgwaddr ) {

    dprintf("NativeLib::Init called\n");
    if (enginestarted)
	return env->NewStringUTF("Engine already initialized");

    std::string errorstr = "";
    jboolean blnIsCopy;

    const char * listenaddrcstr = (env)->GetStringUTFChars(jlistenaddr, &blnIsCopy);
    Address listenaddr = Address(listenaddrcstr);

    const char * httpgwaddrcstr = (env)->GetStringUTFChars(jhttpgwaddr, &blnIsCopy);
    Address httpgwaddr = Address(httpgwaddrcstr);


    // Libevent2 initialization
    LibraryInit();
    Channel::evbase = event_base_new();

    std::string s = "/sdcard/swift/debug.txt";
    //char pidstr[32];
    //sprintf(pidstr,"%d", getpid() );
    //s += pidstr;

    // Debug file saved on SD
    Channel::debug_file = fopen (s.c_str(),"w+");

    dprintf("NativeLib::Init: Log opened\n");

    // Bind to UDP port
    if (listenaddr != Address())
    {
	// seeding
	if (Listen(listenaddr)<=0)
	    errorstr = "can't listen";
    }
    else
    {
	// leeching
	for (int i=0; i<=10; i++) {
	    listenaddr = Address((uint32_t)INADDR_ANY,0);
	    if (Listen(listenaddr)>0)
		break;
	    if (i==10)
		errorstr = "can't listen on ANY";
	}
    }

    // Arno: always have some timer running. Otherwise in some cases libevent
    // won't execute any evtimer events added later.
    evtimer_assign(&evkeepalive, Channel::evbase, LibeventKeepaliveCallback, NULL);
    evtimer_add(&evkeepalive, tint2tv(TINT_SEC));

    // Start HTTP gateway, if requested
    if (errorstr == "" && httpgwaddr!=Address())
    {
	dprintf("NativeLib::Init: Installing HTTP gateway\n");

	// Playback via HTTP GW: Client should contact 127.0.0.1:8082/roothash-in-hex and
	// that will call swift::(Live)Open to start the actual download
	// 32 K steps and no minimal prebuf for Android
	bool ret = InstallHTTPGateway(Channel::evbase,httpgwaddr,chunk_size,maxspeed,"/sdcard/swift/", 32*1024, 0);
	if (ret == false)
	    errorstr = "cannot start HTTP gateway";
    }

    (env)->ReleaseStringUTFChars(jlistenaddr, listenaddrcstr); // release jstring
    (env)->ReleaseStringUTFChars(jhttpgwaddr, httpgwaddrcstr); // release jstring

    enginestarted = true;

    return env->NewStringUTF(errorstr.c_str());
}



void LibeventKeepaliveCallback(int fd, short event, void *arg)
{
    // Called every second to keep libevent timer processing alive?!
    evtimer_add(&evkeepalive, tint2tv(TINT_SEC));
}



JNIEXPORT void JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_Mainloop(JNIEnv * env, jobject obj)
{
    // Enter libevent mainloop
    event_base_dispatch(Channel::evbase);

    // Only reached after Shutdown
}


JNIEXPORT void JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_Shutdown(JNIEnv * env, jobject obj) {

    if (!enginestarted)
	return;

    // Tell mainloop to exit, will release call to progress()
    event_base_loopexit(Channel::evbase, NULL);

    enginestarted = false;
}


/**
 * Allocates a callid for an asynchronous call and schedules it.
 */
int AsyncRegisterCallback(event_callback_fn func, AsyncParams *aptr)
{
    int prc = pthread_mutex_lock(&asyncMutex);
    if (prc != 0)
    {
	dprintf("NativeLib::AsyncRegisterCallback: mutex_lock failed\n");
	return -1;
    }

    aptr->callid_ = asyncCallID;
    asyncCallID++;

    prc = pthread_mutex_unlock(&asyncMutex);
    if (prc != 0)
    {
	dprintf("NativeLib::AsyncRegisterCallback: mutex_unlock failed\n");
	return -1;
    }

    // Call timer
    struct event *evtimerptr = new struct event;
    evtimer_assign(evtimerptr,Channel::evbase,func,aptr);
    evtimer_add(evtimerptr,tint2tv(0));

    return aptr->callid_;
}

/**
 * Sets the result of the asynchronous call identified by callid
 */
void AsyncSetResult(int callid, std::string result)
{
    int prc = pthread_mutex_lock(&asyncMutex);
    if (prc != 0)
    {
	dprintf("NativeLib::AsyncSetResult: mutex_lock failed\n");
	return;
    }

    asyncResMap[callid] = result;

    prc = pthread_mutex_unlock(&asyncMutex);
    if (prc != 0)
    {
	dprintf("NativeLib::AsyncSetResult: mutex_unlock failed\n");
	return;
    }
}


JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_asyncGetResult(JNIEnv *env, jobject obj, jint jcallid)
{
    int callid = (int)jcallid;

    std::string result = "";
    int prc = pthread_mutex_lock(&asyncMutex);
    if (prc != 0)
    {
	dprintf("NativeLib::asyncGetResult: mutex_lock failed\n");
	return env->NewStringUTF("mutex_lock failed");
    }

    intstringmap_t::iterator iter;
    iter = asyncResMap.find(callid);
    if (iter == asyncResMap.end())
	result = "n/a";
    else
    {
	result = iter->second;
	// Arno, 2012-12-04: Remove call result to avoid state buildup.
	asyncResMap.erase(iter);
    }

    prc = pthread_mutex_unlock(&asyncMutex);
    if (prc != 0)
    {
	dprintf("NativeLib::asyncGetResult: mutex_unlock failed\n");
	return env->NewStringUTF("mutex_lock failed");
    }

    return env->NewStringUTF(result.c_str());
}





JNIEXPORT jint JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_asyncOpen( JNIEnv * env, jobject obj, jstring jswarmid, jstring jtracker, jstring jfilename)
{
    dprintf("NativeLib::Open called\n");

    if (!enginestarted)
	return -1; // "Engine not yet initialized"

    jboolean blnIsCopy;

    const char *swarmidcstr = (env)->GetStringUTFChars(jswarmid, &blnIsCopy);
    const char *trackercstr = (env)->GetStringUTFChars(jtracker, &blnIsCopy);
    const char *filenamecstr = (env)->GetStringUTFChars(jfilename, &blnIsCopy);

    Sha1Hash swarmid = Sha1Hash(true,swarmidcstr);
    std::string dest = "";

    // If no filename, use roothash-in-hex as default
    if (swarmid != Sha1Hash::ZERO && filenamecstr == "")
	dest = swarmid.hex();
    else
	dest = filenamecstr;

    if (dest == "")
	return -1; // "No destination could be determined"

    Address tracker(trackercstr);
    AsyncParams *aptr = new AsyncParams(swarmid,tracker,dest);

    // Register callback
    int callid = AsyncRegisterCallback(&LibeventOpenCallback,aptr);

    (env)->ReleaseStringUTFChars(jswarmid, swarmidcstr); // release jstring
    (env)->ReleaseStringUTFChars(jtracker, trackercstr); // release jstring
    (env)->ReleaseStringUTFChars(jfilename, filenamecstr); // release jstring

    return callid;
}


/**
 * Called by thread that called Mainloop which is the only thread active in
 * swift, so all swift:: calls are now thread-safe.
 */
void LibeventOpenCallback(int fd, short event, void *arg)
{
    AsyncParams *aptr = (AsyncParams *) arg;

    std::string errorstr="";
    dprintf("NativeLib::Open: %s writing to %s\n", aptr->swarmid_.hex().c_str(), aptr->filename_.c_str() );
    int td = swift::Open(aptr->filename_,aptr->swarmid_,aptr->tracker_,false);
    if (td < 0)
	errorstr = "cannot open destination file";
    else
    {
	errorstr = swift::SwarmID(td).hex();
	dprintf("NativeLib::Open: swarmid: %s\n", errorstr.c_str());
    }

    // Register result
    AsyncSetResult(aptr->callid_,errorstr);

    delete aptr;
}




JNIEXPORT jint JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_asyncClose( JNIEnv * env, jobject obj, jstring jswarmid, jboolean jremovestate, jboolean jremovecontent )
{
    dprintf("NativeLib::Close called\n");

    if (!enginestarted)
	return -1; // "Engine not yet initialized"

    jboolean blnIsCopy;

    const char *swarmidcstr = (env)->GetStringUTFChars(jswarmid, &blnIsCopy);

    Sha1Hash swarmid = Sha1Hash(true,swarmidcstr);
    bool rs = (bool)jremovestate;
    bool rc = (bool)jremovecontent;
    AsyncParams *aptr = new AsyncParams(swarmid,rs,rc);

    // Register callback
    int callid = AsyncRegisterCallback(&LibeventCloseCallback,aptr);

    (env)->ReleaseStringUTFChars(jswarmid, swarmidcstr); // release jstring

    return callid;
}


/**
 * Called by thread that called Mainloop which is the only thread active in
 * swift, so all swift:: calls are now thread-safe.
 */
void LibeventCloseCallback(int fd, short event, void *arg)
{
    AsyncParams *aptr = (AsyncParams *) arg;

    std::string errorstr="";
    dprintf("NativeLib::Close: %s\n", aptr->swarmid_.hex().c_str() );
    int td = swift::Find(aptr->swarmid_);
    if (td < 0)
	errorstr = "cannot find swarm to close";
    else
    {
	errorstr = aptr->swarmid_.hex();
	swift::Close(td,aptr->removestate_,aptr->removecontent_);
	dprintf("NativeLib::Close: swarmid: %s\n", errorstr.c_str());
    }

    // Register result
    AsyncSetResult(aptr->callid_,errorstr);

    delete aptr;
}



JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_hashCheckOffline(JNIEnv *env, jobject obj, jstring jfilename )
{
    dprintf("NativeLib::hashCheckOffline called\n");

    jboolean blnIsCopy;

    const char *filenamecstr = (env)->GetStringUTFChars(jfilename, &blnIsCopy);

    std::string errorstr = "";
    Sha1Hash swarmid;
    int ret = swift::HashCheckOffline(filenamecstr,&swarmid);
    if (ret < 0)
	errorstr = "Error hash check offline";
    else
	errorstr = swarmid.hex();

    (env)->ReleaseStringUTFChars(jfilename, filenamecstr); // release jstring

    return env->NewStringUTF(errorstr.c_str());
}



JNIEXPORT void JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_SetTracker(JNIEnv * env, jobject obj, jstring jtracker)
{
    if (!enginestarted)
	return; // "Engine not yet initialized";

    std::string errorstr = "";
    jboolean blnIsCopy;

    const char * trackercstr = (env)->GetStringUTFChars(jtracker, &blnIsCopy);
    Address tracker = Address(trackercstr);
    if (tracker==Address())
        dprintf("NativeLib::SetTracker: Tracker address must be hostname:port, ip:port or just port\n");
    else
	SetTracker(tracker);

    (env)->ReleaseStringUTFChars(jtracker , trackercstr); // release jstring
}



JNIEXPORT jint JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_asyncGetHTTPProgress(JNIEnv * env, jobject obj, jstring jswarmid)
{
    if (!enginestarted)
	return -1;  // "Engine not yet initialized"

    jboolean blnIsCopy;

    const char * swarmidcstr = (env)->GetStringUTFChars(jswarmid, &blnIsCopy);
    Sha1Hash swarmid = Sha1Hash(true,swarmidcstr);

    AsyncParams *aptr = new AsyncParams(swarmid);

    // Register callback
    int callid = AsyncRegisterCallback(&LibeventGetHTTPProgressCallback,aptr);

    (env)->ReleaseStringUTFChars(jswarmid , swarmidcstr); // release jstring

    return callid;
}


/**
 * Called by thread that called Mainloop which is the only thread active in
 * swift, so all swift:: calls are now thread-safe.
 */
void LibeventGetHTTPProgressCallback(int fd, short event, void *arg)
{
    AsyncParams *aptr = (AsyncParams *) arg;

    std::string errorstr = HttpGwGetProgressString(aptr->swarmid_);

    // Register result
    AsyncSetResult(aptr->callid_,errorstr);

    delete aptr;
}



JNIEXPORT jint JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_asyncGetStats(JNIEnv * env, jobject obj, jstring jswarmid)
{
    if (!enginestarted)
	return -1;  // "Engine not yet initialized"

    jboolean blnIsCopy;

    const char * swarmidcstr = (env)->GetStringUTFChars(jswarmid, &blnIsCopy);
    Sha1Hash swarmid = Sha1Hash(true,swarmidcstr);

    AsyncParams *aptr = new AsyncParams(swarmid);

    // Register callback
    int callid = AsyncRegisterCallback(&LibeventGetStatsCallback,aptr);

    (env)->ReleaseStringUTFChars(jswarmid , swarmidcstr); // release jstring

    return callid;
}



/**
 * Called by thread that called Mainloop which is the only thread active in
 * swift, so all swift:: calls are now thread-safe.
 */
void LibeventGetStatsCallback(int fd, short event, void *arg)
{
    AsyncParams *aptr = (AsyncParams *) arg;

    std::string errorstr = HttpGwStatsGetSpeedCallback(aptr->swarmid_);

    // Register result
    AsyncSetResult(aptr->callid_,errorstr);

    delete aptr;
}



/*
 * Create live swarm
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_LiveCreate(JNIEnv *env, jobject obj, jstring jswarmid)
{
    if (!enginestarted)
	return env->NewStringUTF("Engine not yet initialized");

    // Clean old live swarm
    if (livesource_lt != NULL)
	delete livesource_lt;

    if (livesource_evb != NULL)
	evbuffer_free(livesource_evb);

    // Buffer for H.264 NALUs from cam, with startcode 00000001 added
    livesource_evb = evbuffer_new();

    // Create live source TODO: use jswarmid parameter
    jboolean blnIsCopy;

    const char * swarmidcstr = (env)->GetStringUTFChars(jswarmid, &blnIsCopy);
    // std::string swarmidstr = "ArnosFirstSwarm";
    Sha1Hash swarmid = Sha1Hash(true,swarmidcstr);

    // Start swarm
    std::string filename = "/sdcard/swift/storage.dat";
    livesource_lt = swift::LiveCreate(filename,swarmid);

    (env)->ReleaseStringUTFChars(jswarmid , swarmidcstr); // release jstring

    return env->NewStringUTF("");
}


/*
 * Add data to live swarm, to be turned into chunks when >=chunk_size has been
 * added. Thread-safe because swift::LiveWrite uses an evtimer
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_LiveAdd(JNIEnv *env, jobject obj, jstring jswarmid, jbyteArray dataArray, jint dataOffset, jint dataLength )
{
    if (!enginestarted)
	return env->NewStringUTF("Engine not yet initialized");

    if (livesource_lt == NULL)
	return env->NewStringUTF("Live swarm not created");

    // http://stackoverflow.com/questions/8439233/how-to-convert-jbytearray-to-native-char-in-jni
    jboolean isCopy;

    jbyte* b = env->GetByteArrayElements(dataArray, &isCopy);
    //jsize dataArrayLen = env->GetArrayLength(dataArray);

    char *data = (char *)b;
    int datalen = (int)dataLength;
    dprintf("NativeLib::LiveAdd: Got %p bytes %d from java\n", data, datalen );

    if (data != NULL && datalen > 0)
    {
	// Must copy data, as the actual swift::LiveWrite call will be done on Mainloop thread
	// Data deallocated via AsyncParams deconstructor.
	char *copydata = new char[datalen];
	memcpy(copydata,data,datalen);

	AsyncParams *aptr = new AsyncParams(copydata,datalen);

	// Register callback
	(void)AsyncRegisterCallback(&LibeventLiveAddCallback,aptr);
    }

    env->ReleaseByteArrayElements(dataArray, b, JNI_ABORT);

    return env->NewStringUTF("");
}

/*
 * Add live data to libevent evbuffer, to be turned into chunks when >=chunk_size
 * has been added.
 */
void LibeventLiveAddCallback(int fd, short event, void *arg)
{
    AsyncParams *aptr = (AsyncParams *) arg;

    // Create chunks of chunk_size()
    int ret = evbuffer_add(livesource_evb,aptr->data_,aptr->datalen_);
    if (ret < 0)
        print_error("live: cam: error evbuffer_add");

    if (evbuffer_get_length(livesource_evb) > livesource_lt->chunk_size())
    {
	// Sufficient data to create a chunk, perhaps even multiple
        size_t nchunklen = livesource_lt->chunk_size() * (size_t)(evbuffer_get_length(livesource_evb)/livesource_lt->chunk_size());
        uint8_t *chunks = evbuffer_pullup(livesource_evb, nchunklen);

        int nwrite = swift::LiveWrite(livesource_lt, chunks, nchunklen);
        if (nwrite < -1)
            print_error("live: create: error");

        int ret = evbuffer_drain(livesource_evb, nchunklen);
        if (ret < 0)
            print_error("live: create: error evbuffer_drain");
    }

    delete aptr;
}


