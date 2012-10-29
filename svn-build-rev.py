import sys
import subprocess

argstr = "svn info | findstr Revision:"
pobj = subprocess.Popen(argstr,stdout=subprocess.PIPE,cwd='.',shell=True)

revstr = pobj.stdout.read()
if len(revstr) == 0:
    print >>sys.stderr,"Error getting svn revision"
else:
    revstr = revstr.rstrip()
    f = open("svn-revision.h","w")
    f.write("std::string SubversionRevisionString = \"")
    f.write(revstr)
    f.write("\";")
    f.close()
    print >>sys.stderr,"Wrote svn revision to svn-revision.h"