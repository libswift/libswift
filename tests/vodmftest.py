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

from testasserver import TestAsNPeers
from SwiftDef import SwiftDef
from swiftconn import *

DEBUG=False

class TestVODMultiFile(TestAsNPeers):
    """
    Test that starts 2 swift process which do a video-on-demand streaming transfer
    """

    def setUpPreSession(self):
        TestAsNPeers.setUpPreSession(self)
        
        # Setup peer0 as source
        self.peers[0].destdir = '.'
        
        #self.peers[0].destdir = tempfile.mkdtemp()
        self.peers[0].zerosdir = os.path.join(os.getcwd(),'seeddump')
        try:
            shutil.rmtree(self.peers[0].zerosdir)
        except:
            print_exc()
        os.mkdir(self.peers[0].zerosdir)

        
        print >>sys.stderr,"test: destdir is",self.peers[0].zerosdir
        
        # Create a multi-file swarm
        self.setUpFileList()
        
        idx = self.filelist[0][0].find("/")
        specprefix = self.filelist[0][0][0:idx]
        
        prefixdir = os.path.join(self.peers[0].zerosdir,specprefix)
        os.mkdir(prefixdir)

        sdef = SwiftDef()

        self.vodinputsize = 0L
        
        # Create content
        for fn,s in self.filelist:
            osfn = fn.replace("/",os.sep)
            fullpath = os.path.join(self.peers[0].zerosdir,osfn)
            f = open(fullpath,"wb")
            data = fn[len(specprefix)+1] * s
            f.write(data)
            f.close()
            
            print >>sys.stderr,"test: Creating",fullpath,fn
            sdef.add_content(fullpath,fn)
            self.vodinputsize += s

        self.specfn = sdef.finalize(self.peers[0].binpath,destdir=self.peers[0].zerosdir)
        f = open(self.specfn,"rb")
        self.spec = f.read()
        f.close()

        self.swarmid = sdef.get_id()
        print >>sys.stderr,"test: setUpPreSession: roothash is",binascii.hexlify(self.swarmid)
        
        self.mfdestfn = os.path.join(self.peers[0].zerosdir,binascii.hexlify(self.swarmid))
        
        print >>sys.stderr,"test: setUpPreSession: copy to",self.mfdestfn
        
        shutil.copyfile(self.specfn,self.mfdestfn)
        shutil.copyfile(self.specfn+".mhash",self.mfdestfn+".mhash")
        shutil.copyfile(self.specfn+".mbinmap",self.mfdestfn+".mbinmap")

        # Start peer1 as VOD downloader
        self.peers[1].destdir = 'clientdump'
        try:
            shutil.rmtree(self.peers[1].destdir)
        except:
            pass
        os.mkdir(self.peers[1].destdir)
        
        #self.peers[1].usegtest = False
        #self.peers[1].binpath = os.path.join("..","Debug","SwiftPPSP.exe")

        #self.peers[0].debug = True
        #self.peers[1].debug = False

        # For CMDGW communication for peer1        
        self.buffer = ''
        self.stop = False

    def setUpFileList(self):
        self.filelist = []
        self.filelist.append(("MyCollection/anita.ts",1234))
        self.filelist.append(("MyCollection/harry.ts",5000))
        self.filelist.append(("MyCollection/sjaak.ts",24567))

        
    def setUpPostSession(self):
        TestAsNPeers.setUpPostSession(self)

    def tearDown(self):
        TestAsNPeers.tearDown(self)
        
        fnlist = [self.peers[0].zerosdir,self.peers[1].destdir]
        for fn in fnlist:
            try:
                #shutil.rmtree(fn)
                pass
            except:
                pass


    def test_vod_multifile(self):
        # Start peer1 as VOD downloader
        CMD = "START tswift://127.0.0.1:"+str(self.peers[0].listenport)+"/"+binascii.hexlify(self.swarmid)+" "+self.peers[1].destdir+"\r\n"
        
        print >>sys.stderr,"SENDING",CMD
        
        self.peers[1].cmdsock.send(CMD)
        
        # Let peers interact
        self.gotcallback = False
        self.gotall = False
        self.count = 0
        self.process_cmdsock(self.peers[1].cmdsock,self.vod_readline)
        self.assertTrue(self.gotcallback)
        self.assertTrue(self.gotall)
        
    def vod_readline(self,caller,cmd):
        self.gotcallback = True
        print >>sys.stderr,"test: vod_readline: Got",`cmd`
        if cmd.startswith("INFO"):
            
            # Safety catch. Assuming speed will be high enough
            self.count += 1
            if self.count > 10:
                self.stop = True
                
            try:
                words = cmd.split()
                hashhex = words[1]
                dlstatus = int(words[2])
                pargs = words[3].split("/")
                seqcomp = int(pargs[0])
                dynasize = int(pargs[1])
                if dynasize == 0:
                    progress = 0.0
                else:
                    progress = float(pargs[0])/float(pargs[1])
                dlspeed = float(words[4])
                ulspeed = float(words[5])
                numleech = int(words[6])
                numseeds = int(words[7])
                
                # Very conservative, client sometimes slow to hookin on paella
                print >>sys.stderr,"test: vod_readline: GOT",seqcomp,"/",dynasize,"WANT",self.vodinputsize
                # Arno: dynasize will be larger than input size as that does not include
                # the size of the multi-file specification
                if seqcomp > 0 and seqcomp == dynasize and dynasize > self.vodinputsize:
                    
                    # Compare original and downloaded content
                    for fn,s in self.filelist:
                        osfn = fn.replace("/",os.sep)
                        srcfullpath = os.path.join(self.peers[0].zerosdir,osfn)
                        dstfullpath = os.path.join(self.peers[1].destdir,osfn)
                        
                        print >>sys.stderr,"test: Comparing",srcfullpath
                        
                        f = open(srcfullpath,"rb")
                        g = open(dstfullpath,"rb")
                        for block in range(0,(s+1023/1024)):
                            expdata = f.read(1024)
                            gotdata = g.read(1024)
                            self.assertEquals(expdata,gotdata)
                        f.close()
                        g.close()
                        
                    self.gotall = True
                    self.stop = True
                
            except:
                print_exc()
                self.assertEquals("INFO params","do not match")
            
                
        return 0
        

    # From FastI2I.py
    def process_cmdsock(self,cmdsock,readlinecallback):
        try:
            while not self.stop:
                data = cmdsock.recv(10240)
                if len(data) == 0:
                    break
                self.data_came_in(data,readlinecallback)
        except:
            print_exc()
            cmdsock.close()

    
    def data_came_in(self,data,readlinecallback):
        """ Read \r\n ended lines from data and call readlinecallback(self,line) """
        # data may come in in parts, not lines! Or multiple lines at same time
        
        if DEBUG:
            print >>sys.stderr,"fasti2i: data_came_in",`data`,len(data)

        if len(self.buffer) == 0:
            self.buffer = data
        else:
            self.buffer = self.buffer + data
        self.read_lines(readlinecallback)
        
    def read_lines(self,readlinecallback):
        while True:
            cmd, separator, self.buffer = self.buffer.partition("\r\n")
            if separator:
                if readlinecallback(self, cmd):
                    # 01/05/12 Boudewijn: when a positive value is returned we immediately return to
                    # allow more bytes to be pushed into the buffer
                    self.buffer = "".join((cmd, separator, self.buffer))
                    break
            else:
                self.buffer = cmd
                break



    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestVODMultiFile))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            
