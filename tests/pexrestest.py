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


class TestPexRes(TestAsServer):

    def test_reply(self):
        
        myaddr = ("127.0.0.1",5353)
        # Fake peer to send as PEX_RES
        myaddr2 = ("127.0.0.1",5352)
        
        hisaddr = ("127.0.0.1",self.listenport)
        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        swarmid = binascii.unhexlify('24aa9484fbee33564fc197252c7c837ce4ce449a')
        
        # Setup listen socket
        self.listensock = Socket(myaddr)

        # Setup listen socket for fake peer
        self.listensock2 = Socket(myaddr2)

        
        # Tell swift to DL swarm via CMDGW
        print >>sys.stderr,"test: Connect CMDGW",hiscmdgwaddr
        self.cmdsock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.cmdsock.connect(hiscmdgwaddr)

        CMD = "START tswift://"+myaddr[0]+":"+str(myaddr[1])+"/"+binascii.hexlify(swarmid)+"\r\n"
        
        self.cmdsock.send(CMD)
        
        [s,d] = self.listensock.listen(swarmid,hs=False) # do not send HS, we need hischanid first
        # Process his handshake and other
        s.c.recv(d)
        
        # Send HANDSHAKE
        d = s.makeDatagram()
        d.add( HandshakeMessage(s.c.get_my_chanid(),POPT_VER_PPSP,swarmid) )

        s.c.send(d)
        
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
        d.add( PexResv4Message(IPv4Port(myaddr2)) )
        s.c.send(d)

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
    
    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestPexRes))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            