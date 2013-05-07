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
from swiftconn import *

DEBUG=False


class TestLiveDownloadFramework(TestAsNPeers):
    """
    Framework for a test that starts 2 swift process which do a live streaming transfer
    """
    def setUpPreSession(self):
        TestAsNPeers.setUpPreSession(self)

        # Setup peer0 as source
        self.peers[0].destdir = '.'

        f = open("liveinput.dat","wb")
        for i in range(0,1017):
            data = chr((ord('a')+i)%256) * 1024
            f.write(data)
        f.close()

        self.peers[0].listenport = 6778

        self.peers[0].livesourceinput = "liveinput.dat"
        self.peers[0].filename = "storage.dat"
        
        # Start peer1 as live downloader
        self.peers[1].destdir = '.'
        
        self.peers[1].usegtest = False
        #self.peers[1].binpath = os.path.join("..","Debug","SwiftUMT.exe")

        self.peers[0].debug = True
        self.peers[1].debug = True

        # For CMDGW communication for peer1        
        self.buffer = ''
        self.stop = False
        
    def setUpPostSession(self):
        TestAsNPeers.setUpPostSession(self)

    def tearDown(self):
        
        time.sleep(100)
        
        TestAsNPeers.tearDown(self)
        try:
            os.remove(self.peers[0].livesourceinput)
        except:
            pass
        try:
            os.remove(self.peers[0].filename)
        except:
            pass


class TestLiveDownloadTests: # subclassed below
    """ Actual tests """

    def tst_live_download(self):
        # Start peer1 as live downloader
        liveswarmid = "e5a12c7ad2d8fab33c699d1e198d66f79fa610c3"
        CMD = "START tswift://127.0.0.1:"+str(self.peers[0].listenport)+"/"+liveswarmid+"@-1 "+self.peers[1].destdir+"\r\n"
        self.peers[1].cmdsock.send(CMD)

        # Let peers interact
        self.gotcallback = False
        self.got10k = False
        self.count = 0
        self.process_cmdsock(self.peers[1].cmdsock,self.live_readline)
        self.assertTrue(self.gotcallback)
        self.assertTrue(self.got10k)
        
    def live_readline(self,caller,cmd):
        self.gotcallback = True
        print >>sys.stderr,"test: live_readline: Got",`cmd`
        if cmd.startswith("INFO"):
            
            # Safety catch
            self.count += 1
            if self.count > 100:
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
                if seqcomp > 10*1024:
                    self.got10k = True
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


class TestLiveDownloadDiscardNone(TestLiveDownloadFramework,TestLiveDownloadTests):
    """
    Test with live discard window None (i.e., source remembers all)
    """
    def test_live_download(self):
        self.tst_live_download()


class TestLiveDownloadDiscardWrap128(TestLiveDownloadFramework,TestLiveDownloadTests):
    """
    Test with live discard window set for both peers. Should give same output
    as with no discard window.
    """
    def setUpPreSession(self):
        TestLiveDownloadFramework.setUpPreSession(self)
        
        self.peers[0].livediscardwindow = 128
        self.peers[1].livediscardwindow = 128

    def test_live_download(self):
        self.tst_live_download()

 


    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestLiveDownloadDiscardNone))
    suite.addTest(unittest.makeSuite(TestLiveDownloadDiscardWrap128))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            
