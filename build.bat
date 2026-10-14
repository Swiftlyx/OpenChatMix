@echo off
setlocal
cd /d "%~dp0"

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
rem vcvars64.bat looks up vswhere.exe on PATH.
set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist build mkdir build

pushd src
rc /nologo /fo ..\build\app.res app.rc
set "RC_RESULT=%ERRORLEVEL%"
popd
if not "%RC_RESULT%"=="0" exit /b 1

cl /nologo /std:c++20 /O2 /MT /W4 /EHsc /utf-8 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX ^
    /Fobuild\ /Febuild\OpenChatMix.exe src\*.cpp src\ui\*.cpp build\app.res ^
    /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF ^
    user32.lib gdi32.lib shell32.lib ole32.lib setupapi.lib hid.lib advapi32.lib wtsapi32.lib ^
    d2d1.lib dwrite.lib windowscodecs.lib dwmapi.lib shcore.lib version.lib avrt.lib dxgi.lib runtimeobject.lib winsqlite3.lib winhttp.lib bcrypt.lib
if errorlevel 1 exit /b 1

echo Built build\OpenChatMix.exe
