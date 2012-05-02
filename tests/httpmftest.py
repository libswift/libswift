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
from traceback import print_exc

DEBUG=False

class TestAsServer(unittest.TestCase):
    """ 
    Parent class for testing the server-side of Tribler
    """
    
    def setUp(self):
        """ unittest test setup code """
        # Main UDP listen socket
        self.setUpPreSession()
        
        # Security: only accept commands from localhost, enable HTTP gw, 
        # no stats/webUI web server
        args=[]
        args.append(str(self.binpath))
        if self.listenport is not None:
            args.append("-l") # listen port
            args.append("0.0.0.0:"+str(self.listenport))
        if self.cmdport is not None:            
            args.append("-c") # command port
            args.append("127.0.0.1:"+str(self.cmdport))
        if self.httpport is not None:
            args.append("-g") # HTTP gateway port
            args.append("127.0.0.1:"+str(self.httpport))
        if self.destdir is not None:
            args.append("-o") # destdir
            args.append(self.destdir)
        if self.filename is not None:
            args.append("-f") # filename
            args.append(self.filename)
            
        args.append("-B") # DEBUG Hack        
        
        if DEBUG:
            print >>sys.stderr,"SwiftProcess: __init__: Running",args,"workdir",self.workdir

        self.stdoutfile = tempfile.NamedTemporaryFile(delete=False)       
        
        #self.popen = subprocess.Popen(args,stdout=subprocess.PIPE,cwd=self.workdir)
        self.popen = subprocess.Popen(args,stdout=self.stdoutfile,cwd=self.workdir) 

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

    def setUpPostSession(self):
        pass

    def tearDown(self):
        """ unittest test tear down code """
        if self.popen is not None:
            self.popen.kill()



MULTIFILE_PATHNAME = "META-INF-multifilespec.txt"

def filelist2spec(filelist):
    # TODO: verify that this gives same sort as C++ CreateMultiSpec
    filelist.sort()    
        
    specbody = ""
    totalsize = 0L
    for pathname,flen in filelist:
        specbody += pathname+" "+str(flen)+"\n"
        totalsize += flen
        
    specsize = len(MULTIFILE_PATHNAME)+1+0+1+len(specbody)
    numstr = str(specsize)
    numstr2 = str(specsize+len(str(numstr)))
    if (len(numstr) == len(numstr2)):
        specsize += len(numstr)
    else:
        specsize += len(numstr)+(len(numstr2)-len(numstr))
    
    spec = MULTIFILE_PATHNAME+" "+str(specsize)+"\n"
    spec += specbody
    return spec
    
    
    
def bytestr2int(b):
    if b == "":
        return None
    else:
        return int(b)
    
    
def rangestr2triple(rangestr,length):
    # Handle RANGE query
    bad = False
    type, seek = string.split(rangestr,'=')
    if seek.find(",") != -1:
        # - Range header contains set, not supported at the moment
        bad = True
    else:
        firstbytestr, lastbytestr = string.split(seek,'-')
        firstbyte = bytestr2int(firstbytestr)
        lastbyte = bytestr2int(lastbytestr)

        if length is None:
            # - No length (live) 
            bad = True
        elif firstbyte is None and lastbyte is None:
            # - Invalid input
            bad = True
        elif firstbyte >= length:
            bad = True
        elif lastbyte >= length:
            if firstbyte is None:
                """ If the entity is shorter than the specified 
                suffix-length, the entire entity-body is used.
                """
                lastbyte = length-1
            else:
                bad = True
        
    if bad:
        return (-1,-1,-1)
    
    if firstbyte is not None and lastbyte is None:
        # "100-" : byte 100 and further
        nbytes2send = length - firstbyte
        lastbyte = length - 1
    elif firstbyte is None and lastbyte is not None:
        # "-100" = last 100 bytes
        nbytes2send = lastbyte
        firstbyte = length - lastbyte
        lastbyte = length - 1
        
    else:
        nbytes2send = lastbyte+1 - firstbyte

    return (firstbyte,lastbyte,nbytes2send)
    
    
    

