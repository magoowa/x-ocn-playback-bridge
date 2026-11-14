@echo off
setlocal
if "%~1"=="" exit /b 2
if not defined VCToolsInstallDir exit /b 2
set "dest=%~f1"
if not exist "%dest%" mkdir "%dest%"
pushd "%dest%"
rc /nologo /fo thumbnail.res "%~dp0thumbnail-version.rc"
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /O2 /MT /LD "%~dp0thumbnail.cpp" thumbnail.res /link /out:XocnThumbnail.dll /def:"%~dp0thumbnail.def" strmiids.lib ole32.lib oleaut32.lib uuid.lib shlwapi.lib gdi32.lib advapi32.lib
set "status=%errorlevel%"
popd
exit /b %status%
