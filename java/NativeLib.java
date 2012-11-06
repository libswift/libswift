package com.tudelft.triblerdroid.swift;

/*
 * JNI interface for the libswift P2P engine.
 * 
 * Written by Riccardo Petrocco and Arno Bakker
 */

public class NativeLib {

  static {
    //System.loadLibrary("swift");
	  System.loadLibrary("event");
  }
  
  /** 
   * Initialize swift engine. MUST be called before any other functions.
   * Not thread-safe, only 1 global call allowed.
   * 
   * @param listenaddr IP address and port to listen to, or empty 
   * (=IP 0.0.0.0 and port randomly chosen)
   * @param httpgwaddr IP address and port for internal HTTP server to listen to 
   * @return empty string (=OK) or error string.
   */
  public native String Init( String listenaddr, String httpgwaddr );

  /** 
   * Enter swift mainloop. Does not exit till shutdown is called!
   * Not thread-safe, only 1 global call allowed.
   */
  public native void Mainloop();

  /** 
   * Shutdown swift engine.
   * Thread-safe.
   * 
   * @return empty string (=OK) or error string.
   */
  public native void Shutdown();

  /**
   * Retrieve result of previous async* call, if available.
   * Thread-safe.
   * 
   * @param callid as returned by async* call
   * @return "n/a" if not yet available or as described in async* call 
   */
  public native String asyncGetResult(int callid);
  
  
  /** 
   * Start swift download. For streaming via the internal HTTP server do not 
   * call this method, just do a HTTP GET /roothash-in-hex on the HTTPGW 
   * address configured via Init(). Append "@-1" to the path for live streams.
   * 
   * If content is already on disk (e.g. starting a seed) and the swift engine 
   * finds a checkpoint for this content (i.e., a filename.mhash and 
   * filename.mbinmap) it will not hash check. 
   * 
   * If the swarmid is all 0 and no checkpoint is found, the swift engine 
   * will hash check the content in filename. This may take a while, so the 
   * asyncGetResult will not yield a result quickly. Moreover, all network 
   * traffic will also be halted during this period. To prevent this situation 
   * you should call hashCheckOffline(filename) beforehand, see below.
   * 
   * Thread-safe.
   * 
   * @param swarmid	SwarmID in hex, may be all 0 for seeding
   * @param tracker tracker for this download
   * @param filename Location where to store content.
   * @return callid or -1 on error.
   * asyncGetResult(callid) will return swarmid (=OK) or error string.
   */
  public native int asyncOpen( String swarmid, String tracker, String filename );


  
  /** 
   * Write a swift checkpoint for the specified file.
   * 
   * Thread-safe.
   * 
   * @param filename Location where content is stored.
   * @return swarmid (=OK) or error string.
   */
  public native String hashCheckOffline( String filename );

  
  /** 
   * Set default swift tracker.
   * Thread-safe.
   * 
   * @param tracker tracker for this download
   */
  public native void SetTracker( String tracker );
  

  /**
   * Returns progress for the specified swift download streamed via the HTTPGW 
   * as a fraction of the number of bytes written to the HTTP socket and the 
   * total size of the content (0 for live).
   * Thread-safe.
   * 
   * @param swarmid	SwarmID in hex
   * @return callid or -1 on error.
   * asyncGetResult(callid) will return "xxx/yyy"
   */
  public native int asyncGetHTTPProgress( String swarmid );


  /**
   * Returns statistics for the specified swift download following KTH spec.
   * Thread-safe.
   * 
   * @param swarmid	SwarmID in hex
   * @return callid -1 on error.
   * asyncGetResult(callid) will return "a/b/c/d/e/f"
   */
  public native int asyncGetStats( String swarmid );
  
  
  /**
   * Create a live swarm (currently just one).
   * Not thread-safe, only 1 global call allowed.
   * 
   * @param swarmid public key to identify swarm (future)
   * @return empty string (=OK) or error string
   */
  public native String LiveCreate( String swarmid );
  
  
  /**
   * Pass data to live source.
   * Thread-safe.
   * 
   * @param swarmid public key to identify swarm
   * @param data content
   * @param offset offset of content in data array (UNUSED)
   * @param length size of the content
   * @returns empty string (=OK) or error string.
   */
  public native String LiveAdd( String swarmid, byte[] data, int offset, int length );
}