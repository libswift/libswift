# Written by Arno Bakker
#
# Test for concurrent HTTP requests
#
# see LICENSE.txt for license information
#

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
from threading import Event, Thread, currentThread, Condition

from activatetest import TestDirSeedFramework
from httpmftest import rangestr2triple

DEBUG = False

# Thread must come as first parent class!
class HttpRequestor(Thread):
    def __init__(self,addr,path,rangestr):
        Thread.__init__(self)
        self.setDaemon(True)
        self.addr = addr;
        self.path = path
        self.rangestr = rangestr
        self.passed = False
        
    def run(self):
        url = 'http://'+self.addr+self.path    
        req = urllib2.Request(url)
        val = "bytes="+self.rangestr
        req.add_header("Range", val)
            
        print >>sys.stderr,"test: Requesting",self.rangestr
            
        resp = urllib2.urlopen(req)
        data = resp.read()
        resp.close()
        
        
        response_headers = resp.info()
        rangereply = response_headers["Content-Range"]
        words = rangereply.split()
        print >>sys.stderr,"Range reply",rangereply

        if resp.getcode() == 206 and words[1].startswith(self.rangestr):
            self.passed = True


class TestHttpConcurrentRequest(TestDirSeedFramework):

    def setUpPreSession(self):
        TestDirSeedFramework.setUpPreSession(self)
        self.httpport = 8192
        self.destdir = self.scandir
        
        # use bill.ts
        self.fidx = 1
        fn = self.filelist[self.fidx][0]
        swarmid = self.filelist[self.fidx][2]
        swarmidhex = binascii.hexlify(swarmid)
        
        # Copy to swarmid, to allow HTTPGW to swift::Open them
        dir = os.path.dirname(fn)
        dfn = os.path.join(dir,swarmidhex)
        shutil.copyfile(fn,dfn)
        shutil.copyfile(fn+".mbinmap",dfn+".mbinmap")
        shutil.copyfile(fn+".mhash",dfn+".mhash")
        

    def test_concurrent_ios(self):
        
        swarmid = self.filelist[self.fidx][2]
        contentsize = self.filelist[self.fidx][1]
        path = "/"+binascii.hexlify(swarmid)
        
        rangestr = "0-1" 
        req0 = HttpRequestor('127.0.0.1:'+str(self.httpport),path,rangestr)
        
        rangestr = "0-"+str(contentsize-1) 
        req1 = HttpRequestor('127.0.0.1:'+str(self.httpport),path,rangestr)
        
        req0.start()
        req1.start()
        
        time.sleep(5)
        
        self.assertTrue(req0.passed)
        self.assertTrue(req1.passed)
    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestHttpConcurrentRequest))
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

