# Written by Arno Bakker
# see LICENSE.txt for license information
#
# TODO: split up test_? LocalRepos create 2

import sys
import os
import unittest
from threading import Event, Thread, currentThread, Condition
from socket import error as socketerror
import time
from traceback import print_exc
import shutil
import random
import socket
import subprocess

from M2Crypto import Rand

from testasserver import TestAsServer

DEBUG = False


NREPEATS = 10


# Thread must come as first parent class!
class UDPListener(Thread):
    def __init__(self,testcase,port):
        Thread.__init__(self)
        self.setDaemon(True)
        self.testcase = testcase
        self.port = port
        
        self.myss = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.myss.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.myss.bind(('', self.port))

        print >>sys.stderr,"test: udp: Bound to port",self.port

    def run(self):
        while True:
            msg = self.myss.recv(5000)
            print >>sys.stderr,"test: udp: Got",len(msg)
            self.testcase.assertEqual(len(msg),4+self.testcase.randsize)
            prefix = msg[0:4]
            data = msg[4:]
            self.testcase.assertEqual(prefix,"\xff\xff\xff\xff")
            self.testcase.assertEqual(data,self.testcase.data)
            self.testcase.notify()


class TestTunnel(TestAsServer):
    """
    Test for swift ability to tunnel data from CMD TCP connections over UDP.
    """
    
    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)
        
        self.cond = Condition()
        
        self.peer1port = 1234
        self.peer1 = UDPListener(self,self.peer1port)
        self.peer1.start()

        self.destdir = "."
        
        self.udpsendport = random.randint(14001,14999) #

        time.sleep(2) # let server threads start


    def notify(self):
        self.cond.acquire()
        self.cond.notify()
        self.cond.release()


    def wait(self):
        self.cond.acquire()
        self.cond.wait()
        self.cond.release()

    def test_tunnel_send(self):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.connect(("127.0.0.1", self.cmdport))

        print >>sys.stderr,"test: Send over TCP, receive on UDP"
        for i in range(0,NREPEATS):        
            self.randsize = random.randint(1,2048)
            self.data = Rand.rand_bytes(self.randsize)
            cmd = "TUNNELSEND 127.0.0.1:"+str(self.peer1port)+"/ffffffff "+str(self.randsize)+"\r\n";
            self.s.send(cmd+self.data)
            # Read at UDPListener
            self.wait()

        print >>sys.stderr,"test: Separate TUNNEL cmd from data on TCP"
        for i in range(0,NREPEATS):        
            self.randsize = random.randint(1,2048)
            self.data = Rand.rand_bytes(self.randsize)
            cmd = "TUNNELSEND 127.0.0.1:"+str(self.peer1port)+"/ffffffff "+str(self.randsize)+"\r\n";
            self.s.send(cmd)
            time.sleep(.1)
            self.s.send(self.data)
            # Read at UDPListener
            self.wait()

        print >>sys.stderr,"test: Add command after TUNNEL"
        for i in range(0,NREPEATS):        
            self.randsize = random.randint(1,2048)
            self.data = Rand.rand_bytes(self.randsize)
            cmd = "TUNNELSEND 127.0.0.1:"+str(self.peer1port)+"/ffffffff "+str(self.randsize)+"\r\n";
            cmd2 = "SETMOREINFO 979152e57a82d8781eb1f2cd0c4ab8777e431012 1\r\n"
            self.s.send(cmd+self.data+cmd2)
            # Read at UDPListener
            self.wait()

        print >>sys.stderr,"test: Send data in parts"
        for i in range(0,NREPEATS):
            self.randsize = random.randint(1,2048)
            self.data = Rand.rand_bytes(self.randsize)
            cmd = "TUNNELSEND 127.0.0.1:"+str(self.peer1port)+"/ffffffff "+str(self.randsize)+"\r\n";
            self.s.send(cmd)
            self.s.send(self.data[0:self.randsize/2])
            self.s.send(self.data[self.randsize/2:])
            # Read at UDPListener
            self.wait()

        print >>sys.stderr,"test: Send UDP, receive TCP"
        self.s2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        totaldata = ''
        for i in range(0,NREPEATS):
            self.randsize = random.randint(1,2048)
            self.data = Rand.rand_bytes(self.randsize)
            
            # Send data over UDP
            print >>sys.stderr,"test: TCP: Sending swift UDP bytes",self.randsize
            swiftmsg = "\xff\xff\xff\xff"+self.data
            nsend = self.s2.sendto(swiftmsg,0,("127.0.0.1",self.listenport))

            # Receive data via TCP 
            print >>sys.stderr,"test: TCP: Recv"
            crlfidx=-1
            while crlfidx == -1:
                gotdata = self.s.recv(5000)
                print >>sys.stderr,"test: TCP: Got cmd bytes",len(gotdata)
                if len(gotdata) == 0:
                    break
                totaldata += gotdata
                crlfidx = totaldata.find('\r\n')
                
            cmd = totaldata[0:crlfidx]
            print >>sys.stderr,"test: TCP: Got cmd",cmd
            
            totaldata = totaldata[crlfidx+2:] # strip cmd
            
            words = cmd.split()
            if words[0] == "TUNNELRECV":
                srcstr = words[1]
                size = int(words[2])
                
                while len(totaldata) < size:
                    gotdata = self.s.recv(5000)
                    print >>sys.stderr,"test: TCP: Got tunnel bytes",len(gotdata)
                    if len(gotdata) == 0:
                        break
                    totaldata += gotdata

                tunneldata=totaldata[0:size]
                totaldata = totaldata[size:]
                self.assertEqual(self.randsize,len(tunneldata))
                self.assertEqual(self.data,tunneldata)
                print >>sys.stderr,"test: TCP: Done"
                
            else:
                self.assertEqual(words[0],"TUNNELRECV")
                
                
                



def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestTunnel))
    
    return suite
    

def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()
