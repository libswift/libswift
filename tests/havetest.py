# Written by Arno Bakker
# see LICENSE.txt for license information

import unittest

import os
import sys
import tempfile
import random
import socket
import shutil
import time
import subprocess
import urllib2
import string
import binascii
from traceback import print_exc

from testasserver import TestAsServer
from swiftconn import *

DEBUG=False


class TestHave(TestAsServer):
    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)

        self.destdir = os.path.join(os.getcwdu(),"dest")

        try:
            shutil.rmtree(self.destdir)
        except:
            pass
        os.mkdir(self.destdir)
        
    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)
        

    def tearDown(self):
        TestAsServer.tearDown(self)
        #time.sleep(1)
        #shutil.rmtree(self.destdir)


    def test_connect_one(self):
        
        myaddr = ("127.0.0.1",5353)
        hisaddr = ("127.0.0.1",self.listenport)
        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        swarmid = binascii.unhexlify('24aa9484fbee33564fc197252c7c837ce4ce449a')
        
        # Setup listen socket
        self.listensock = Socket(myaddr)
        
        # Tell swift to DL swarm via CMDGW
        print >>sys.stderr,"test: Connect CMDGW",hiscmdgwaddr
        self.cmdsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.cmdsock.connect(hiscmdgwaddr)

        CMD = "START tswift://"+myaddr[0]+":"+str(myaddr[1])+"/"+binascii.hexlify(swarmid)+"\r\n"
        
        self.cmdsock.send(CMD)
        
        [s,d] = self.listensock.listen(swarmid,hs=False) # do not send HS, we need hischanid first
        # Process his handshake and other
        s.c.recv(d)
        
        # Send HAVE
        d = s.makeDatagram()
        d.add( HandshakeMessage(s.c.get_my_chanid(),POPT_VER_PPSP,swarmid) )

        d.add( HaveMessage(ChunkRange(0,6)) )
        s.c.send(d)
        
        d = s.recv()
        responded = False
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            responded = True
            #if msg.get_id() == MSG_ID_REQUEST:
                
        self.assertTrue(responded)
        
        time.sleep(10)
    
    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestHave))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            