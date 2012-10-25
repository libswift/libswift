/*
 *  NativeLib.cpp
 *  Java interface to swift for use in Android
 *
 *  Created by Victor Grishchenko, Arno Bakker
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include "compat.h"
#include "swift.h"
// jni header file
#include "com_tudelft_triblerdroid_swift_NativeLib.h"
#include <sstream>


using namespace swift;

// httpgw.cpp functions
bool InstallHTTPGateway( struct event_base *evbase,Address bindaddr, uint32_t chunk_size, double *maxspeed, std::string storage_dir, int32_t vod_step, int32_t min_prebuf );
bool HTTPIsSending();
std::string HTTPGetProgressString(Sha1Hash root_hash);
std::string StatsGetSpeedCallback();

// Local functions
void ReportCallback(int fd, short event, void *arg);
void LiveSourceCameraCallback(char *data, int datalen);
void LiveSourceAttemptCreate();

// Global variables
struct event evreport;
int single_td = -1;
int attempts = 0;
bool enginestarted = false;

LiveTransfer *livesource_lt = NULL;
struct evbuffer *livesource_evb = NULL;


/*
 * Class:     com_tudelft_swift_NativeLib
 * Method:    start
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 *
 * Arno, 2012-01-30: Modified to use HTTP gateway to get streaming playback.
 */
#define STREAM_MODE		1


JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_start (JNIEnv * env, jobject obj, jstring hash, jstring INtracker, jstring destination) {

    dprintf("NativeLib::start called\n");

    uint32_t chunk_size = SWIFT_DEFAULT_CHUNK_SIZE;
    double maxspeed[2] = {DBL_MAX,DBL_MAX};

    // Ric: added 4 android
    jboolean blnIsCopy;

    // Arno: Needs to be done currently for every new download
    const char * tmpCstr = (env)->GetStringUTFChars(INtracker, &blnIsCopy);
    Address tracker = Address(tmpCstr);
    if (tracker==Address())
        return env->NewStringUTF("address must be hostname:port, ip:port or just port");
    SetTracker(tracker);

    (env)->ReleaseStringUTFChars(INtracker , tmpCstr); // release jstring

    if (enginestarted)
    	return env->NewStringUTF("started");
    else
    {
	// Libevent2 initialization
	// TODO: don't when in DOWNLOADMODE
	LibraryInit();
	Channel::evbase = event_base_new();

	std::string s = "/sdcard/swift/debug.txt";
	//char pidstr[32];
	//sprintf(pidstr,"%d", getpid() );
	//s += pidstr;

	// Debug file saved on SD
	Channel::debug_file = fopen (s.c_str(),"w+");

	// Bind to UDP port
	Address bindaddr;
	if (bindaddr!=Address()) { // seeding
	    if (Listen(bindaddr)<=0)
		return env->NewStringUTF("cant listen");
	} else if (tracker!=Address()) { // leeching
		for (int i=0; i<=10; i++) {
		    bindaddr = Address((uint32_t)INADDR_ANY,0);
		    if (Listen(bindaddr)>0)
			break;
		    if (i==10)
			return env->NewStringUTF("cant listen");
		}
	}

	if (STREAM_MODE) {
	    // Playback via HTTP GW: Client should contact 127.0.0.1:8082/roothash-in-hex and
	    // that will call swift::(Live)Open to start the actual download
	    Address httpgwaddr("0.0.0.0:8082");
	    // 32 K steps and no minimal prebuf for Android
	    bool ret = InstallHTTPGateway(Channel::evbase,httpgwaddr,chunk_size,maxspeed,"/sdcard/swift/", 32*1024, 0);
	    if (ret == false)
		return env->NewStringUTF("cannot start HTTP gateway");
	}

	// Arno: always, for statsgw, rate control, etc.
	//evtimer_assign(&evreport, Channel::evbase, ReportCallback, NULL);
	//evtimer_add(&evreport, tint2tv(TINT_SEC));

	enginestarted = true;
    }

    if (!STREAM_MODE)
    {
    	if (single_td != -1)
    	    swift::Close(single_td);

	tmpCstr = (env)->GetStringUTFChars(hash, &blnIsCopy);
	Sha1Hash root_hash = Sha1Hash(true,tmpCstr); // FIXME ambiguity
	if (root_hash==Sha1Hash::ZERO)
	    return env->NewStringUTF("SHA1 hash must be 40 hex symbols");

	(env)->ReleaseStringUTFChars(hash , tmpCstr); // release jstring

	char * filename = (char *)(env)->GetStringUTFChars(destination , &blnIsCopy);

        // Download then play
        if (root_hash!=Sha1Hash::ZERO && !filename)
            filename = strdup(root_hash.hex().c_str());

        if (filename)
        {
            dprintf("Opening %s writing to %s\n", root_hash.hex().c_str(), filename );
	    single_td = swift::Open(filename,root_hash);
	    if (single_td < 0)
        	return env->NewStringUTF("cannot open destination file");
	    printf("Root hash: %s\n", SwarmID(single_td).hex().c_str());
        }
        else
            dprintf("Not Opening %s, no dest \n", root_hash.hex().c_str() );
    }

    return env->NewStringUTF("started");
}


