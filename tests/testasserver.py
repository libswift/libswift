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
import socket
from traceback import print_exc

DEBUG=True

class TestServerFramework:
    """ 
    Framework class for testing the server-side of Tribler. Can be used to
    control 1 or more swift processes from Python. Subclassed into a 
    unittest.TestCase below.
    """
    def mixSetUpPreSession(self):
   
        self.listenport = random.randint(10001,10999)  
        # NSSA control socket
        self.cmdport = random.randint(11001,11999)  
        # content web server
        self.httpport = random.randint(12001,12999)
        self.statshttpport = None
            
        self.workdir = '.' 
        self.destdir = None
        self.filename = None
        self.scandir = None
        self.zerosdir = None
        self.progress = False
        self.family = socket.AF_INET
        
        self.usegtest = True
        self.binpath = None
        if self.usegtest:
           self.binpath = os.path.join(".","swift4gtest")
           if not os.path.exists(self.binpath):
               self.binpath = None
           
        if self.binpath is None:
           self.binpath = os.path.join("..","swift")

        self.livesourceinput = None
        self.livediscardwindow = None
        self.chunksize = None
        
        self.debug = True
        

    
    def mixSetUp(self):
        """ unittest test setup code """
        # Main UDP listen socket

        if self.family == socket.AF_INET:
            self.inaddrany = "0.0.0.0"
            self.localhost = "127.0.0.1"
            clinaddrany = self.inaddrany
            cllocalhost = self.localhost
        else:
            self.inaddrany = "::0"
            self.localhost = "::1"
            # Swift takes RFC2732 IPv6 addresses on command line
            clinaddrany = "["+self.inaddrany+"]"
            cllocalhost = "["+self.localhost+"]"

        self.hiscmdgwaddr = (self.localhost,self.cmdport)
            
        # Security: only accept commands from localhost, enable HTTP gw, 
        # no stats/webUI web server
        args=[]
        args.append(str(self.binpath))

        if self.usegtest:
            r = "%05d" % ( random.randint(0,99999))
            xmlfile = 'arno'+r+'.out.xml'
            args.append('--gtest_output=xml:'+xmlfile)
            args.append("-G") # less strict cmdline testing
        
        if self.listenport is not None:
            args.append("-l") # listen port
            args.append(clinaddrany+":"+str(self.listenport))
        if self.cmdport is not None:            
            args.append("-c") # command port
            args.append(cllocalhost+":"+str(self.cmdport))
        if self.httpport is not None:
            args.append("-g") # HTTP gateway port
            args.append(cllocalhost+":"+str(self.httpport))
        if self.destdir is not None:
            args.append("-o") # destdir
            args.append(self.destdir)
        if self.filename is not None:
            args.append("-f") # filename
            args.append(self.filename)
        if self.scandir is not None:
            args.append("-d") 
            args.append(self.scandir)
        if self.zerosdir is not None:
            args.append("-e") 
            args.append(self.zerosdir)
        if self.livesourceinput is not None:
            args.append("-i") 
            args.append(self.livesourceinput)
        if self.statshttpport is not None:
            args.append("-s") 
            args.append(cllocalhost+":"+str(self.statshttpport))
        if self.progress is not None:
            args.append("-p")
        if self.livediscardwindow is not None:
            args.append("-W") 
            args.append(str(self.livediscardwindow))
        if self.chunksize is not None:
            args.append("-z") 
            args.append(str(self.chunksize))
            
          
        if self.debug:  
            args.append("-B") # DEBUG Hack        
        
        if DEBUG:
            print >>sys.stderr,"SwiftProcess: __init__: Running",args,"workdir",self.workdir

        self.stdoutfile = tempfile.NamedTemporaryFile(delete=False)       

        if sys.platform == "win32":
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP
        else:
            creationflags=0
        self.popen = subprocess.Popen(args,stdout=self.stdoutfile,cwd=self.workdir,creationflags=creationflags) 

        if DEBUG:
            print >>sys.stderr,"SwiftProcess: sleep to let process start"
        time.sleep(1)

        # Open CMD connection by default
        self.cmdsock = None
        if self.cmdport is not None and self.hiscmdgwaddr is not None:
            print >>sys.stderr,"test: Connect CMDGW",self.hiscmdgwaddr
            self.cmdsock = socket.socket(self.family, socket.SOCK_STREAM)
            self.cmdsock.connect(self.hiscmdgwaddr)

        


    def mixSetUpPostSession(self):
        pass

    def mixTearDown(self):
        """ unittest test tear down code """
        # Causes swift to end, and swift4gtest to exit TEST() call.
        if self.cmdsock is not None:
            self.cmdsock.close()
            # Arno: must sleep to avoid two swift processing using
            # code coverage from writing to the same .gcdna file.
            # http://gcc.gnu.org/onlinedocs/gcc/Invoking-Gcov.html#Invoking-Gcov
            time.sleep(5)
        
        if self.popen is not None:
            self.popen.poll()
            print >>sys.stderr,"test: SwiftProc status",self.popen.returncode
            if self.popen.returncode != 0:
                try:
                    self.popen.kill()
                except:
                    print_exc()
                time.sleep(5)
            
        print >>sys.stderr,"TestAsServer: tearDown: EXIT"

        
        
class TestAsServer(unittest.TestCase,TestServerFramework):
    """ 
    Parent class for testing the server-side of swift
    """
    def setUpPreSession(self):
        return TestServerFramework.mixSetUpPreSession(self)
    
    def setUp(self):
        self.setUpPreSession()
        TestServerFramework.mixSetUp(self)
        self.setUpPostSession()

    def setUpPostSession(self):
        return TestServerFramework.mixSetUpPostSession(self)

    def tearDown(self):
        return TestServerFramework.mixTearDown(self)


class TestAsNPeers(unittest.TestCase):
    """ 
    Parent class for doing tests with multiple swift processes controlled via
    the CMDGW TCP socket interface.
    """
    def getNumPeers(self):
        """ Override this method to increase the number of peers """
        return 2
    
    def setUpPreSession(self):
        self.N = self.getNumPeers()
        self.peers = []
        for i in range(0,self.N):
            self.peers.append(None)
            self.peers[i] = TestServerFramework()
            self.peers[i].mixSetUpPreSession()
    
    def setUp(self):
        self.setUpPreSession()
        for i in range(0,self.N):
            self.peers[i].mixSetUp()
        self.setUpPostSession()

    def setUpPostSession(self):
        for i in range(0,self.N):
            self.peers[i].mixSetUpPostSession()

    def tearDown(self):
        for i in range(0,self.N):
            self.peers[i].mixTearDown()

