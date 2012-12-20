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
    #print >>sys.stderr,"crtup_cmp:",a,b
    
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
        #print >>sys.stderr,"Comparing",pair[0][0],pair[1][0]
        hash = sha(pair[0][1]+pair[1][1]).digest()
        # calc chunkspec of parent
        crtup = (pair[0][0][0],pair[1][0][1])
        #print >>sys.stderr,"Parent",crtup
        parent = [crtup,hash]
        # Add to hashdict, sender will expect this.
        hashdict[crtup] = hash
        # repeat recursive hash with parent and its sibling
        if i >= len(unclelist):
            break
        pair = [parent,[unclelist[i],hashdict[unclelist[i]]]]
        i += 1
    return hash


def check_peak_hashes(hashdict,peaklist):
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
    return hash

    


class TestRequest(TestDirSeedFramework):

    def test_request_one(self):
        myaddr = ("127.0.0.1",15353)
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
        s.send(d)

        # Recv DATA  
        print >>sys.stderr,"test: Waiting for response"
        time.sleep(1)
        
        # clair.ts is 64K exactly
        peaklist = [(0,63)]
        # Uncles for chunk 0. MUST be sorted in the uncle order
        unclelist = [(1,1),(2,3),(4,7),(8,15),(16,31),(32,63)]
        expunclelist = peaklist + unclelist
        hashdict = {}
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_INTEGRITY:
                crtup = (msg.chunkspec.s,msg.chunkspec.e)
                if crtup in expunclelist: # test later
                    expunclelist.remove(crtup)
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
        self.assertEquals([],expunclelist)

        # Check peak hashes
        self.assertEquals(hashdict[peaklist[0]],swarmid)

        # See if they add up to the root hash
        gothash = check_hashes(hashdict,[(0,0)]+unclelist)
        self.assertEquals(swarmid,gothash)

        # Send Ack + explicit close
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(0,0),TimeStamp(1234L)) )
        d.add( HandshakeMessage(CHAN_ID_ZERO,POPT_VER_PPSP) )
        s.c.send(d)




    def test_request_one_middle(self):
        myaddr = ("127.0.0.1",5354)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # Request from bill.ts
        fidx = 1 
        swarmid = self.filelist[fidx][2]
        # bill.ts is 195.788 chunks, 3 peaks [0,127], ...
        # MUST be sorted low to high level
        peaklist = [(192,195),(128,191),(0,127)]

        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Request DATA
        d = s.makeDatagram()
        d.add( RequestMessage(ChunkRange(67,67)) )
        s.c.send(d)

        # Recv hashes and chunk 67
        self.get_bill_67(s,fidx,swarmid,peaklist)

        # Send Ack + explicit close
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(67,67),TimeStamp(1234L)) )
        d.add( HandshakeMessage(CHAN_ID_ZERO,POPT_VER_PPSP) )
        s.c.send(d)



    def get_bill_67(self,s,fidx,swarmid,peaklist):
        
        # Uncles for chunk 67. MUST be sorted in the uncle order
        unclelist = [(66,66),(64,65),(68,71),(72,79),(80,95),(96,127),(0,63)]
        expunclelist = peaklist + unclelist
        hashdict = {}

        # Recv DATA  
        print >>sys.stderr,"test: Waiting for response"
        d = s.recv()

        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_INTEGRITY:
                crtup = (msg.chunkspec.s,msg.chunkspec.e)
                if crtup in expunclelist: # test later
                    expunclelist.remove(crtup)
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
        self.assertEquals([],expunclelist)

        # Check peak hashes against root hash
        gothash = check_peak_hashes(hashdict,peaklist)
        self.assertEquals(swarmid,gothash)

        # See if they add up to the covering peak hash, now that they are OK.
        gothash = check_hashes(hashdict,[(67,67)]+unclelist)
        exphash = hashdict[peaklist[len(peaklist)-1]]
        self.assertEquals(exphash,gothash)

        return hashdict

    def test_request_two(self):
        
        print >>sys.stderr,"test: test_request_two"
        
        myaddr = ("127.0.0.1",5356)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # Request from bill.ts
        fidx = 1 
        swarmid = self.filelist[fidx][2]
        # bill.ts is 195.788 chunks, 3 peaks [0,127], ...
        # MUST be sorted low to high level
        peaklist = [(192,195),(128,191),(0,127)]
        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Request DATA
        d = s.makeDatagram()
        d.add( RequestMessage(ChunkRange(67,68)) )  # ask 2 chunks
        s.c.send(d)

        # Recv hashes and chunk 67 
        hashdict = self.get_bill_67(s,fidx,swarmid,peaklist) # SHOULD process sequentially
        
        # Send Ack 67
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(67,67),TimeStamp(1234L)) )
        s.c.send(d)
        
        # Recv hashes and chunk 68
        self.get_bill_68(s,fidx,swarmid,peaklist,hashdict) # SHOULD process sequentially

        # Send Ack + explicit close
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(67,68),TimeStamp(1234L)) )
        d.add( HandshakeMessage(CHAN_ID_ZERO,POPT_VER_PPSP) )
        s.c.send(d)



    def get_bill_68(self,s,fidx,swarmid,peaklist,hashdict):
        
        # bill.ts is 195.788 chunks, 3 peaks [0,127], ...
        # Uncles for chunk 67. MUST be sorted in the uncle order
        realunclelist = [(69,69),(70,71),(64,67),(72,79),(80,95),(96,127),(0,63)]
        recvunclelist = [(69,69),(70,71)]  # already known is [(64,67),(72,79),(80,95),(96,127),(0,63)]
        expunclelist = recvunclelist # peaklist already sent before

        # Recv DATA  
        print >>sys.stderr,"test: Waiting for response"
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_INTEGRITY:
                crtup = (msg.chunkspec.s,msg.chunkspec.e)
                if crtup in expunclelist: # test later
                    expunclelist.remove(crtup)
                hashdict[crtup] = msg.intbytes
            if msg.get_id() == MSG_ID_DATA:
                self.assertEquals(ChunkRange(68,68).to_bytes(),msg.chunkspec.to_bytes())
                filename = self.filelist[fidx][0]
                f = open(filename,"rb")
                expchunk = f.read(CHUNKSIZE)
                f.close()
                self.assertEquals(expchunk,msg.chunk)
                hash = sha(expchunk).digest()
                hashdict[(68,68)] = hash

        # See if we got necessary peak + uncle hashes
        self.assertEquals([],expunclelist)

        # See if they add up to the covering peak hash, now that they are OK.
        
        gothash = check_hashes(hashdict,[(68,68)]+realunclelist)
        exphash = hashdict[peaklist[len(peaklist)-1]]
        self.assertEquals(exphash,gothash)


    def test_request_two_cancel_2nd(self):
        myaddr = ("127.0.0.1",5356)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # Request from bill.ts
        fidx = 1 
        swarmid = self.filelist[fidx][2]
        # bill.ts is 195.788 chunks, 3 peaks [0,127], ...
        # MUST be sorted low to high level
        peaklist = [(192,195),(128,191),(0,127)]
        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Request DATA
        d = s.makeDatagram()
        d.add( RequestMessage(ChunkRange(67,68)) )  # ask 2 chunks
        s.c.send(d)


        # Cancel 68
        d = s.makeDatagram()
        d.add( CancelMessage(ChunkRange(68,68)) )  # cancel 1 chunk
        s.c.send(d)

        # Recv hashes and chunk 67 
        hashdict = self.get_bill_67(s,fidx,swarmid,peaklist) # SHOULD process sequentially
        
        # Send Ack 67
        d = s.makeDatagram()
        d.add( AckMessage(ChunkRange(67,67),TimeStamp(1234L)) )
        s.c.send(d)
        
        # Now we shouldn't get 68
        gotdata = False
        d = s.recv()
        while True:
            msg = d.get_message()
            if msg is None:
                break
            print >>sys.stderr,"test: Got",`msg`
            if msg.get_id() == MSG_ID_DATA:
                self.assertEquals(ChunkRange(68,68).to_bytes(),msg.chunkspec.to_bytes())
                gotdata = True

        self.assertFalse(gotdata)

        # Send explicit close
        d = s.makeDatagram()
        d.add( HandshakeMessage(CHAN_ID_ZERO,POPT_VER_PPSP) )
        s.c.send(d)


    def test_request_many_cancel_some1(self):
        
        print >>sys.stderr,"test: test_request_many_cancel_some1"
        
        # Request from bill.ts
        fidx = 1
        
        # Ask many chunks
        reqcp = ChunkRange(67,100)  
        
        # Cancel some chunks
        cancelcplist = []
        cancelcplist.append(ChunkRange(69,69))  
        cancelcplist.append(ChunkRange(75,78)) 
        cancelcplist.append(ChunkRange(80,80)) 
        cancelcplist.append(ChunkRange(96,99)) 
            
        # What chunks still expected
        expcplist = []
        expcplist += [ChunkRange(i,i) for i in range(67,69)]
        expcplist += [ChunkRange(i,i) for i in range(70,75)]
        expcplist += [ChunkRange(i,i) for i in range(79,80)]
        expcplist += [ChunkRange(i,i) for i in range(81,96)]
        expcplist += [ChunkRange(100,100)]
        
        self.do_request_many_cancel_some(fidx, reqcp, cancelcplist, expcplist)
        

    def test_request_many_cancel_some2(self):
        
        print >>sys.stderr,"test: test_request_many_cancel_some2"
        
        # Request from clair.ts
        fidx = 2
        
        # Ask many chunks
        reqcp = ChunkRange(0,30)  
        
        # Cancel some chunks
        cancelcplist = []
        cancelcplist.append(ChunkRange(4,5))  
        cancelcplist.append(ChunkRange(8,12)) 
        cancelcplist.append(ChunkRange(16,22)) 
        cancelcplist.append(ChunkRange(24,27))
        cancelcplist.append(ChunkRange(30,30)) 
            
        # What chunks still expected
        expcplist = []
        expcplist += [ChunkRange(i,i) for i in range( 0,3+1)]
        expcplist += [ChunkRange(i,i) for i in range( 6,7+1)]
        expcplist += [ChunkRange(i,i) for i in range(13,15+1)]
        expcplist += [ChunkRange(i,i) for i in range(23,23+1)]
        expcplist += [ChunkRange(i,i) for i in range(28,29+1)]
        
        self.do_request_many_cancel_some(fidx, reqcp, cancelcplist, expcplist)

        
        
    def do_request_many_cancel_some(self,fidx,reqcp,cancelcplist,expcplist):
        myaddr = ("127.0.0.1",5356)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # Request from fidx
        swarmid = self.filelist[fidx][2]
        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Request range of chunks
        d = s.makeDatagram()
        d.add( RequestMessage(reqcp) )  # ask many chunks
        s.send(d)


        # Cancel some chunks
        d = s.makeDatagram()
        for cp in cancelcplist:
            d.add( CancelMessage(cp) )  
        s.send(d)
        
        # Receive uncanceled chunks
        cidx = 0
        for attempt in range(0,reqcp.e - reqcp.s):
            d = s.recv()
            while True:
                expchunkspec = expcplist[cidx]
                msg = d.get_message()
                if msg is None:
                    break
                print >>sys.stderr,"test: Got",`msg`
                if msg.get_id() == MSG_ID_DATA:
                    self.assertEquals(expchunkspec.to_bytes(),msg.chunkspec.to_bytes())
                    cidx += 1

                    # Send ACK
                    d = s.makeDatagram()
                    d.add( AckMessage(expchunkspec,TimeStamp(1234L)) )
                    s.send(d)
                    break
            if cidx == len(expcplist):
                break

        # Should only receive non-CANCELed chunks.
        self.assertEquals(cidx,len(expcplist))

        # Send explicit close
        d = s.makeDatagram()
        d.add( HandshakeMessage(CHAN_ID_ZERO,POPT_VER_PPSP) )
        s.send(d)



    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestRequest))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            