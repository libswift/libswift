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
    



class TestMultiFileSeek(TestAsServer):

    def setUpPreSession(self):
        TestAsServer.setUpPreSession(self)
        self.cmdport = None
        self.destdir = tempfile.mkdtemp()
        
        print >>sys.stderr,"test: destdir is",self.destdir
        
        
        specprefix = "MyCollection"
        filelist = []
        filelist.append((specprefix+"/anita.ts",1234))
        filelist.append((specprefix+"/harry.ts",5000))
        filelist.append((specprefix+"/sjaak.ts",24567))
        
        prefixdir = os.path.join(self.destdir,specprefix)
        os.mkdir(prefixdir)
        
        # Create content
        for fn,s in filelist:
            osfn = fn.replace("/",os.sep)
            fullpath = os.path.join(self.destdir,osfn)
            f = open(fullpath,"wb")
            data = fn[0] * s
            f.write(data)
            f.close()
        
        # Create spec
        self.spec = filelist2spec(filelist)
        
        fullpath = os.path.join(self.destdir,MULTIFILE_PATHNAME)
        f = open(fullpath,"wb")
        f.write(self.spec)
        f.close()
        
        self.filelist = filelist
        self.filename = fullpath

    def setUpPostSession(self):
        TestAsServer.setUpPostSession(self)
        
        # Allow it to write
        time.sleep(2)
        print >>sys.stderr,"POPEN READING"
        
        f = open(self.stdoutfile.name,"rb")
        output = f.read(1024)
        f.close()
        
        print >>sys.stderr,"POPEN POST READING"
        
        print >>sys.stderr,"POPEN OUTPUT",output
        
        prefix = "Root hash: "
        idx = output.find(prefix)
        if idx != -1:
            self.roothashhex = output[idx+len(prefix):idx+len(prefix)+40]
        else:
            self.assert_(False,"Could not read roothash from swift output")
            
        print >>sys.stderr,"test: setUpPostSession: roothash is",self.roothashhex
        
        self.urlprefix = "http://127.0.0.1:"+str(self.httpport)+"/"+self.roothashhex
        
    def test_simple(self):
        
        url = self.urlprefix        
        req = urllib2.Request(url)
        resp = urllib2.urlopen(req)
        data = resp.read()
        
        # Create content
        if data[0:len(self.spec)] != self.spec:
            self.assert_(False,"returned content doesn't match spec")
            print >>sys.stderr,"bad spec"
        offset = len(self.spec)
        for fn,s in self.filelist:
            fn.replace("/",os.sep)
            fullpath = os.path.join(self.destdir,fn)
            f = open(fullpath,"rb")
            content = f.read() 
            f.close()
            
            if data[offset:offset+s] != content:
                self.assert_(False,"returned content doesn't match file "+fn )
                print >>sys.stderr,"bad"
                
            offset += s
        
        self.assertEqual(offset, len(data), "returned less content than expected" )
        print >>sys.stderr,"expected",offset,"got",len(data)
        

def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestMultiFileSeek))
    
    return suite

if __name__ == "__main__":
    unittest.main()
        