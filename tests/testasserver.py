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

class TestAsServer(unittest.TestCase):
    """ 
    Parent class for testing the server-side of Tribler
    """
    
    def setUp(self):
        """ unittest test setup code """
        # Main UDP listen socket
        self.setUpPreSession()
        
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
        
        # Security: only accept commands from localhost, enable HTTP gw, 
        # no stats/webUI web server
        args=[]
        args.append(str(self.binpath))
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
        if self.progress is not None:
            args.append("-p") 

            
        args.append("-B") # DEBUG Hack        
        
        if DEBUG:
            print >>sys.stderr,"SwiftProcess: __init__: Running",args,"workdir",self.workdir

        self.stdoutfile = tempfile.NamedTemporaryFile(delete=False)       
        
        #self.popen = subprocess.Popen(args,stdout=subprocess.PIPE,cwd=self.workdir)
        self.popen = subprocess.Popen(args,stdout=self.stdoutfile,cwd=self.workdir) 

        if DEBUG:
            print >>sys.stderr,"SwiftProcess: sleep to let process start"
        time.sleep(1)

        self.setUpPostSession()

    def setUpPreSession(self):
        self.binpath = os.path.join("..","swift") 
        self.listenport = random.randint(10001,10999)  
        # NSSA control socket
        self.cmdport = random.randint(11001,11999)  
        # content web server
        self.httpport = random.randint(12001,12999)
            
        self.workdir = '.' 
        self.destdir = None
        self.filename = None
        self.scandir = None
        self.progress = False
        self.family = socket.AF_INET

    def setUpPostSession(self):
        pass

    def tearDown(self):
        """ unittest test tear down code """
        if self.popen is not None:
            self.popen.kill()
            
        time.sleep(5)
        print >>sys.stderr,"TestAsServer: tearDown: EXIT"

        