/*
 * Class:     com_tudelft_swift_NativeLib
 * Method:    progress
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_mainloop(JNIEnv * env, jobject obj) {

    // Enter libevent mainloop
    event_base_dispatch(Channel::evbase);

    // Never reached
    return 100;
}


/*
 * Class:     com_tudelft_swift_NativeLib
 * Method:    stop
 * Signature: ()Ljava/lang/Boolean;
 *
 * Arno, 2012-01-30: Now only called on Application shutdown.
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_stop(JNIEnv * env, jobject obj) {

    if (!enginestarted)
	return env->NewStringUTF("stopped");

    // Tell mainloop to exit, will release call to progress()
    event_base_loopexit(Channel::evbase, NULL);

    enginestarted = false;

    return env->NewStringUTF("stopped");
}


/*
 * Class:     com_tudelft_swift_NativeLib
 * Method:    httpprogress
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_httpprogress(JNIEnv * env, jobject obj, jstring hash) {

    std::stringstream rets;
    jboolean blnIsCopy;

    const char * tmpCstr = (env)->GetStringUTFChars(hash, &blnIsCopy);

    if (single_td != -1)
    {
	rets << swift::SeqComplete(single_td);
	rets << "/";
	rets << swift::Size(single_td);

	(env)->ReleaseStringUTFChars(hash , tmpCstr); // release jstring
	return env->NewStringUTF( rets.str().c_str() );
    }
    else
    {
	Sha1Hash root_hash = Sha1Hash(true,tmpCstr);
	std::string ret = HTTPGetProgressString(root_hash);

	(env)->ReleaseStringUTFChars(hash , tmpCstr); // release jstring
	return env->NewStringUTF(ret.c_str());
    }
}




void ReportCallback(int fd, short event, void *arg) {
    // Called every second to print/calc some stats

    dprintf("report callback\n");
    if (true)
    {
        // ARNOSMPTODO: Restore fail behaviour when used in SwarmPlayer 3000.
        if (!HTTPIsSending()) {
            // TODO
            //event_base_loopexit(Channel::evbase, NULL);
            return;
        }
    }

	evtimer_add(&evreport, tint2tv(TINT_SEC));
}


/*
 * Class:     com_tudelft_swift_NativeLib
 * Method:    stats
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_stats(JNIEnv * env, jobject obj) {

    std::string ret = StatsGetSpeedCallback();
    return env->NewStringUTF( ret.c_str() );
}



/*
 * Class:     com_tudelft_swift_NativeLib
 * Method:    hello
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_hello(JNIEnv * env, jobject obj) {

    return env->NewStringUTF("Hallo from Swift.. Library is working :-)");
}


/*
 * Create live swarm
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_livecreate(JNIEnv *env, jobject obj)
{
    if (!enginestarted)
    {
	// Libevent2 initialization
	LibraryInit();
	Channel::evbase = event_base_new();

	std::string s = "/sdcard/swift/debug.txt";
	//char pidstr[32];
	//sprintf(pidstr,"%d", getpid() );
	//s += pidstr;

	// Debug file saved on SD
	Channel::debug_file = fopen (s.c_str(),"w+");

	// Bind to UDP port
	Address bindaddr("0.0.0.0:6778");
	int sock = swift::Listen(bindaddr);
	if (sock<=0)
	    return env->NewStringUTF("live cant listen");

	// NAT
	/*const char *msg = "Hello World!";
	Address destaddr("192.168.0.102:4433");
	int r = sendto(sock,msg,strlen(msg),0,(struct sockaddr*)&(destaddr.addr),sizeof(struct sockaddr_in));
	if (r < 0)
	    dprintf("%s NAT ping failed %d\n", tintstr(), r );
	else
	    dprintf("%s NAT ping OK %d\n", tintstr(), r );
	 */

	// Buffer for H.264 from cam, with startcode 00000001 added
	livesource_evb = evbuffer_new();

	// Start live source
        std::string swarmidstr = "ArnosFirstSwarm";
        Sha1Hash swarmid = Sha1Hash(swarmidstr.c_str(), swarmidstr.length());

        // Create swarm
	std::string filename = "/sdcard/swift/storage.dat";
        livesource_lt = swift::LiveCreate(filename,swarmid);

	enginestarted = true;
    }

    return env->NewStringUTF("livecreate end");
}


/*
 * Add data to live swarm, to be turned into chunks when >=chunk_size has been
 * added.
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_swift_NativeLib_liveadd(JNIEnv *env, jobject obj, jbyteArray dataArray, jint dataOffset, jint dataLength )
{

    // http://stackoverflow.com/questions/8439233/how-to-convert-jbytearray-to-native-char-in-jni
    jboolean isCopy;

    jbyte* b = env->GetByteArrayElements(dataArray, &isCopy);
    //jsize dataArrayLen = env->GetArrayLength(dataArray);

    char *data = (char *)b;
    int datalen = (int)dataLength;
    dprintf("live: cam: Got %p bytes %d from java\n", data, datalen );

    if (data != NULL && datalen > 0)
    	LiveSourceCameraCallback(data,datalen);

    env->ReleaseByteArrayElements(dataArray, b, JNI_ABORT);

    return env->NewStringUTF("liveadd end");
}


void LiveSourceCameraCallback(char *data, int datalen)
{
    fprintf(stderr,"live: cam: read %d bytes\n", datalen );

    // Create chunks of chunk_size()
    int ret = evbuffer_add(livesource_evb,data,datalen);
    if (ret < 0)
        print_error("live: cam: error evbuffer_add");

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


