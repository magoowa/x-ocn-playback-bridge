@echo off
setlocal
if "%~1"=="" exit /b 2
if "%~2"=="" exit /b 2
if not defined VCToolsInstallDir (
    echo Run from an x64 Visual Studio Developer Command Prompt.
    exit /b 2
)
set "base=%~f1"
set "dest=%~f2"
set "source=%~dp0"
if not exist "%dest%" mkdir "%dest%"
pushd "%dest%"
rc /nologo /fo version.res "%source%version.rc"
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++17 /O2 /EHsc /MT /DUNICODE /D_UNICODE /I"%base%" /LD "%source%filter.cpp" "%base%\*.cpp" version.res /link /OUT:XocnSource.ax /DEF:"%source%filter.def" strmiids.lib ole32.lib oleaut32.lib uuid.lib winmm.lib user32.lib advapi32.lib gdi32.lib d3d11.lib dxgi.lib
set "buildStatus=%errorlevel%"
popd
exit /b %buildStatus%

