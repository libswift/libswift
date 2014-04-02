// -*- coding: utf-8 -*-
// vi:si:et:sw=2:sts=2:ts=2
/*
  TribeChannel - Torrent video for <video>

  Written by Jan Gerber, Riccardo Petrocco, Arno Bakker
  see LICENSE.txt for license information
 */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

const Cc = Components.classes;
const Ci = Components.interfaces;

var tribeLoggingEnabled = true;

function LOG(aMsg) {
  if (tribeLoggingEnabled)
  {
    aMsg = ("*** TSwift : " + aMsg);
    Cc["@mozilla.org/consoleservice;1"].getService(Ci.nsIConsoleService).logStringMessage(aMsg);
    dump(aMsg);
  }
}


function TribeChannel() {
  this.wrappedJSObject = this;
  this.prefService = Cc["@mozilla.org/preferences-service;1"].getService(Ci.nsIPrefBranch).QueryInterface(Ci.nsIPrefService);
  try {
    tribeLoggingEnabled = this.prefService.getBoolPref("tswift.logging.enabled");
  } catch (e) {}

}

TribeChannel.prototype =
{
  classDescription: "Tribe channel",
  classID: Components.ID("68bfe8e9-c7ec-477d-a26c-2391333a7a42"),
  contractID: "@p2pnext.org/tswift/channel;1",
  QueryInterface: XPCOMUtils.generateQI([Ci.tswiftIChannel,
                                         Ci.nsIChannel,
                                         Ci.nsISupports]),
  _xpcom_factory : TribeChannelFactory,
  init: false,
  backend: 'python',
  running: false,
  torrent_url: '',
  swift_url: '',
  swift_path_query: '',
  http_url: '',
  swift_http_port: 0,
  setTorrentUrl: function(url) {
	/* Format:
	 * BT: 
	 *     tribe://torrenturl
	 *     where torrenturl is full URL of torrent file, unescaped.
	 * Swift:
	 *     tswift://tracker/roothash?k1=v1&k2=v2
	 *  or
	 *     tswift://tracker/roothash?k1=v1&k2=v2|httpurl   FIXME2014
	 *     where httpurl is the full URL of the HTTP equivalent, unescaped
	 *     
	 * Note: tswift:// is already stripped in TribeProtocolHandler.js
	 */
    
    var pidx = url.indexOf('|');
    if (pidx == -1)
    {
    	p2purl = url;
    }
    else
    {
    	p2purl = url.substr(0,pidx);
    	this.http_url = url.substr(pidx+1);
    }
    	
    
    if (true)
    {
        this.backend = 'swift';
        this.swift_url = p2purl;
  	  
        hashidx = this.swift_url.indexOf('/')+1;
        this.swift_path_query = this.swift_url.substr(hashidx,this.swift_url.length-hashidx);
        LOG("ARNO Swift path " + this.swift_path_query);
    }
    else
    {
        this.backend = 'python';
        this.torrent_url = p2purl;
    }
    
    LOG("setTorrentURL: torrentURL " + this.torrent_url + " swiftURL " + this.swift_url + " httpURL " + this.http_url);
    
    this.swift_http_port = 8000+Math.floor(Math.random()*50000);
  },
  cancel: function(aStatus) {
      LOG("cancel called");
  },
  shutdown: function() {
    LOG("shutdown called\n"); 
    var msg = 'SHUTDOWN\r\n';
    this.outputStream.write(msg, msg.length);

    //this.outputStream.close();
    //this.inputStream.close();
    this.transport.close(Components.results.NS_OK);
  },
  asyncOpen: function(aListener, aContext)
  {
    var _this = this;
    if(this.init) {
      LOG('asyncOpen called again\n');
      throw Components.results.NS_ERROR_ALREADY_OPENED;
    }
    this.init = true;
    var socketTransportService = Cc["@mozilla.org/network/socket-transport-service;1"].getService(Ci.nsISocketTransportService);
    
    var hostIPAddr = "127.0.0.1";
    var hostPort = "62063"; // Arno, 2010-08-10: SwarmPlayer independent from SwarmPlugin
    if (this.backend == 'swift')
        hostPort = "62481"; // dummy hack coexistence
    
    try {
      hostIPAddr = this.prefService.getCharPref("tswift.host.ipaddr");
    } catch (e) {}

    try {
      hostPort = this.prefService.getCharPref("tswift.host.port");
    } catch (e) {}

    this.transport = socketTransportService.createTransport(null, 0, hostIPAddr, hostPort, null);
    // Alright to open streams here as they are non-blocking by default
    this.outputStream = this.transport.openOutputStream(0,0,0);
    this.inputStream = this.transport.openInputStream(0,0,0);

    /* Arno, 2010-06-15: Let player inform BG process about capabilities
       to allow sharing of BGprocess between SwarmTransport and SwarmPlugin
       (the latter has pause capability)
     */
    var msg = 'SUPPORTS VIDEVENT_START\r\n';
    msg = msg + 'START ' + this.torrent_url + '\r\n'; // concat, strange async interface
    this.outputStream.write(msg, msg.length);

    var dataListener = {
      onStartRequest: function(request, context) {},
      onStopRequest: function(request, context, status) {
      
        LOG("onStopRequest " + _this.running );
        if (status == Components.results.NS_ERROR_CONNECTION_REFUSED) 
        {
          if (_this.backend == 'swift' && _this.running == true)
              return;

          if (!_this.startBackgroundDaemon())
          {
              this.onBGError();
              return;
          }
          _this.running=true;
          // swift backend
          if (_this.backend == 'swift')
          {
              // Send GET /roothash?query to swift process
              var video_url = 'http://127.0.0.1:'+_this.swift_http_port+'/'+_this.swift_path_query;
	      
              // Give process time to start and listen
              var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
              timer.initWithCallback(function() { dataListener.onPlay(video_url); },
                                 1000, Ci.nsITimer.TYPE_ONE_SHOT);
          }
          else
          {
              // Retry connect after 1 sec
              var timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
              timer.initWithCallback(function() { _this.asyncOpen(aListener, aContext) },
                                 1000, Ci.nsITimer.TYPE_ONE_SHOT);
          }
        }
        else 
        {
          LOG('BackgroundProcess closed Control connection\n');
          this.onBGError();
        }
      },
      onDataAvailable: function(request, context, inputStream, offset, count) {
        var sInputStream = Cc["@mozilla.org/scriptableinputstream;1"].createInstance(Ci.nsIScriptableInputStream);
        sInputStream.init(inputStream);

        var s = sInputStream.read(count).split('\r\n');
        
        for(var i=0;i<s.length;i++) {
          var cmd = s[i];
          if (cmd.substr(0,4) == 'PLAY') {
            var video_url = cmd.substr(5);
            this.onPlay(video_url);
            break;
          }
          if (cmd.substr(0,5) == "ERROR") {
            LOG('ERROR in BackgroundProcess\n');
            this.onBGError();
            break;
          }
        }
      },
      onBGError: function() {
            // Arno: It's hard to figure out how to throw an exception here
            // that causes FX to fail over to alternative <source> elements
            // inside the <video> element. The hack that appears to work is
            // to create a Channel to some URL that doesn't exist.
    	    //
    	    // 2010-11-22, This implements pre-play fallback, as Firefox won't 
    	    // fall back to another <source> when the first source has delivered
    	    // some data. See onPlay for intra-play fallback.
            //
            var fake_video_url = 'http://127.0.0.1:6877/createxpierror.html';
            var ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
            var video_channel = ios.newChannel(fake_video_url, null, null);
            video_channel.asyncOpen(aListener, aContext);
      },
      onPlay: function(video_url) {

    	  // Start playback of P2P-delivered video
    	  LOG('PLAY !!!!!! '+video_url+'\n');
    	  
    	  //
    	  // Arno, 2010-11-22: Initialize intra-playback HTTP fallback.
    	  //
    	  var fvideo_httpchan = null;
    	  var video_httpchan = null;
    	  // Swift HTTPGW currently returns length rounded up to nearest KB, or
    	  // exact length. Property of swift protocol. FIXME
    	  var fuzzy_len = 0;
    	  var orig_request = null;
          var swiftstopped=false;
    	  
          var replaceListener = {
      
             onStartRequest: function(request, context) 
             {
                 LOG("HTTPalt: onStart: status " + request.status);
                 // + " HTTP " + fvideo_httpchan.responseStatus + "\n");
                 
                 if (request.status == Components.results.NS_OK)
                 {
	                 if (fvideo_httpchan.responseStatus == 206)
	                 {
		                 LOG("HTTPalt: onStart: Content-Range: " + fvideo_httpchan.getResponseHeader("Content-Range") + "\n");
		                 // Don't communicate to sink, we're transparently failing over
	                 }
	                 else
	                 {
	                	 // Because of the fuzzy swift content-length we may request
	                	 // more data than there is. In that case, the HTTP server
	                	 // should respond with 416, range-req not satisfiable.
	                	 // upload.wikipedia.org, however, appears to return 200?!
	                	 // 
	                	 // Close HTTP conn and tell reader we're done.
	                	 LOG("HTTPalt: onStart: Bad HTTP response, aborting failover");
	                	 request.cancel(Components.results.NS_OK);
	                	 //aListener.onStopRequest(orig_request, context, Components.results.NS_OK );
	                 }
                 }
                 else
                 {
                	 // Error contacting fallback server, e.g. NS_ERROR_CONNECTION_REFUSED
                	 LOG("HTTPalt: onStart: Error contacting HTTP fallback server, aborting failover.");
                	 request.cancel(Components.results.NS_OK);
                 }
             },
             onStopRequest: function(request, context, status) 
             {
                 LOG("HTTPalt: onStop\n");
                 // Tell sink that we're definitely done
                 aListener.onStopRequest(request,context,status);
             },
             onDataAvailable: function(request, context, inputStream, offset, count) 
             {
                 //LOG("HTTPalt: onData: off " + offset + " count " + count );
                 // Send replacement data from HTTP to sink
                 aListener.onDataAvailable(request,context,inputStream,offset,count);
             }
          }; // Replace end
      
          var lastoffset=0; 
          var failmonListener = {
      
             onStartRequest: function(request, context) 
             {
                 LOG("Failmon: onStart: status " + request.status + " name " + request.name + "\n");
                 if (request.status == Components.results.NS_OK)
                 {
                	  try
                	  {
                		  LOG("Failmon: onStart: Content-Length: " + video_httpchan.getResponseHeader("Content-Length") + "\n");
                		  fuzzy_len = parseInt(video_httpchan.getResponseHeader("Content-Length"));
                	  }
                	  catch(e)
                	  {
                		  LOG("Failmon: onStart: Content-Length not avail: " + e );
                	  }
                 }
                 // Must always be called! Be mindful of exception above this!
                 aListener.onStartRequest(request,context);
             },
             onStopRequest: function(request, context, status) 
             {
                 LOG("Failmon: onStop: status " + status + "\n");
                 swiftstopped = true;
                 //aListener.onStopRequest(request,context,status);
           
                 // P2P playback failed during playback, failover to HTTP
                 LOG("Failmon: onStop: failing over starting " + lastoffset + " fzlen " + fuzzy_len );
                 if (lastoffset < fuzzy_len)
                 {
                	 // Possibly still data to send (possibly due to fuzzy swift content-length)
                	 // Fail over to HTTP server using Range: request.
                	 
	                 //var fvideo_url = "http://upload.wikimedia.org/wikipedia/en/0/07/Sintel_excerpt.OGG";
	                 //var fvideo_url = "http://127.0.0.1:8061/Sintel_excerpt.OGG";
	                 var fvideo_url = _this.http_url;
	                 var fios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
	                 var fvideo_channel = fios.newChannel(fvideo_url, null, null);
	                 fvideo_httpchan = fvideo_channel.QueryInterface(Components.interfaces.nsIHttpChannel);

	                 orig_request = request;
	                 var rangestr = "bytes="+lastoffset+"-";
	                 fvideo_httpchan.setRequestHeader("Range", rangestr, false );
	                 fvideo_channel.asyncOpen(replaceListener, context);
                 }
                 else
                 {
                	 // Either sent all, or nothing (lastoffset == 0), but
                	 // this can be handled the same, just stop. Causes
                	 // failover to next <source> element in latter case.
                	 //
                	 LOG("Failmon: onStop: sent complete asset, no failover" );
                	 aListener.onStopRequest(request,context,Components.results.NS_OK);
                 }
             },
             onDataAvailable: function(request, context, inputStream, offset, count) 
             {
                 //LOG("Failmon: onData: off " + offset + " count " + count + " stop " + swiftstopped );
                 // Arno, 2010-11-10: I once noticed swift delivering data after onStop. Protect.
                 if (!swiftstopped)
                 {
                     aListener.onDataAvailable(request,context,inputStream,offset,count);
                     lastoffset = offset+count;
                 }
             }
          }; // Failmon end
          
          
          var ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
          var video_channel = ios.newChannel(video_url, null, null);
          video_httpchan = video_channel.QueryInterface(Components.interfaces.nsIHttpChannel);
          if (_this.http_url == '')
          {
        	  // No intra-play HTTP fallback, do normal thing
        	  video_channel.asyncOpen(aListener, aContext);
          }
          else
          {
        	  // Activate intra-play HTTP fallback option
        	  video_channel.asyncOpen(failmonListener, aContext);
          }
          //video_channel.onShutdown(_this.shutdown);
          //cleanup if window is closed
          var windowMediator = Cc["@mozilla.org/appshell/window-mediator;1"].getService(Ci.nsIWindowMediator);
          var nsWindow = windowMediator.getMostRecentWindow("navigator:browser");
          nsWindow.content.addEventListener("unload", function() { _this.shutdown() }, false);
      },
    };
    var pump = Cc["@mozilla.org/network/input-stream-pump;1"].createInstance(Ci.nsIInputStreamPump);
    pump.init(this.inputStream, -1, -1, 0, 0, false);
    pump.asyncRead(dataListener, null);
  },
  startBackgroundDaemon: function() {
      try 
      {
            LOG('BackgroundProcess safe start');
            this.safeStartBackgroundDaemon();
            return true;
      } 
      catch (e) 
      {
          LOG('BackgroundProcess could not be started\n' + e );
          return false;
      }
  },
  safeStartBackgroundDaemon: function() {
    var osString = Cc["@mozilla.org/xre/app-info;1"]
                     .getService(Components.interfaces.nsIXULRuntime).OS;  
    var bgpath = "";
    if (this.backend == 'python')
    {
        if (osString == "WINNT")
            bgpath = 'SwarmEngine.exe';
        else if (osString == "Darwin")
            bgpath = "SwarmPlayer.app/Contents/MacOS/SwarmPlayer";
        else
            bgpath = 'swarmengined';
    }
    else
    {
        // swift backend
        if (osString == "WINNT")
            bgpath = 'swift.exe';
        else if (osString == "Darwin")
            bgpath = "swift"; 
        else
            bgpath = 'swift';
        var urlarg = this.swift_url.substr(0,this.swift_url.indexOf('/'));
    }
   
    var file = __LOCATION__.parent.parent.QueryInterface(Ci.nsILocalFile);
    file.appendRelativePath('bgprocess');
    file.appendRelativePath(bgpath);

    // Debug
    LOG('ARNO Looking for swift in '+file.path+'\n');
    
    
    // Arno, 2010-06-16: Doesn't work on Ubuntu with /usr/share/xul-ext* install      
    try {
        file.permissions = 0755;
    } catch (e) {}
    var process = Cc["@mozilla.org/process/util;1"].createInstance(Ci.nsIProcess);
    process.init(file);
    var args = [];
    if (this.backend == 'python')
    {
        if (tribeLoggingEnabled && osString != "Darwin")
          args.push('debug');
    }
    else
    {
      // swift backend
      args.push('-t');
      args.push(urlarg);
      args.push('-g');
      args.push('0.0.0.0:'+this.swift_http_port);
      // RATELIMIT
      // Arno, 2014-04-02: Temporary disabled. Broken in github/devel branch.
      //args.push('-y');  
      //args.push('1024');
      //args.push('-p');
      args.push('-w');
      // debugging on
      //if (tribeLoggingEnabled && osString != "Darwin")
      //{
      //    args.push('-D');
      //    args.push('log.log'); //dummy argument?
      //}
    }
    process.run(false, args, args.length);
  },
} 

var TribeChannelFactory =
{
  createInstance: function (outer, iid)
  {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;

    if (!iid.equals(Ci.tswiftIChannel) &&
        !iid.equals(Ci.nsIChannel) &&
        !iid.equals(Ci.nsISupports) )
      throw Components.results.NS_ERROR_NO_INTERFACE;

    var tc =  new TribeChannel();
    var tcid = tc.QueryInterface(iid);
    return tcid;
  }
};

/**
* XPCOMUtils.generateNSGetFactory was introduced in Mozilla 2 (Firefox 4).
* XPCOMUtils.generateNSGetModule is for Mozilla 1.9.2 (Firefox 3.6).
*/
if (XPCOMUtils.generateNSGetFactory)
    var NSGetFactory = XPCOMUtils.generateNSGetFactory([TribeChannel]);
else
    var NSGetModule = XPCOMUtils.generateNSGetModule([TribeChannel]);

