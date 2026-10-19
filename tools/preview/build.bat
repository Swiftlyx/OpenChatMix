@echo off
setlocal
rem Builds build\preview\OpenChatMixPreview.exe: the panel with a simulated headset, to look at every page.
rem Run build.bat first; the RNNoise objects come from it.
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

if not exist build\rnnoise\*.obj (
    echo Run build.bat first.
    exit /b 1
)
if not exist build\preview\obj mkdir build\preview\obj

pushd tools\preview
rc /nologo /fo ..\..\build\preview\preview.res preview.rc
set "RC_RESULT=%ERRORLEVEL%"
popd
if not "%RC_RESULT%"=="0" exit /b 1

rem Every source but main.cpp, which is the app itself.
set "SOURCES="
for %%f in (src\*.cpp) do if /i not "%%~nxf"=="main.cpp" call set "SOURCES=%%SOURCES%% %%f"

cl /nologo /std:c++20 /O2 /MT /W4 /EHsc /utf-8 /permissive- /DUNICODE /D_UNICODE /DNOMINMAX /Ithird_party\vst3 ^
    /Fobuild\preview\obj\ /Febuild\preview\OpenChatMixPreview.exe tools\preview\preview.cpp %SOURCES% src\ui\*.cpp ^
    build\rnnoise\*.obj build\preview\preview.res ^
    /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF ^
    user32.lib gdi32.lib shell32.lib ole32.lib setupapi.lib hid.lib advapi32.lib wtsapi32.lib ^
    d2d1.lib dwrite.lib windowscodecs.lib dwmapi.lib shcore.lib version.lib avrt.lib dxgi.lib runtimeobject.lib winsqlite3.lib winhttp.lib bcrypt.lib ws2_32.lib oleaut32.lib
if errorlevel 1 exit /b 1

echo Built build\preview\OpenChatMixPreview.exe
