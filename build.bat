@echo off

if not exist build ( mkdir build )
if not exist build\debug ( mkdir build\debug )
if not exist build\release ( mkdir build\release )

set cflags=/nologo /Zi /std:c++20 /EHsc /Wall /WX /wd4100 /wd4710 /wd4711 /wd4820 /wd5039 /wd5045 /wd5246 /DNOMINMAX /DVC_EXTRALEAN /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS
set lflags=/INCREMENTAL:NO /subsystem:windows

rc /nologo /fo resources.res src\resources.rc
copy /Y resources.res build\debug\resources.res 1>nul
copy /Y resources.res build\release\resources.res 1>nul
del resources.res

set sources=..\..\src\main.cpp ..\..\src\common.cpp resources.res

pushd build\debug
echo ----------------
echo building debug:
cl %cflags% /Feclock_debug.exe /Od /DCLOCK_DEBUG %sources% /link %lflags%
del *.obj
popd

pushd build\release
echo ----------------
echo building release:
cl %cflags% /Feclock.exe /Oi /O2 %sources% /link %lflags%
del *.obj
popd
