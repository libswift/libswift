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

from activatetest import TestDirSeedFramework
from SwiftDef import SwiftDef
from swiftconn import *

DEBUG=False


class TestRequest(TestDirSeedFramework):

    def test_request_one(self):
        myaddr = ("127.0.0.1",5353)
        hisaddr = ("127.0.0.1",self.listenport)
        
        # last
        swarmid = self.filelist[len(self.filelist)-1][2]
        
        s = SwiftConnection(myaddr,hisaddr,swarmid)
        
        d = s.recv()
        s.c.recv(d)
        
        # Request DATA
        d = s.makeDatagram()
        d.add( RequestMessage(ChunkRange(0,0)) )
        s.c.send(d)

        # Recv DATA        
        d = s.recv()
        s.c.recv(d)

        time.sleep(5)
    
    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestRequest))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            