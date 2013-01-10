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

from testasserver import TestAsServer
from swiftconn import *

DEBUG=False


class TestLive(TestAsServer):
    """
    Basic test that starts a live source which generates ~1000 chunks.
    """
    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)

        self.destdir = '.'

        f = open("liveinput.dat","wb")
        self.nchunks = 1017
        for i in range(0,self.nchunks):
            data = chr((ord('a')+i)%256) * 1024
            f.write(data)
        f.close()

        self.livesourceinput = "liveinput.dat"
        self.filename = "storage.dat"
        

        
    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)
        

    def tearDown(self):
        TestAsServer.tearDown(self)
        try:
            os.remove(self.livesourceinput)
        except:
            pass
        try:
            os.remove(self.filename)
        except:
            pass


    def test_live_source(self):
        # Let source generate chunks
        time.sleep(20)
        
        print >>sys.stderr,"test: Comparing input to storage.dat"
        f = open(self.livesourceinput,"rb")
        g = open(self.filename,"rb")
        for i in range(0,self.nchunks):
            gotdata = f.read(1024)
            expdata = g.read(1024)
            self.assertEquals(gotdata,expdata)
        f.close()
        g.close()
        


    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestLive))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            
