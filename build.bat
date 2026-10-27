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
if not exist build\rnnoise mkdir build\rnnoise

pushd src
rc /nologo /fo ..\build\app.res app.rc
set "RC_RESULT=%ERRORLEVEL%"
popd
if not "%RC_RESULT%"=="0" exit /b 1

rem RNNoise (third_party\rnnoise) is C and keeps its own warnings.
cl /nologo /c /std:c11 /O2 /MT /W0 /utf-8 /Ithird_party\rnnoise\include /Fobuild\rnnoise\ third_party\rnnoise\src\*.c
if errorlevel 1 exit /b 1

rem /Zi and /DEBUG write build\OpenChatMix.pdb beside the executable, for reading crash and hang dumps; the
rem executable names only the file, not this folder.
cl /nologo /std:c++20 /O2 /MT /W4 /EHsc /utf-8 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /Ithird_party\vst3 /Zi ^
    /Fobuild\ /Fdbuild\ /Febuild\OpenChatMix.exe src\*.cpp src\ui\*.cpp build\rnnoise\*.obj build\app.res ^
    /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF /DEBUG /INCREMENTAL:NO /PDBALTPATH:%%_PDB%% ^
    dbghelp.lib user32.lib gdi32.lib shell32.lib ole32.lib setupapi.lib cfgmgr32.lib hid.lib advapi32.lib wtsapi32.lib ^
    d2d1.lib dwrite.lib windowscodecs.lib dwmapi.lib shcore.lib version.lib avrt.lib dxgi.lib runtimeobject.lib winsqlite3.lib winhttp.lib bcrypt.lib ws2_32.lib oleaut32.lib mmdevapi.lib
if errorlevel 1 exit /b 1

echo Built build\OpenChatMix.exe
