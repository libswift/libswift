# Written by Arno Bakker, Jie Yang
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
from SwiftDef import SwiftDef
from swiftconn import *

DEBUG=False


class TestRequest(TestDirSeedFramework):

    def test_request_one(self):
        myaddr = ("127.0.0.1",5353)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # last
        fidx = len(self.filelist)-1
        swarmid = self.filelist[fidx][2]
        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Request DATA
        d = s.makeDatagram()
        #killer = ChannelID.from_bytes('\x05\xacH\xa0')
        #d.add( killer )
        d.add( RequestMessage(ChunkRange(0,0)) )
        s.c.send(d)

        # Recv DATA  
        print >>sys.stderr,"test: Waiting for response"
        
        time.sleep(5)
        # Repeat
        d = s.makeDatagram()
        d.add( RequestMessage(ChunkRange(0,0)) )
        s.c.send(d)
              
        peakunclelist = [[0,63],[32,63],[16,31],[8,15],[4,7],[2,3],[1,1]]
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_INTEGRITY:
                crlist = [msg.chunkspec.s,msg.chunkspec.e]
                peakunclelist.remove(crlist)
            if msg.get_id() == MSG_ID_DATA:
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                filename = self.filelist[fidx][0]
                f = open(filename,"rb")
                CHUNKSIZE=1024
                expchunk = f.read(CHUNKSIZE)
                f.close()
                self.assertEquals(expchunk,msg.chunk)

        # See if we got necessary peak + uncle hashes
        self.assertEquals([],peakunclelist)

        # Send Ack
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(0,0),TimeStamp(1234L)) )
        s.c.send(d)


        time.sleep(5)
    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestRequest))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            