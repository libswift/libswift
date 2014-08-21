#!/bin/sh -x

export XULRUNNER_IDL=$HOME/xulrunner-sdk/idl
export XULRUNNER_XPIDL=$HOME/xulrunner-sdk/bin/xpidl

# ----- Clean up

/bin/rm -rf dist

# ----- Build

echo Build swift first by calling scons

# Diego: building the deepest dir we get all of them.
mkdir -p dist/installdir/bgprocess

# Arno: Move swift binary to installdir
cp swift dist/installdir/bgprocess

cp LICENSE dist/installdir/LICENSE.txt

# ----- Build XPI of SwarmTransport
mkdir -p dist/installdir/components
cp firefox/icon.png dist/installdir
cp firefox/install.rdf dist/installdir
cp firefox/chrome.manifest dist/installdir
cp -r firefox/components dist/installdir
cp -r firefox/chrome dist/installdir
cp -r firefox/skin dist/installdir
rm -rf `find dist/installdir -name .svn`

# ----- Turn .idl into .xpt
$XULRUNNER_XPIDL -m typelib -w -v -I $XULRUNNER_IDL -e dist/installdir/components/tribeIChannel.xpt firefox/tribeIChannel.idl
$XULRUNNER_XPIDL -m typelib -w -v -I $XULRUNNER_IDL -e dist/installdir/components/tribeISwarmTransport.xpt firefox/tribeISwarmTransport.idl

cd dist/installdir
# ----- Turn installdir into .xpi
zip -9 -r SwarmPlayer.xpi * 
mv SwarmPlayer.xpi ..
cd ../..

