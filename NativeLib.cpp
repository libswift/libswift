/*
 *  swift.cpp
 *  swift the multiparty transport protocol
 *
 *  Created by Victor Grishchenko on 2/15/10.
 *  Copyright 2010 Delft University of Technology. All rights reserved.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include "compat.h"
#include "swift.h"
// jni header file
#include "com_tudelft_triblerdroid_first_NativeLib.h"
#include <sstream>


using namespace swift;

// httpgw.cpp functions
bool InstallHTTPGateway (struct event_base *evbase,Address bindaddr, size_t chunk_size, double *maxspeed, char *storagedir);
bool HTTPIsSending();
std::string HTTPGetProgressString(Sha1Hash root_hash);


// Local functions
void ReportCallback(int fd, short event, void *arg);
struct event evreport;

int file = -1;
int attempts = 0;
bool enginestarted = false;

/*
 * Class:     com_tudelft_swift_NativeLib
 * Method:    start
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 *
 * Arno, 2012-01-30: Modified to use HTTP gateway to get streaming playback.
 */
#define STREAM_MODE		1


JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_first_NativeLib_start (JNIEnv * env, jobject obj, jstring hash, jstring INtracker, jstring destination) {

	dprintf("NativeLib::start called\n");

	size_t chunk_size = SWIFT_DEFAULT_CHUNK_SIZE;
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
		char pidstr[32];
		sprintf(pidstr,"%d", getpid() );
		s += pidstr;

		// Debug file saved on SD
		Channel::debug_file = fopen (s.c_str(),"w+");

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
			// Playback via HTTP GW
			Address httpgwaddr("0.0.0.0:8082");
			SOCKET ret = InstallHTTPGateway(Channel::evbase,httpgwaddr,chunk_size,maxspeed,"/sdcard/swift/");
			if (ret < 0)
				return env->NewStringUTF("cannot start HTTP gateway");
		}

		// Arno: always, for statsgw, rate control, etc.
		//evtimer_assign(&evreport, Channel::evbase, ReportCallback, NULL);
		//evtimer_add(&evreport, tint2tv(TINT_SEC));

		enginestarted = true;
    }

    if (!STREAM_MODE)
    {
    	if (file != -1)
    		swift::Close(file);

		tmpCstr = (env)->GetStringUTFChars(hash, &blnIsCopy);
		Sha1Hash root_hash = Sha1Hash(true,tmpCstr); // FIXME ambiguity
		if (root_hash==Sha1Hash::ZERO)
			return env->NewStringUTF("SHA1 hash must be 40 hex symbols");

		(env)->ReleaseStringUTFChars(hash , tmpCstr); // release jstring

		char * filename = (char *)(env)->GetStringUTFChars(destination , &blnIsCopy);

        // Download then play
        if (root_hash!=Sha1Hash::ZERO && !filename)
            filename = strdup(root_hash.hex().c_str());

        if (filename) {

          dprintf("Opening %s writing to %s\n", root_hash.hex().c_str(), filename );
          file = Open(filename,root_hash);
          if (file<=0)
               return env->NewStringUTF("cannot open destination file");
          printf("Root hash: %s\n", RootMerkleHash(file).hex().c_str());
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
JNIEXPORT jint JNICALL Java_com_tudelft_triblerdroid_first_NativeLib_mainloop(JNIEnv * env, jobject obj) {

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
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_first_NativeLib_stop(JNIEnv * env, jobject obj) {

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
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_first_NativeLib_httpprogress(JNIEnv * env, jobject obj, jstring hash) {

	std::stringstream rets;
    jboolean blnIsCopy;

	const char * tmpCstr = (env)->GetStringUTFChars(hash, &blnIsCopy);

	if (file != -1)
	{
		rets << swift::SeqComplete(file);
		rets << "/";
		rets << swift::Size(file);

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
 * Method:    hello
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_tudelft_triblerdroid_first_NativeLib_hello(JNIEnv * env, jobject obj) {

	return env->NewStringUTF("Hallo from Swift.. Library is working :-)");
}

