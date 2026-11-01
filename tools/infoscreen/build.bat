@echo off
setlocal
rem Builds build\infoscreen\OpenChatMix-InfoScreen.exe, the optional Info Screen add-on: what plays, the weather and
rem the PC's sensors on the GameDAC's display. It is a program of its own, attached to each release.
cd /d "%~dp0..\.."

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found. Install Visual Studio Build Tools with the C++ workload.
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo MSVC x64 toolset not found.
    exit /b 1
)
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist build\infoscreen\obj mkdir build\infoscreen\obj

pushd tools\infoscreen
rc /nologo /fo ..\..\build\infoscreen\infoscreen.res infoscreen.rc
set "RC_RESULT=%ERRORLEVEL%"
popd
if not "%RC_RESULT%"=="0" exit /b 1

cl /nologo /std:c++20 /O2 /MT /W4 /EHsc /utf-8 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX ^
    /Fobuild\infoscreen\obj\ /Febuild\infoscreen\OpenChatMix-InfoScreen.exe tools\infoscreen\*.cpp src\json.cpp ^
    build\infoscreen\infoscreen.res ^
    /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF ^
    user32.lib gdi32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib winhttp.lib wbemuuid.lib pdh.lib ^
    windowscodecs.lib runtimeobject.lib
if errorlevel 1 exit /b 1

echo Built build\infoscreen\OpenChatMix-InfoScreen.exe
