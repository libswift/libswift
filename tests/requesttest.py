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
from sha import sha

from activatetest import TestDirSeedFramework
from SwiftDef import SwiftDef
from swiftconn import *

DEBUG=False

def crlist_cmp(x,y):
    a = x[0]
    b = y[0]
    
    if a[0] <= b[0] and b[1] <= a[1]: # b contained in a
        return 1 # b smaller
    if b[0] <= a[0] and a[1] <= b[1]: # a contained in b
        return -1 # a smaller
    if a[1] <= b[0]:
        return -1 # a ends before b starts
    if b[1] <= a[0]:
        return 1 # b ends before a starts
    if a == b:
        return 0 
    else:
        print >>sys.stderr,"\n\nNOT YET IMPLEMENTED cmp a b",a,b


def check_hashes(hashlist):
    hash = None
    hashlist.sort(cmp=crlist_cmp)
    pair = [ hashlist[0], hashlist[1] ]
    i = 2
    while i < len(hashlist): # not peak
        # order left right
        pair.sort(cmp=crlist_cmp) 
        # calc root hash of parent
        hash = sha(pair[0][1]+pair[1][1]).digest()
        # calc chunkspec of parent
        crlist = [pair[0][0][0],pair[1][0][1]]
        parent = [crlist,hash]
        # repeat recursive hash with parent and its sibling
        pair = [parent,hashlist[i]]
        i += 1
    return hash


class TestRequest(TestDirSeedFramework):

    def test_request_one(self):
        myaddr = ("127.0.0.1",5353)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # Request from claire.ts
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
        time.sleep(1)
        
        # clair.ts is 64K exactly
        peakunclelist = [[0,63],[32,63],[16,31],[8,15],[4,7],[2,3],[1,1]]
        hashlist = []
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_INTEGRITY:
                crlist = [msg.chunkspec.s,msg.chunkspec.e]
                peakunclelist.remove(crlist)
                hashlist.append([crlist,msg.intbytes])
            if msg.get_id() == MSG_ID_DATA:
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                filename = self.filelist[fidx][0]
                f = open(filename,"rb")
                CHUNKSIZE=1024
                expchunk = f.read(CHUNKSIZE)
                f.close()
                self.assertEquals(expchunk,msg.chunk)
                hash = sha(expchunk).digest()
                hashlist.append([[0,0],hash])

        # See if we got necessary peak + uncle hashes
        self.assertEquals([],peakunclelist)

        # See if they add up to the root hash
        gothash = check_hashes(hashlist)
        self.assertEquals(swarmid,gothash)

        # Send Ack + explicit close
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(0,0),TimeStamp(1234L)) )
        d.add( HandshakeMessage(CHAN_ID_ZERO,None) )
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

            