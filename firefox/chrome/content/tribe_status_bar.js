
/*
  TSwiftStatuBar - functions for the SwarmPlayer status bar

  Written by Riccardo Petrocco
  see LICENSE.txt for license information
*/

// TODO make async requests using ajax


var TSwiftStatusBar = {
	// Install a timeout handler to install the interval routine

  startup: function()
  {
    this.refreshInformation();
    window.setInterval(this.refreshInformation, 2500);
    this.tswiftChannel = null;
  },


  // Called periodically to refresh traffic information
  refreshInformation: function()
  {

    var httpRequest = null;
    var fullUrl = "http://127.0.0.1:6876/webUI?&{%22method%22:%22get_speed_info%22}";
    var tswiftBar = this;
    var gotInfo = false;
    
    function infoReceived()
    {
	    var tswiftPanel = document.getElementById('tswiftstatusbar');
	    var output = httpRequest.responseText;

	    gotInfo = true;
	    
	    if (output.length)
	    {
		    var resp = JSON.parse(output);

		    if(resp.success) {
		      
		      if (tswiftPanel.src != "chrome://tswift/skin/swarmplugin.png") {
    		    
		        tswiftPanel.src = "chrome://tswift/skin/swarmplugin.png";
  		        //tswiftPanel.onclick = openWebUI;
  		        tswiftPanel.onclick = openAndReuseTab;
    		    tswiftPanel.tooltipText="Click here to access the SwarmPlayer Web Interface"
    		  }
    		  
		      tswiftPanel.label = "Down: " + parseInt(resp.downspeed) + " KB/s, Up: " + parseInt(resp.upspeed) + " KB/s";
            }
	    }
    }
    
    function openWebUI()
        {
          var win = Components.classes['@mozilla.org/appshell/window-mediator;1'].getService(Components.interfaces.nsIWindowMediator).getMostRecentWindow('navigator:browser'); 
          win.openUILinkIn('http://127.0.0.1:6876/webUI', 'tab');
        }
        
    function openAndReuseTab() 
        {
          url = "http://127.0.0.1:6876/webUI";
          var wm = Components.classes["@mozilla.org/appshell/window-mediator;1"]
                             .getService(Components.interfaces.nsIWindowMediator);
          var browserEnumerator = wm.getEnumerator("navigator:browser");

          // Check each browser instance for our URL
          var found = false;
          while (!found && browserEnumerator.hasMoreElements()) {
            var browserWin = browserEnumerator.getNext();
            var tabbrowser = browserWin.gBrowser;

            // Check each tab of this browser instance
            var numTabs = tabbrowser.browsers.length;
            for (var index = 0; index < numTabs; index++) {
              var currentBrowser = tabbrowser.getBrowserAtIndex(index);
              if (url == currentBrowser.currentURI.spec) {

                // The URL is already opened. Select this tab.
                tabbrowser.selectedTab = tabbrowser.tabContainer.childNodes[index];

                // Focus *this* browser-window
                browserWin.focus();

                found = true;
                break;
              }
            }
          }

          // Our URL isn't open. Open it now.
          if (!found) {
            var recentWindow = wm.getMostRecentWindow("navigator:browser");
            if (recentWindow) {
              // Use an existing browser window
              recentWindow.delayedOpenTab(url, null, null, null, null);
            }
            else {
              // No browser windows are open, so open a new one.
              window.open(url);
            }
          }
      }

    
    function restartBG()
    {

      TSwiftStatusBar.startBG();

    }

    function onAbort(e)
    {
    	// Arno, 2010-12-21: Apparently all requests are aborted after 1 s
    	// (to prevent alert when server down I suspect). So this is called
    	// on every request. Unfortunately, onerror handlers are no longer
    	// called. Hence, look if we got info in between, and if not, 
    	// call error handler, i.e., disable status bar icon.
    	//
    	if (gotInfo == false)
    	{
    		restoreBar("ONABORTRESTORE");
    		gotInfo = false;
    	}
    }
    
    function restoreBar(e)
    {
    	var tswiftPanel = document.getElementById('tswiftstatusbar');

        if (tswiftPanel.src != "chrome://tswift/skin/swarmplugin_grey.png") {    
          tswiftPanel.src = "chrome://tswift/skin/swarmplugin_grey.png";
	      tswiftPanel.onclick=restartBG;
	      tswiftPanel.label = " ";
		  tswiftPanel.tooltipText="SwarmPlayer: is not running."
		    
		  TSwiftStatusBar.tswiftChannel = null;
        }
      
    }

    //TODO remove
    function reqTimeout()
    {
        httpRequest.abort();
        return;
        // Note that at this point you could try to send a notification to the
        // server that things failed, using the same xhr object.
    }
    
    try 
    {
        httpRequest = new XMLHttpRequest();
        httpRequest.open("GET", fullUrl, true);
        httpRequest.onload = infoReceived;
        //httpRequest.onerror = restoreBar;
        // Arno, 2010-12-21: See above
        httpRequest.addEventListener("error", restoreBar, false);   
        httpRequest.addEventListener("abort", onAbort, false);   
        httpRequest.send(null);
        
        
        // Timeout to abort in 5 seconds
        //var reqTimeout = setTimeout(reqTimeout(),1000);
        setTimeout(function()
            {
                httpRequest.abort();
                return;
            }
            ,1000);
    }
    catch( err )
    {
        aMsg = ("*** StatusBar : " + err.description);
        Cc["@mozilla.org/consoleservice;1"].getService(Ci.nsIConsoleService).logStringMessage(aMsg);
        dump(aMsg);
    }
  },
  
  startBG: function() {

    if (this.tswiftChannel == null) { 
      var tswiftChannel = Components.classes['@p2pnext.org/tswift/channel;1'].getService().wrappedJSObject;
                                       
      this.tswiftChannel = tswiftChannel;
                                       
    }
    
    if (!tswiftChannel.init) {
      tswiftChannel.startBackgroundDaemon();
    }
    
  },
  
}


window.addEventListener("load", function(e) { TSwiftStatusBar.startup(); }, false);
