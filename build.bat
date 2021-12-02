@echo off

if not exist build ( mkdir build )
if not exist build\debug ( mkdir build\debug )
if not exist build\release ( mkdir build\release )

set cflags=/nologo /Zi /std:c++20 /EHsc /Wall /WX /wd5039
set lflags=/INCREMENTAL:NO /subsystem:windows

pushd build\debug
echo ----------------
echo building debug:
cl %cflags% /Feclock_debug.exe /Od /DCLOCK_DEBUG ..\..\src\main.cpp /link %lflags%
del *.obj
popd

pushd build\release
echo ----------------
echo building release:
cl %cflags% /Feclock.exe /Oi /O2 ..\..\src\main.cpp /link %lflags%
del *.obj
popd
