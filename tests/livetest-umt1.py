# Written by Arno Bakker
# see LICENSE.txt for license information
#
# TODO: 
# - with live discard window

import unittest

import os
import sys
import tempfile
import random
import shutil
import time
import subprocess
import urllib2
import string
import binascii
from traceback import print_exc
from sha import sha

from activatetest import TestDirSeedFramework
from testasserver import TestAsServer
from SwiftDef import SwiftDef
from swiftconn import *

DEBUG=False

CHUNKSIZE=1024

def chunkspeccontain(big,small):
    if big == small:
        return False
    if big[0] <= small[0] and small[1] <= big[1]:
        return True
    else:
        return False


class TestLiveSourceUMT(TestAsServer):
    """
    Test if live source's output is sane: peaks and HAVEs span a 
    consecutive range (assuming no losses)
    """

    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)

        self.destdir = '.'

        f = open("liveinput.dat","wb")
        self.nchunks = 1017
        for i in range(0,self.nchunks):
            data = chr((ord('0')+i)%256) * 1024
            f.write(data)
        f.close()

        self.livesourceinput = "liveinput.dat"
        self.filename = "storage.dat"

        liveswarmidhex = "e5a12c7ad2d8fab33c699d1e198d66f79fa610c3"
        self.liveswarmid = binascii.unhexlify(liveswarmidhex)
        
        self.debug = True
        
        
    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)

    def tearDown(self):
        TestAsServer.tearDown(self)
        try:
            os.remove(self.livesourceinput)
        except:
            pass
        try:
            os.remove(self.filename)
        except:
            pass

    
    def test_monitor_source(self):
        myaddr = ("127.0.0.1",15357)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # Let source start up
        time.sleep(.5)
        
        print >>sys.stderr,"test: Connect as peer"
        s = SwiftConnection(myaddr,hisaddr,self.liveswarmid,cipm=POPT_CIPM_UNIFIED_MERKLE,lsa=POPT_LSA_PRIVATEDNS)

        peaklist = []
        havelist = []
        recvflag=True        
        kacount = 0
        while True:
            try:
                d = s.recv()
            except socket.timeout:
                break
            while True:
                msg = d.get_message()
                if msg is None:
                    break
                print >>sys.stderr,"test: Got",`msg`
                if msg.get_id() == MSG_ID_HANDSHAKE:
                    s.c.set_his_chanid(msg.chanid)
                    self.send_keepalive(s)
                elif msg.get_id() == MSG_ID_HAVE:
                    newhave = (msg.chunkspec.s,msg.chunkspec.e)
                    havelist = self.update_chunklist(newhave, havelist)
                    print >>sys.stderr,"test: havelist",havelist

                    # Must not have HAVEs that are not covered by a signed peak                    
                    for have in havelist:
                        found = False
                        for peak in peaklist:
                            if chunkspeccontain(peak, have) or (peak == have):
                                found = True
                                break
                        self.assertTrue(found)
                    
                        
                elif msg.get_id() == MSG_ID_SIGNED_INTEGRITY:
                    newpeak = (msg.chunkspec.s,msg.chunkspec.e)
                    peaklist = self.update_chunklist(newpeak, peaklist)
                    print >>sys.stderr,"test: peaklist",peaklist
                        
                kacount += 1
                if (kacount % 100) == 0:
                    self.send_keepalive(s)

        self.assertTrue(len(peaklist) > 0)
        self.assertTrue(len(havelist) > 0)

        # Send explicit close
        d = s.makeDatagram()
        d.add( HandshakeMessage(CHAN_ID_ZERO,POPT_VER_PPSP) )
        s.c.send(d)

    def update_chunklist(self,newpeak,peaklist):
        templist = []
        added=False
        for peak in peaklist:
            if not chunkspeccontain(newpeak,peak):
                templist.append(peak)
            elif not added:
                templist.append(newpeak)
                added = True
        if not added:
            templist.append(newpeak)
        olde = -1
        for peak in templist:
            (start,end) = peak
            if olde == -1:
                self.assertEquals(0,start)
            if olde != -1:
                self.assertEquals(olde+1,start)
            olde = end
        return templist

        

    def send_keepalive(self,s):
        print >>sys.stderr,"test: Send keep alive"            
        d2 = s.makeDatagram()
        d2.add( KeepAliveMessage() )
        s.c.send(d2)






        
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestLiveSourceUMT))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            