class TestFrameMultiFileSeek(TestAsServer):
    """
    Framework for multi-file tests.
    """

    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)
        self.cmdport = None
        self.destdir = tempfile.mkdtemp()
        
        print >>sys.stderr,"test: destdir is",self.destdir
        
        self.setUpFileList()
        
        idx = self.filelist[0][0].find("/")
        specprefix = self.filelist[0][0][0:idx]
        
        prefixdir = os.path.join(self.destdir,specprefix)
        os.mkdir(prefixdir)
        
        # Create content
        for fn,s in self.filelist:
            osfn = fn.replace("/",os.sep)
            fullpath = os.path.join(self.destdir,osfn)
            f = open(fullpath,"wb")
            data = fn[len(specprefix)+1] * s
            f.write(data)
            f.close()
        
        # Create spec
        self.spec = filelist2spec(self.filelist)
        
        fullpath = os.path.join(self.destdir,MULTIFILE_PATHNAME)
        f = open(fullpath,"wb")
        f.write(self.spec)
        f.close()
        
        self.filename = fullpath

    def setUpFileList(self):
        self.filelist = []
        # Minimum 1 entry

    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)
        
        # Allow it to write root hash
        time.sleep(2)
        
        f = open(self.stdoutfile.name,"rb")
        output = f.read(1024)
        f.close()
        
        prefix = "Root hash: "
        idx = output.find(prefix)
        if idx != -1:
            self.roothashhex = output[idx+len(prefix):idx+len(prefix)+40]
        else:
            self.assert_(False,"Could not read roothash from swift output")
            
        print >>sys.stderr,"test: setUpPostSession: roothash is",self.roothashhex
        
        self.urlprefix = "http://127.0.0.1:"+str(self.httpport)+"/"+self.roothashhex

    def test_read_all(self):
        
        url = self.urlprefix        
        req = urllib2.Request(url)
        resp = urllib2.urlopen(req)
        data = resp.read()
        
        # Read and compare content
        if data[0:len(self.spec)] != self.spec:
            self.assert_(False,"returned content doesn't match spec")
        offset = len(self.spec)
        for fn,s in self.filelist:
            osfn = fn.replace("/",os.sep)
            fullpath = os.path.join(self.destdir,osfn)
            f = open(fullpath,"rb")
            content = f.read() 
            f.close()
            
            if data[offset:offset+s] != content:
                self.assert_(False,"returned content doesn't match file "+fn )
                
            offset += s
        
        self.assertEqual(offset, len(data), "returned less content than expected" )
        

    def test_read_file0(self):
        wanttup = self.filelist[0]
        self._test_read_file(wanttup)

    def test_read_file1(self):
        if len(self.filelist) > 1:
            wanttup = self.filelist[1]
            self._test_read_file(wanttup)
        
    def test_read_file2(self):
        if len(self.filelist) > 2:
            wanttup = self.filelist[2]
            self._test_read_file(wanttup)

    def _test_read_file(self,wanttup):
        url = self.urlprefix+"/"+wanttup[0]    
        req = urllib2.Request(url)
        resp = urllib2.urlopen(req)
        data = resp.read()
        resp.close()
        
        osfn = wanttup[0].replace("/",os.sep)
        fullpath = os.path.join(self.destdir,osfn)
        f = open(fullpath,"rb")
        content = f.read() 
        f.close()
            
        if data != content:
            self.assert_(False,"returned content doesn't match file "+osfn )
                
        self.assertEqual(len(content), len(data), "returned less content than expected" )

    def test_read_file0_range(self):
        wanttup = self.filelist[0]
        self._test_read_file_range(wanttup,"-2")
        self._test_read_file_range(wanttup,"0-2")
        self._test_read_file_range(wanttup,"2-")
        self._test_read_file_range(wanttup,"4-10")

    def test_read_file1_range(self):
        if len(self.filelist) > 1:
            wanttup = self.filelist[1]
            self._test_read_file_range(wanttup,"-2")
            self._test_read_file_range(wanttup,"0-2")
            self._test_read_file_range(wanttup,"2-")
            self._test_read_file_range(wanttup,"4-10")

    def test_read_file2_range(self):
        if len(self.filelist) > 2:
            wanttup = self.filelist[2]
            self._test_read_file_range(wanttup,"-2")
            self._test_read_file_range(wanttup,"0-2")
            self._test_read_file_range(wanttup,"2-")
            self._test_read_file_range(wanttup,"4-10")


    def _test_read_file_range(self,wanttup,rangestr):
        url = self.urlprefix+"/"+wanttup[0]    
        req = urllib2.Request(url)
        val = "bytes="+rangestr
        req.add_header("Range", val)
        (firstbyte,lastbyte,nbytes) = rangestr2triple(val,wanttup[1])
            
        print >>sys.stderr,"test: Requesting",firstbyte,"to",lastbyte,"total",nbytes,"from",wanttup[0]
            
        resp = urllib2.urlopen(req)
        data = resp.read()
        resp.close()
        
        osfn = wanttup[0].replace("/",os.sep)
        fullpath = os.path.join(self.destdir,osfn)
        f = open(fullpath,"rb")
        content = f.read() 
        f.close()
            
        #print >>sys.stderr,"test: got",`data`
        #print >>sys.stderr,"test: want",`content[firstbyte:lastbyte+1]`
            
        if data != content[firstbyte:lastbyte+1]:
            self.assert_(False,"returned content doesn't match file "+osfn )
                
        self.assertEqual(nbytes, len(data), "returned less content than expected" )


class TestMFSAllAbove1K(TestFrameMultiFileSeek):
    """ 
    Concrete test of files all > 1024 bytes
    """

    def setUpFileList(self):
        self.filelist = []
        self.filelist.append(("MyCollection/anita.ts",1234))
        self.filelist.append(("MyCollection/harry.ts",5000))
        self.filelist.append(("MyCollection/sjaak.ts",24567))


class TestMFS1stSmall(TestFrameMultiFileSeek):
    """ 
    Concrete test with 1st file fitting in 1st chunk (i.e. spec+file < 1024)
    """
    def setUpFileList(self):
        self.filelist = []
        self.filelist.append(("MyCollection/anita.ts",123))
        self.filelist.append(("MyCollection/harry.ts",5000))
        self.filelist.append(("MyCollection/sjaak.ts",24567))


def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestMFSAllAbove1K))
    suite.addTest(unittest.makeSuite(TestMFS1stSmall))
    
    return suite


def main():
    unittest.main(defaultTest='test_suite',argv=[sys.argv[0]])

if __name__ == "__main__":
    main()

        