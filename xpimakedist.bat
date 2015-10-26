REM @echo off

set XULRUNNER=\build\xulrunner-1.9.1.19
set ZIP7CMD="C:\Program Files\7-Zip\7z.exe"

REM ----- Check for XULRUNNER

IF NOT EXIST %XULRUNNER% (
  echo .
  echo Could not locate the XULRUNNER SDK at %XULRUNNER%.
  echo Please modify this script or install from https://developer.mozilla.org/en/XULRunner
  exit /b
)

REM ----- Check for ZIP7CMD

IF NOT EXIST %ZIP7CMD% (
  echo .
  echo Could not locate the 7-Zip at %ZIP7CMD%.
  echo Please modify this script or install from ww.7-zip.org
  exit /b
)


REM ----- Clean up

rmdir /S /Q dist


REM ----- Build

@echo Build swift.exe first by calling scons

REM Diego: building the deepest dir we get all of them.
mkdir dist\installdir\bgprocess

REM Arno: Move swift binary to installdir
copy swift.exe dist\installdir\bgprocess

copy LICENSE dist\installdir\LICENSE.txt

REM ----- Build XPI of SwarmTransport
mkdir dist\installdir\components
copy firefox\icon.png dist\installdir
copy firefox\install.rdf dist\installdir
copy firefox\chrome.manifest dist\installdir
xcopy firefox\components dist\installdir\components /S /I
xcopy firefox\chrome dist\installdir\chrome /S /I
xcopy firefox\skin dist\installdir\skin /S /I

REM ----- Turn .idl into .xpt
%XULRUNNER%\bin\xpidl -m typelib -w -v -I %XULRUNNER%\idl -e dist\installdir\components\tribeIChannel.xpt firefox\tribeIChannel.idl
%XULRUNNER%\bin\xpidl -m typelib -w -v -I %XULRUNNER%\idl -e dist\installdir\components\tribeISwarmTransport.xpt firefox\tribeISwarmTransport.idl

cd dist\installdir

REM Arno: Win7 gives popup if swift.exe is not signed
"C:\Program Files\Microsoft Platform SDK for Windows Server 2003 R2\Bin\signtool.exe" sign /f c:\build\certs\swarmplayerprivatekey.pfx /p "" /d "SwarmPlugin for Internet Explorer and Firefox" /du "http://www.pds.ewi.tudelft.nl/code.html" /t "http://timestamp.verisign.com/scripts/timestamp.dll" bgprocess\swift.exe

REM ----- Turn installdir into .xpi
%ZIP7CMD% a -tzip "SwarmPlayer.xpi" * -r -mx=9 
move SwarmPlayer.xpi ..
cd ..\..
 

