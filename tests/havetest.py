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


class TestHave(TestAsServer):
    """
    Let swift connect to us, a pretend seeder. Send HAVE, and respond
    to its REQUEST.
    """
    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)

        self.destdir = os.path.join(os.getcwdu(),"dest")

        try:
            shutil.rmtree(self.destdir)
        except:
            pass
        os.mkdir(self.destdir)
        
        # Create tree with 6 chunks
        self.chunks = []
        for i in range(0,6+1):
            chunk = chr(i) * 1024
            self.chunks.append(chunk)
            
        self.hashes = {}
        self.hashes[(7,7)] = '\x00' * 20
        for i in range(0,6+1):
            hash = sha(self.chunks[i]).digest()
            self.hashes[(i,i)] = hash
        self.hashes[(0,1)] = sha(self.hashes[(0,0)]+self.hashes[(1,1)]).digest()
        self.hashes[(2,3)] = sha(self.hashes[(2,2)]+self.hashes[(3,3)]).digest()
        self.hashes[(4,5)] = sha(self.hashes[(4,4)]+self.hashes[(5,5)]).digest()
        self.hashes[(6,7)] = sha(self.hashes[(6,6)]+self.hashes[(7,7)]).digest()
        
        self.hashes[(0,3)] = sha(self.hashes[(0,1)]+self.hashes[(2,3)]).digest()
        self.hashes[(4,7)] = sha(self.hashes[(4,5)]+self.hashes[(6,7)]).digest()
        
        self.hashes[(0,7)] = sha(self.hashes[(0,3)]+self.hashes[(4,7)]).digest()
        
    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)
        

    def tearDown(self):
        TestAsServer.tearDown(self)
        #time.sleep(1)
        #shutil.rmtree(self.destdir)


    def test_connect_one(self):
        
        myaddr = ("127.0.0.1",15353)
        hisaddr = ("127.0.0.1",self.listenport)
        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        swarmid = self.hashes[(0,7)]
        
        # Setup listen socket
        self.listensock = Socket(myaddr)

        # self.cmdsock from TestAsServer
        CMD = "START tswift://"+myaddr[0]+":"+str(myaddr[1])+"/"+binascii.hexlify(swarmid)+"\r\n"
        
        self.cmdsock.send(CMD)
        
        [s,d] = self.listensock.listen(swarmid,hs=False) # do not send HS, we need hischanid first
        # Process his handshake and other
        s.c.recv(d)
        
        # Send HANDSHAKE and HAVE
        d = s.makeDatagram()
        d.add( HandshakeMessage(s.c.get_my_chanid(),POPT_VER_PPSP,None,swarmid) )

        d.add( HaveMessage(ChunkRange(0,6)) )
        s.send(d)
        
        # Wait for REQUEST
        d = s.recv()
        responded = False
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            responded = True
            if msg.get_id() == MSG_ID_REQUEST:
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                
        self.assertTrue(responded)
        
        # Simulate sending of chunk (0,0)
        d = s.makeDatagram()
        
        # Send peaks
        d.add( IntegrityMessage(ChunkRange(0,3),self.hashes[(0,3)] ) )
        d.add( IntegrityMessage(ChunkRange(4,5),self.hashes[(4,5)] ) )
        d.add( IntegrityMessage(ChunkRange(6,6),self.hashes[(6,6)] ) )

        # Send uncle
        d.add( IntegrityMessage(ChunkRange(1,1),self.hashes[(1,1)] ) )
        d.add( IntegrityMessage(ChunkRange(2,3),self.hashes[(2,3)] ) )
        d.add( IntegrityMessage(ChunkRange(4,7),self.hashes[(4,7)] ) )
        
        # Send data
        d.add( DataMessage(ChunkRange(0,0),TimeStamp(1234L),self.chunks[0] ) )
        s.send(d)
        
        # Recv ACK and HAVE
        gotack = False
        gothave = False
        
        print >>sys.stderr,"test: Waiting for response"
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            if msg.get_id() == MSG_ID_HAVE:
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                gothave = True
            if msg.get_id() == MSG_ID_ACK:
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                gotack = True

        self.assertTrue(gotack and gothave)

        # Now connect as another peer and see if chunk is advocated
        myaddr2 = ("127.0.0.1",5352)
        s2 = SwiftConnection(myaddr2,hisaddr,swarmid)
        d2 = s2.recv()
        gothave2 = False
        while True:
            msg = d2.get_message()
            if msg is None:
                break 
            if msg.get_id() == MSG_ID_HAVE:
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                gothave2 = True
                
        self.assertTrue(gothave2)


    """
    def disabled_test_choke(self):
        
        myaddr = ("127.0.0.1",15353)
        hisaddr = ("127.0.0.1",self.listenport)
        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        swarmid = self.hashes[(0,7)]
        
        # Setup listen socket
        self.listensock = Socket(myaddr)
        
        # Tell swift to DL swarm via CMDGW
        # self.cmdsock from TestAsServer
        CMD = "START tswift://"+myaddr[0]+":"+str(myaddr[1])+"/"+binascii.hexlify(swarmid)+"\r\n"
        
        self.cmdsock.send(CMD)
        
        [s,d] = self.listensock.listen(swarmid,hs=False) # do not send HS, we need hischanid first
        # Process his handshake and other
        s.c.recv(d)
        
        # Send HAVE + CHOKE
        d = s.makeDatagram()
        d.add( HandshakeMessage(s.c.get_my_chanid(),POPT_VER_PPSP,None,swarmid) )

        d.add( HaveMessage(ChunkRange(0,6)) )
        d.add( ChokeMessage() )
        s.send(d)
        
        d = s.recv()
        gotreq = False
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            if msg.get_id() == MSG_ID_REQUEST:
                gotreq = True
                
        self.assertFalse(gotreq)
        
        time.sleep(5)
        
        # Send UNCHOKE
        d = s.makeDatagram()
        d.add( UnchokeMessage() )
        s.send(d)

        d = s.recv()
        gotreq = False
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            if msg.get_id() == MSG_ID_REQUEST:
                gotreq = True
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                
        self.assertTrue(gotreq)
    """    

    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestHave))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            
