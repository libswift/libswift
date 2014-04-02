// -*- coding: utf-8 -*-
// vi:si:et:sw=2:sts=2:ts=2
/*
  JavaScript global constructor swarmTransport
  
  Written by Jan Gerber
  see LICENSE.txt for license information
 */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

const Cc = Components.classes;
const Ci = Components.interfaces;

function SwarmTransport() {
}

SwarmTransport.prototype =
{
  classDescription: "tswiftTransport",
  classID: Components.ID("3dfea7b2-52e6-467f-b2c6-19fd6d459bf6"),
  contractID: "@p2pnext.org/tswift/swarmTransport;1",
  QueryInterface: XPCOMUtils.generateQI(
    [Ci.tswiftISwarmTransport,
     Ci.nsISecurityCheckedComponent,
     Ci.nsISupportsWeakReference,
     Ci.nsIClassInfo]),
  _xpcom_factory : SwarmTransportFactory,
  _xpcom_categories : [{
    category: "JavaScript global constructor",
    entry: "tswiftTransport"
  }],
  version: 3000.7,
} 

var SwarmTransportFactory =
{
  createInstance: function (outer, iid)
  {
    if (outer != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;

    if (!iid.equals(Ci.nsIProtocolHandler) &&
        !iid.equals(Ci.nsISupports) )
      throw Components.results.NS_ERROR_NO_INTERFACE;

    return (new SwarmTransport()).QueryInterface(iid);
  }
};

/**
* XPCOMUtils.generateNSGetFactory was introduced in Mozilla 2 (Firefox 4).
* XPCOMUtils.generateNSGetModule is for Mozilla 1.9.2 (Firefox 3.6).
*/
if (XPCOMUtils.generateNSGetFactory)
    var NSGetFactory = XPCOMUtils.generateNSGetFactory([SwarmTransport]);
else
    var NSGetModule = XPCOMUtils.generateNSGetModule([SwarmTransport]);

