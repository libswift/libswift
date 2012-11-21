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

CHUNKSIZE=1024
EMPTYHASH='\x00' * 20

def crtup_cmp(x,y):
    a = x[0]
    b = y[0]
    print >>sys.stderr,"crtup_cmp:",a,b
    
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
        print >>sys.stderr,"\n\nNOT YET IMPLEMENTED crtup_cmp a b",a,b


def check_hashes(hashdict,unclelist):
    hash = None
    pair = [ [unclelist[0],hashdict[unclelist[0]]], [unclelist[1],hashdict[unclelist[1]]] ]
    i = 2
    while True: 
        # order left right
        pair.sort(cmp=crtup_cmp) 
        # calc root hash of parent
        print >>sys.stderr,"Comparing",pair[0][0],pair[1][0]
        hash = sha(pair[0][1]+pair[1][1]).digest()
        # calc chunkspec of parent
        crtup = [pair[0][0][0],pair[1][0][1]]
        print >>sys.stderr,"Parent",crtup
        parent = [crtup,hash]
        # repeat recursive hash with parent and its sibling
        if i >= len(unclelist):
            break
        pair = [parent,[unclelist[i],hashdict[unclelist[i]]]]
        i += 1
    return hash


class TestRequest(TestDirSeedFramework):

    def disabled_test_request_one(self):
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
        peaklist = [(0,63)]
        # Uncles for chunk 0. MUST be sorted in the uncle order
        unclelist = [(1,1),(2,3),(4,7),(8,15),(16,31),(32,63)]
        peakunclelist = peaklist + unclelist
        hashdict = {}
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_INTEGRITY:
                crtup = (msg.chunkspec.s,msg.chunkspec.e)
                if crtup in peakunclelist: # test later
                    peakunclelist.remove(crtup)
                hashdict[crtup] = msg.intbytes
            if msg.get_id() == MSG_ID_DATA:
                self.assertEquals(ChunkRange(0,0).to_bytes(),msg.chunkspec.to_bytes())
                filename = self.filelist[fidx][0]
                f = open(filename,"rb")
                
                expchunk = f.read(CHUNKSIZE)
                f.close()
                self.assertEquals(expchunk,msg.chunk)
                hash = sha(expchunk).digest()
                hashdict[(0,0)] = hash

        # See if we got necessary peak + uncle hashes
        self.assertEquals([],peakunclelist)

        # Check peak hashes
        self.assertEquals(hashdict[peaklist[0]],swarmid)

        # See if they add up to the root hash
        gothash = check_hashes(hashdict,[(0,0)]+unclelist)
        self.assertEquals(swarmid,gothash)

        # Send Ack + explicit close
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(0,0),TimeStamp(1234L)) )
        d.add( HandshakeMessage(CHAN_ID_ZERO,None) )
        s.c.send(d)


        time.sleep(5)


    def test_request_two(self):
        myaddr = ("127.0.0.1",5353)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # Request from bill.ts
        fidx = 1 
        swarmid = self.filelist[fidx][2]
        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Request DATA
        d = s.makeDatagram()
        d.add( RequestMessage(ChunkRange(67,67)) )
        s.c.send(d)

        # Recv DATA  
        print >>sys.stderr,"test: Waiting for response"
        time.sleep(1)
        
        # bill.ts is 195.788 chunks, 3 peaks [0,127], ...
        # MUST be sorted low to high level
        peaklist = [(192,195),(128,191),(0,127)]
        # Uncles for chunk 67. MUST be sorted in the uncle order
        unclelist = [(66,66),(64,65),(68,71),(72,79),(80,95),(96,127),(0,63)]
        peakunclelist = peaklist + unclelist
        hashdict = {}
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_INTEGRITY:
                crtup = (msg.chunkspec.s,msg.chunkspec.e)
                if crtup in peakunclelist: # test later
                    peakunclelist.remove(crtup)
                hashdict[crtup] = msg.intbytes
            if msg.get_id() == MSG_ID_DATA:
                self.assertEquals(ChunkRange(67,67).to_bytes(),msg.chunkspec.to_bytes())
                filename = self.filelist[fidx][0]
                f = open(filename,"rb")
                expchunk = f.read(CHUNKSIZE)
                f.close()
                self.assertEquals(expchunk,msg.chunk)
                hash = sha(expchunk).digest()
                hashdict[(67,67)] = hash

        # See if we got necessary peak + uncle hashes
        self.assertEquals([],peakunclelist)

        # Check peak hashes against root hash
        righthash = EMPTYHASH
        i = 0
        # Build up hash tree starting from lowest peak hash, combining it
        # with a right-side empty hash until it has the same size tree as 
        # covered by the next peak, until we have combined with the last peak,
        # in which case the top hash should be the root hash.
        #
        lefthash = hashdict[peaklist[i]]
        gotwidth = peaklist[i][1] - peaklist[i][0] +1
        while True:
            hash = sha(lefthash+righthash).digest()
            gotwidth *= 2
            if i == len(peaklist)-1:
                break
            wantwidth = peaklist[i+1][1] - peaklist[i+1][0] +1
            if gotwidth >= wantwidth:
                # Our tree is now as big as the next available peak,
                # so we can combine those
                i += 1
                lefthash = hashdict[peaklist[i]]
                righthash = hash
            else:
                # Our tree still small, increase by assuming all empty
                # hashes on the right side
                lefthash = hash
                righthash = EMPTYHASH
        gothash = hash
        
        self.assertEquals(swarmid,gothash)

        # See if they add up to the covering peak hash, now that they are OK.
        gothash = check_hashes(hashdict,[(67,67)]+unclelist)
        exphash = hashdict[peaklist[len(peaklist)-1]]
        self.assertEquals(exphash,gothash)

        # Send Ack + explicit close
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(67,67),TimeStamp(1234L)) )
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

            