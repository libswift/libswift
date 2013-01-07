# Written by Arno Bakker
# see LICENSE.txt for license information

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

from activatetest import TestDirSeedFramework
from swiftconn import *

DEBUG=False


class TestDirSeed(TestDirSeedFramework):

    def test_pex_req(self):
        myaddr = ("127.0.0.1",15353)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # use anita.ts
        fidx = 0
        swarmid = self.filelist[fidx][2]
        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Reply something, so connection is established
        d = s.makeDatagram()
        d.add( HaveMessage(ChunkRange(0,0)) )
        s.send(d)


        # Create new channel and send PEX_REQ
        myaddr2 = ("127.0.0.1",15352)
        
        s2 = SwiftConnection(myaddr2,hisaddr,swarmid)
        
        # Receive HANDSHAKE
        d = s2.recv()
        s2.c.recv(d)
        
        # Send PEX_REQ
        d = s2.makeDatagram()
        d.add( PexReqMessage() )
        s2.send(d)
        
        # PEX_RES should be for our first connection
        d2 = s2.recv()
        responded = False
        while True:
            msg = d2.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Parsed",`msg` 
            if msg.get_id() == MSG_ID_PEX_RESv4:
                self.assertEquals(IPv4Port(myaddr).to_bytes(),msg.ipp.to_bytes())
                responded = True
                
        self.assertTrue(responded)

    
    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestDirSeed))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            