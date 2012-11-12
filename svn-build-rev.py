import sys
import subprocess
import time

argstr = "svn info"
pobj = subprocess.Popen(argstr,stdout=subprocess.PIPE,cwd='.',shell=True)

count = 0.0
while count < 10.0: # 10 seconds
    pobj.poll()
    if pobj.returncode is not None:
        break
    time.sleep(1)
    count += 1.0


revstr = pobj.stdout.read()
if len(revstr) == 0:
    print >>sys.stderr,"Error getting svn revision"
else:
    urlprefix = "URL: "
    sidx = revstr.find(urlprefix)
    if sidx == -1:
        print >>sys.stderr,"Error getting svn revision, no URL"
        os._exit(-1)
    eidx = revstr.find("\n",sidx)
    url = revstr[sidx+len(urlprefix):eidx].rstrip()

    revprefix = "Revision: "
    sidx = revstr.find(revprefix)
    if sidx == -1:
        print >>sys.stderr,"Error getting svn revision, no Revision"
        os._exit(-1)
    eidx = revstr.find("\n",sidx)
    rev = revstr[sidx+len(revprefix):eidx].rstrip()
    
    urlrev = url+"@"+rev
    
    f = open("svn-revision.h","w")
    f.write("std::string SubversionRevisionString = \"")
    f.write(urlrev)
    f.write("\";")
    f.close()
    print >>sys.stderr,"Wrote svn revision to svn-revision.h"