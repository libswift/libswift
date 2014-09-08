# Written by Victor Grishchenko, Arno Bakker 
# see LICENSE.txt for license information
#
# Requirements:
#  - scons: Cross-platform build system    http://www.scons.org/
#  - libevent2: Event driven network I/O   http://www.libevent.org/
#    * Set install path below >= 2.0.17
# For unittests:
#  - googletest: Google C++ Test Framework http://code.google.com/p/googletest/
#       * Set install path in tests/SConscript
#


import os
import sys

DEBUG = True
#CODECOVERAGE = (DEBUG and True)
CODECOVERAGE = False
WITHOPENSSL = True

TestDir = u"tests"

target = 'swift'
source = [ 'bin.cpp', 'binmap.cpp', 'sha1.cpp','hashtree.cpp',
    	   'transfer.cpp', 'channel.cpp', 'sendrecv.cpp', 'send_control.cpp', 
    	   'compat.cpp','avgspeed.cpp', 'avail.cpp', 'cmdgw.cpp', 'httpgw.cpp',
           'storage.cpp', 'zerostate.cpp', 'zerohashtree.cpp',
           'api.cpp', 'content.cpp', 'live.cpp', 'swarmmanager.cpp', 
           'address.cpp', 'livehashtree.cpp', 'livesig.cpp', 'exttrack.cpp']
# cmdgw.cpp now in there for SOCKTUNNEL

env = Environment()
if sys.platform == "win32":
    # get default environment
    include = os.environ.get("INCLUDE", u"")
    libpath = os.environ.get("LIBPATH", u"")
    cxxpath = os.environ.get('CXXPATH', u"")

    # "MSVC works out of the box". Sure.
    # Make sure scons finds cl.exe, etc.
    env.Append ( ENV = { 'PATH' : os.environ['PATH'] } )

    # Make sure scons finds std MSVC include files
    if not include:
        print "swift: Please run scons in a Visual Studio Command Prompt"
        sys.exit(-1)

    # some library dir settings
    LIBEVENT2_PATH = u"\\build\\libevent-2.0.20-stable-debug"
    if not os.path.exists(LIBEVENT2_PATH):
        LIBEVENT2_PATH = u"\\build\\libevent-2.0.19-stable"
    if not os.path.exists(LIBEVENT2_PATH):
        LIBEVENT2_PATH = u"C:\\build\\libevent-2.0.21-stable"

    if WITHOPENSSL:
        OPENSSL_PATH = u"C:\\OpenSSL-Win32"
        if not os.path.exists(OPENSSL_PATH):
            OPENSSL_PATH = u"C:\\build\\openssl-1.0.1f"

    include += LIBEVENT2_PATH + u"\\include;"
    include += LIBEVENT2_PATH + u"\\WIN32-Code;"
    libpath += LIBEVENT2_PATH + u"\\lib;"
    libpath += LIBEVENT2_PATH + u";"
    if WITHOPENSSL:
        include += OPENSSL_PATH + u"\\include;"
        libpath += OPENSSL_PATH + u"\\lib;"
    env.Append ( ENV = { 'INCLUDE' : include } )

    cxxpath += include
    if DEBUG:
        env.Append(CXXFLAGS="/Zi /MTd")
        env.Append(LINKFLAGS="/DEBUG")
    else:
        env.Append(CXXFLAGS="/DNDEBUG") # disable asserts
    if WITHOPENSSL:
        env.Append(CXXFLAGS="/DOPENSSL")

    env.Append(CXXPATH=cxxpath)
    env.Append(CPPPATH=cxxpath)

    # getopt for win32
    source += [u'getopt.c', u'getopt_long.c']

    # Set libs to link to
    # Advapi32.lib for CryptGenRandom in evutil_rand.obj
    libs = ['ws2_32', 'libevent', 'Advapi32'] 
    if WITHOPENSSL:
        libs.append('libeay32')
    if DEBUG:
        libs.append('Dbghelp')

    # Somehow linker can't find uuid.lib
    WINSDK_70 = u"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.0"
    WINSDK_70A = u"C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A"
    WINSDK_71A = u"C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.1A"
    WINSDK_80A = u"C:\\Program Files (x86)\\Windows Kits\\8.0"
    WINSDK_81A = u"C:\\Program Files (x86)\\Windows Kits\\8.1"
    if os.path.exists(WINSDK_81A):
        libpath += os.path.join(WINSDK_81A, u"Lib\\winv6.3\\um\\x86") + u";"
    elif os.path.exists(WINSDK_80A):
        libpath += os.path.join(WINSDK_80A, u"Lib\\Win8\\um\\x86") + u";"
    elif os.path.exists(WINSDK_71A):
        libpath += os.path.join(WINSDK_71A, u"Lib") + u";"
    elif os.path.exists(WINSDK_70A):
        libpath += os.path.join(WINSDK_70A, u"Lib") + u";"
    elif os.path.exists(WINSDK_70):
        libpath += os.path.join(WINSDK_70, u"Lib") + u";"
    else:
        print u"swift: Cannot find Windows SDK."
        sys.exit(-1)

    # Make the swift.exe a Windows program not a Console program when used inside another prog
    if not DEBUG:
    	env.Append(LINKFLAGS="/SUBSYSTEM:WINDOWS")

    linkflags = u""

    APPSOURCE = [u'swift.cpp', u'statsgw.cpp', u'getopt.c', u'getopt_long.c']

else:
    # Linux or Mac build
    libevent2path = '/home/arno/pkgs/libevent-2.0.20-stable-debug'
    if WITHOPENSSL:
        opensslpath = '/usr/lib/i386-linux-gnu'

    # Enable the user defining external includes
    cpppath = os.environ.get('CPPPATH', '')
    if not cpppath:
        print "To use external libs, set CPPPATH environment variable to list of colon-separated include dirs"
    cpppath += libevent2path+'/include:'
    env.Append(CPPPATH=".:"+cpppath)
    #env.Append(LINKFLAGS="--static")

    #if DEBUG:
    #    env.Append(CXXFLAGS="-g")

    # Large-file support always
    env.Append(CXXFLAGS="-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE")
    if WITHOPENSSL:
        env.Append(CXXFLAGS="-DOPENSSL")

    # Set libs to link to
    libs = ['stdc++','libevent','pthread']
    if WITHOPENSSL:
        libs.append('ssl')
	libs.append('crypto')

    libpath = os.environ.get('LIBPATH', '')
    if not libpath:
        print "To use external libs, set LIBPATH environment variable to list of colon-separated lib dirs"
    libpath += libevent2path+'/lib:'
    if WITHOPENSSL:
        libpath += opensslpath

    linkflags = '-Wl,-rpath,'+libevent2path+'/lib'
    env.Append(LINKFLAGS=linkflags);

    APPSOURCE=['swift.cpp','statsgw.cpp']

env.Append(LIBPATH=libpath);

if DEBUG:
    env.Append(CXXFLAGS="-DDEBUG")

env.StaticLibrary (
    target='libswift',
    source = source,
    LIBS=libs,
    LIBPATH=libpath )

env.Program(
   target='swift',
   source=APPSOURCE,
   #CPPPATH=cpppath,
   LIBS=['libswift',libs],
   LIBPATH=libpath+':.')

Export("env")
Export("libs")
Export("linkflags")
Export("DEBUG")
Export("CODECOVERAGE")
# Uncomment the following line to build the tests
#SConscript('tests/SConscript')

