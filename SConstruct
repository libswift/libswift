# Written by Victor Grishchenko, Arno Bakker 
# see LICENSE.txt for license information
#
# Requirements:
#  - scons: Cross-platform build system    http://www.scons.org/
#  - libevent2: Event driven network I/O   http://www.libevent.org/
#    * Install in \build\libevent-2.0.14-stable
# For debugging:
#  - googletest: Google C++ Test Framework http://code.google.com/p/googletest/
#       * Install in \build\gtest-1.4.0
#


import os
import sys

DEBUG = True

TestDir = u"tests"

target = "swift"
source = ['address.cpp', 'api.cpp', 'avail.cpp', 'avgspeed.cpp',
        'bin.cpp', 'binmap.cpp',
        'channel.cpp', 'cmdgw.cpp', 'compat.cpp', 'content.cpp',
        'exttrack.cpp',
        'httpgw.cpp', 'hashtree.cpp',
        'live.cpp', 'livehashtree.cpp', 'livesig.cpp',
        'send_control.cpp', 'sendrecv.cpp', 'sha1.cpp', 'storage.cpp', 'swarmmanager.cpp',
        'transfer.cpp',
        'zerostate.cpp', 'zerohashtree.cpp']
# cmdgw.cpp now in there for SOCKTUNNEL

env = Environment()
if sys.platform == "win32":
    # get default environment
    include = os.environ.get("INCLUDE", "")
    libpath = os.environ.get("LIBPATH", "")

    # environment check
    if not include:
        print u"swift: Please run scons in a Visual Studio Command Prompt"
        sys.exit(-1)

    # "MSVC works out of the box". Sure.
    # Make sure scons finds cl.exe, etc.
    env.Append ( ENV = { 'PATH' : os.environ['PATH'] } )

    # --- libevent2
    libevent2path = "\\build\\libevent-2.0.19-stable"
    if not os.path.exists(libevent2path):
        libevent2path = 'C:\\build\\libevent-2.0.21-stable'
    include += os.path.join(libevent2path, u"include") + u";"
    include += os.path.join(libevent2path, u"WIN32-Code") + u";"
    if libevent2path == u"\\build\\libevent-2.0.19-stable":
        libpath += libevent2path + u";"
    else:
        libpath += os.path.join(libevent2path, u"lib") + u";"

    env.Append ( ENV = { 'INCLUDE' : include } )

    cxxpath = os.environ.get("CXXPATH", "")
    cxxpath += include
    if DEBUG:
        env.Append(CXXFLAGS="/Zi /MTd")
        env.Append(LINKFLAGS="/DEBUG")
    else:
        env.Append(CXXFLAGS="/DNDEBUG") # disable asserts
    env.Append(CXXPATH=cxxpath)
    env.Append(CPPPATH=cxxpath)

    # getopt for win32
    source += [u"getopt.c", u"getopt_long.c"]
    libs = [u"ws2_32", u"libevent", u"Advapi32"]

    # --- gtest
    if DEBUG:
        gtest_dir = u"\\build\\gtest-1.4.0"
        if not os.path.exists(gtest_dir):
            gtest_dir = u"C:\\build\\gtest-1.7.0"

        include += os.path.join(gtest_dir, u"include") + u";"
        libpath += os.path.join(gtest_dir, u"lib") + u";"
        libpath += os.path.join(gtest_dir, u"msvc\\gtest\\Debug") + u";"
        libs += [u"gtestd"]

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

    # TODO: Make the swift.exe a Windows program not a Console program
    if not DEBUG:
    	env.Append(LINKFLAGS="/SUBSYSTEM:WINDOWS")
    
    APPSOURCE=['swift.cpp','httpgw.cpp','statsgw.cpp','getopt.c','getopt_long.c']
    
else:
    libevent2path = '/arno/pkgs/libevent-2.0.15-arno-http'

    # Enable the user defining external includes
    cpppath = os.environ.get('CPPPATH', '')
    if not cpppath:
        print "To use external libs, set CPPPATH environment variable to list of colon-separated include dirs"
    cpppath += libevent2path + '/include:'
    env.Append(CPPPATH=".:"+cpppath)
    #env.Append(LINKFLAGS="--static")

    cxxflags = os.environ.get('CXXFLAGS', '')
    if DEBUG:
        cxxflags += " -g "

    # Large-file support always
    cxxflags += " -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE "
    env.Append(CXXFLAGS=cxxflags)

    # Set libs to link to
    libs = ['stdc++','libevent','pthread']
    libpath = os.environ.get('LIBPATH', '')
    if not libpath:
        print "To use external libs, set LIBPATH environment variable to list of colon-separated lib dirs"
    libpath += libevent2path+'/lib:'

    linkflags = '-Wl,-rpath,' + libevent2path + '/lib'
    env.Append(LINKFLAGS=linkflags);

    APPSOURCE=['swift.cpp','httpgw.cpp','statsgw.cpp']

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
   LIBS=[libs,'libswift'],
   LIBPATH=libpath+':.')

   
Export("env")
Export("libs")
Export("libpath")
Export("DEBUG")
# Arno: uncomment to build tests
#SConscript('tests/SConscript')
