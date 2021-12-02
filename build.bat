@echo off

if not exist build ( mkdir build )
if not exist build\debug ( mkdir build\debug )
if not exist build\release ( mkdir build\release )

set cflags=/nologo /Zi /std:c++20 /EHsc /Wall /WX /wd5039
set lflags=/INCREMENTAL:NO /subsystem:windows

rc /nologo /fo resources.res src\resources.rc
copy /Y resources.res build\debug\resources.res 1>nul
copy /Y resources.res build\release\resources.res 1>nul
del resources.res

pushd build\debug
echo ----------------
echo building debug:
cl %cflags% /Feclock_debug.exe /Od /DCLOCK_DEBUG ..\..\src\main.cpp resources.res /link %lflags%
del *.obj
popd

pushd build\release
echo ----------------
echo building release:
cl %cflags% /Feclock.exe /Oi /O2 ..\..\src\main.cpp resources.res /link %lflags%
del *.obj
popd
