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
    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)

        self.destdir = '.'

        f = open("liveinput.dat","wb")
        for i in range(0,1017):
            data = chr((ord('a')+i)%256) * 1024
            f.write(data)
        f.close()

        self.livesourceinput = "liveinput.dat"
        self.filename = "storage.dat"
        

        
    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)
        

    def tearDown(self):
        
        # Let source generate chunks
        time.sleep(15)
        
        TestAsServer.tearDown(self)
        try:
            os.remove(self.livesourceinput)
        except:
            pass
        try:
            os.remove(self.filename)
        except:
            pass


    def test_connect_one(self):
        pass

    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestLive))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            
