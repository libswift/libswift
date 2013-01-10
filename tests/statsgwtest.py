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


class TestStatsGW(TestAsServer):
    """
    Basic test for stats GW (used by SwarmTransport 3000), just to make 
    code coverage happy.
    """
    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)

        self.statshttpport = 8091

        
    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)
        

    def tearDown(self):
        TestAsServer.tearDown(self)

    def test_stats_overview(self):
        url = "http://127.0.0.1:"+str(self.statshttpport)+"/webUI"

        try:        
            req = urllib2.Request(url)
            resp = urllib2.urlopen(req)
        except:
            self.assertTrue(False)
            print_exc()
        
        data = resp.read()
        self.assertTrue(len(data) > 0)
        self.assertTrue(data.find("Swift Web Interface") != -1)


    def test_stats_get_speed_info(self):
        url = "http://127.0.0.1:"+str(self.statshttpport)+"/webUI?&{%22method%22:%22get_speed_info%22}"

        try:        
            req = urllib2.Request(url)
            resp = urllib2.urlopen(req)
        except:
            self.assertTrue(False)
            print_exc()
        
        data = resp.read()
        self.assertTrue(len(data) > 0)
        self.assertTrue(data.find("downspeed") != -1)

    def test_stats_exit(self):
        url = "http://127.0.0.1:"+str(self.statshttpport)+"/webUI/exit"

        try:        
            req = urllib2.Request(url)
            resp = urllib2.urlopen(req)
        except:
            self.assertTrue(False)
            print_exc()
        
        data = resp.read()
        self.assertTrue(len(data) > 0)
        self.assertTrue(data.find("Swift is no longer running") != -1)


    
def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestStatsGW))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

            
