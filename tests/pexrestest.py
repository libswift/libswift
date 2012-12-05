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
from sha import sha
from traceback import print_exc

from testasserver import TestAsServer
from swiftconn import *

DEBUG=False


class TestPexResFramework(TestAsServer):

    def do_test_reply(self,family,myaddr,myaddr2,cert=None):     
           
        hiscmdgwaddr = (self.localhost,self.cmdport)
        swarmid = binascii.unhexlify('24aa9484fbee33564fc197252c7c837ce4ce449a')
        
        # Setup listen socket
        self.listensock = Socket(myaddr,family=family)

        # Setup listen socket for fake peer
        self.listensock2 = Socket(myaddr2,family=family)

        
        # Tell swift to DL swarm via CMDGW
        print >>sys.stderr,"test: Connect CMDGW",hiscmdgwaddr
        self.cmdsock = socket.socket(self.family, socket.SOCK_STREAM)
        self.cmdsock.connect(hiscmdgwaddr)

        httptracker = None
        if self.family == socket.AF_INET6:
            httptracker = "["+myaddr[0]+"]:"+str(myaddr[1])
        else:
            httptracker = myaddr[0]+":"+str(myaddr[1])

        CMD = "START tswift://"+httptracker+"/"+binascii.hexlify(swarmid)+"\r\n"
        
        self.cmdsock.send(CMD)
        
        [s,d] = self.listensock.listen(swarmid,hs=False) # do not send HS, we need hischanid first
        # Process his handshake and other
        s.c.recv(d)
        
        # Send HANDSHAKE
        d = s.makeDatagram()
        d.add( HandshakeMessage(s.c.get_my_chanid(),POPT_VER_PPSP,None,swarmid) )

        s.send(d)
        
        # Expect PEX_REQ
        d = s.recv()
        responded = False
        wantpex = False
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            responded = True
            if msg.get_id() == MSG_ID_PEX_REQ:
                wantpex = True
                
        self.assertTrue(wantpex)
        
        # Send PEX_RES 
        d = s.makeDatagram()
        if cert is None:
            if family == socket.AF_INET6:
                d.add( PexResv6Message(IPv6Port(myaddr2)) )
            else:
                d.add( PexResv4Message(IPv4Port(myaddr2)) )
        else:
            d.add( PexResCertMessage(cert) )
        s.send(d)

        # Listen on fake peer socket
        responded = False
        [s2,d2] = self.listensock2.listen(swarmid,hs=False) # do not send HS, we need hischanid first
        while True:
            msg = d2.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            responded = True
            if msg.get_id() == MSG_ID_HANDSHAKE:
                self.assertEquals(swarmid,msg.swarmid)

        # Expect answer on fake peer's socket
        self.assertTrue(responded)    


class TestPexRes4cert(TestPexResFramework):

    def test_reply_v4(self):
        
        myaddr = ("127.0.0.1",5353)
        # Fake peer to send as PEX_RES
        myaddr2 = ("127.0.0.1",5352)
        self.do_test_reply(socket.AF_INET,myaddr,myaddr2)


    def disabled_test_reply_cert(self):
        # TODO: swift PEX cert support
        
        myaddr = ("127.0.0.1",5357)
        # Fake peer to send as PEX_RES
        myaddr2 = ("127.0.0.1",5356)
        cert = '\xab' * 481 
        self.do_test_reply(socket.AF_INET,myaddr,myaddr2,cert=cert)





class TestPexRes6(TestPexResFramework):

    def setUpPreSession(self):
        TestPexResFramework.setUpPreSession(self)
        self.family = socket.AF_INET6

    def test_reply_v6(self):
        # TODO: swift IPv6 support
        
        myaddr = ("::1",5355)
        # Fake peer to send as PEX_RES
        myaddr2 = ("::1",5354)
        self.do_test_reply(socket.AF_INET6,myaddr,myaddr2)

    
def test_suite():
    suite = unittest.TestSuite()
    #suite.addTest(unittest.makeSuite(TestPexRes4cert))
    suite.addTest(unittest.makeSuite(TestPexRes6))
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            