@echo off
rem Builds icongen.exe and regenerates assets\OpenChatMix.ico and the PNG previews.
setlocal
cd /d "%~dp0"
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
for /f "usebackq delims=" %%i in (`vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist obj mkdir obj
cl /nologo /std:c++20 /O2 /EHsc /utf-8 /DUNICODE /D_UNICODE /Foobj\ /Feobj\icongen.exe icongen.cpp ^
    /link d2d1.lib windowscodecs.lib ole32.lib || exit /b 1
obj\icongen.exe "%~dp0..\..\assets" || exit /b 1
echo Icon written to assets\OpenChatMix.ico
