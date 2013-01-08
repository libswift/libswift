# Written by Arno Bakker
# see LICENSE.txt for license information
#
# TODO:
# - CHECKPOINT
# - Test effect of MAXSPEED
#

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
import urlparse
import string
import binascii
import json
from sha import sha
from traceback import print_exc

from requesttest import TestZeroSeedFramework
from swiftconn import *


DEBUG=False


class TestHave(TestZeroSeedFramework):

    def setUpPreSession(self):
        TestZeroSeedFramework.setUpPreSession(self)

        self.httpport = random.randint(12001,12999)
        self.destdir = self.zerosdir
        
        self.buffer = ''
        self.stop = False
        self.exitwait = 1

    def test_start(self):

        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        self.swarmid = self.filelist[0][2]
        
        # self.cmdsock from TestAsServer
        CMD = "START tswift:/"+binascii.hexlify(self.swarmid)+" "+self.destdir+"\r\n"
        
        self.cmdsock.send(CMD)
        
        self.gotinfo = False
        self.gotplay = False
        self.process_cmdsock(self.start_readline)
        self.assertTrue(self.gotinfo and self.gotplay)
        
    def start_readline(self,caller,cmd):
        print >>sys.stderr,"test: start_readline: Got",`cmd`
        if cmd.startswith("INFO"):
            try:
                words = cmd.split()
                hashhex = words[1]
                dlstatus = int(words[2])
                pargs = words[3].split("/")
                dynasize = int(pargs[1])
                if dynasize == 0:
                    progress = 0.0
                else:
                    progress = float(pargs[0])/float(pargs[1])
                dlspeed = float(words[4])
                ulspeed = float(words[5])
                numleech = int(words[6])
                numseeds = int(words[7])
                
                self.assertEquals(hashhex,binascii.hexlify(self.swarmid))
                self.assertTrue(dlstatus == 2 or dlstatus == 4) # HASHCHECK or SEEDING
                if dlstatus == 4:
                    self.assertEquals(dynasize,self.filelist[0][1])
                    self.assertEquals(progress,1.0)
                    self.gotinfo = True
                else:
                    self.assertEquals(dynasize,0)
                    self.assertEquals(progress,0.0)
                    
                self.assertEquals(dlspeed,0.0)
                self.assertEquals(ulspeed,0.0)
                self.assertEquals(numleech,0)
                self.assertEquals(numseeds,0)
    
                
            except:
                print_exc()
                self.assertEquals("INFO params","do not match")
                
                
        if cmd.startswith("PLAY"):
            words = cmd.split()
            
            hashhex = words[1]
            url = words[2]
            
            self.assertEquals(hashhex,binascii.hexlify(self.swarmid))
            
            tup = urlparse.urlparse(url)
            self.assertEquals(tup.scheme,"http")
            self.assertEquals(tup.hostname,"127.0.0.1")
            self.assertEquals(tup.port,self.httpport)
            self.assertEquals(tup.path,"/"+binascii.hexlify(self.swarmid))
            
            self.gotplay = True
            
        if self.gotinfo and self.gotplay:            
            self.stop = True
        return 0

    def test_start_remove(self):

        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        self.swarmid = self.filelist[0][2]
        
        # self.cmdsock from TestAsServer
        CMD = "START tswift:/"+binascii.hexlify(self.swarmid)+" "+self.destdir+"\r\n"
        
        self.cmdsock.send(CMD)
        
        self.gotcallback = False
        self.process_cmdsock(self.start_remove_readline)
        self.assertTrue(self.gotcallback)
        

    def start_remove_readline(self,caller,cmd):
        print >>sys.stderr,"test: start_remove_readline: Got",`cmd`
        self.gotcallback = True
        
        CMD = "REMOVE "+binascii.hexlify(self.swarmid)+" 1 1\r\n"
        self.cmdsock.send(CMD)
        
        time.sleep(5)
        
        contentfile = os.path.join(self.destdir,binascii.hexlify(self.swarmid))
        self.assertFalse(os.path.exists(contentfile))
        self.assertFalse(os.path.exists(contentfile+".mhash"))
        self.assertFalse(os.path.exists(contentfile+".mbinmap"))
        
        self.stop = True
        return 0

    # TODO CHECKPOINT
    
    def test_maxspeed(self):

        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        self.swarmid = self.filelist[0][2]
        
        # self.cmdsock from TestAsServer
        CMD = "START tswift:/"+binascii.hexlify(self.swarmid)+" "+self.destdir+"\r\n"
        CMD += "MAXSPEED "+binascii.hexlify(self.swarmid)+" UPLOAD 10.0\r\n"
        
        # TODO: actually check speed change, requires different test :-(
        # So this is more a code coverage test.
        
        self.cmdsock.send(CMD)
        
        self.gotinfo = False
        self.gotplay = False
        self.process_cmdsock(self.start_readline)
        self.assertTrue(self.gotinfo and self.gotplay)


    def test_setmoreinfo(self):

        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        self.swarmid = self.filelist[0][2]
        
        # self.cmdsock from TestAsServer
        CMD = "START tswift:/"+binascii.hexlify(self.swarmid)+" "+self.destdir+"\r\n"
        CMD += "SETMOREINFO "+binascii.hexlify(self.swarmid)+" 1\r\n"
        
        self.cmdsock.send(CMD)
        
        self.gotinfo = False
        self.gotplay = False
        self.gotmoreinfo = False
        self.process_cmdsock(self.setmoreinfo_readline)
        self.assertTrue(self.gotinfo and self.gotplay and self.gotmoreinfo)
        
    def setmoreinfo_readline(self,caller,cmd):
        print >>sys.stderr,"test: setmoreinfo_readline: Got",`cmd`
        if cmd.startswith("INFO"):
            self.gotinfo = True
        if cmd.startswith("PLAY"):
            self.gotplay = True
        if cmd.startswith("MOREINFO"):
            try:
                words = cmd.split()
            
                hashhex = words[1]
                self.assertEquals(hashhex,binascii.hexlify(self.swarmid))
                
                jsondata = cmd[len("MOREINFO ")+40+1:]
                midict = json.loads(jsondata)

                self.gotmoreinfo = True
            except:
                print_exc()
                self.assertEquals("MOREINFO","does not contain JSON")
            
        if self.gotinfo and self.gotplay and self.gotmoreinfo:            
            self.stop = True
        return 0


    def test_error(self):

        hiscmdgwaddr = ("127.0.0.1",self.cmdport)
        self.swarmid = self.filelist[0][2]
        
        # self.cmdsock from TestAsServer
        CMD = "START tswift:/BADURL"+binascii.hexlify(self.swarmid)+" "+self.destdir+"\r\n"
        
        self.cmdsock.send(CMD)
        
        self.goterror = False
        self.process_cmdsock(self.error_readline)
        self.assertTrue(self.goterror)
        
    def error_readline(self,caller,cmd):
        print >>sys.stderr,"test: error_readline: Got",`cmd`
        if cmd.startswith("ERROR"):
            words = cmd.split()
            hashhex = words[1]
            self.assertEquals(hashhex,binascii.hexlify('\x00' * 20))
            
            self.goterror = True
            self.stop = True
            
        # Note: swift will close CMD connection 
        
        return 0

    # SHUTDOWN tested via TestAsServer


    # From FastI2I.py
    def process_cmdsock(self,readlinecallback):
        try:
            while not self.stop:
                data = self.cmdsock.recv(10240)
                if len(data) == 0:
                    break
                self.data_came_in(data,readlinecallback)
        except:
            print_exc()
            self.cmdsock.close()

    
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
    suite.addTest(unittest.makeSuite(TestHave))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            
