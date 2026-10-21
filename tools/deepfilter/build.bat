@echo off
rem Builds DeepFilterNet-OpenChatMix.dll and DeepFilterNet-NOTICES.txt into build\deepfilter.
rem Requires rustup with the stable x86_64-pc-windows-msvc toolchain (tested with Rust 1.99.1),
rem Visual Studio Build Tools with the C++ workload, and cargo-about 0.9 or later
rem (cargo install cargo-about --locked --features cli).
setlocal
cd /d "%~dp0"

where cargo >nul 2>nul || set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
where cargo >nul 2>nul || (
    echo cargo not found. Install Rust from https://rustup.rs
    exit /b 1
)

for %%i in ("%~dp0..\..\build\deepfilter") do set "OUT=%%~fi"
if not exist "%OUT%" mkdir "%OUT%"
set "CARGO_TARGET_DIR=%OUT%\target"
if defined CARGO_HOME (set "CARGO_DIR=%CARGO_HOME%") else set "CARGO_DIR=%USERPROFILE%\.cargo"
rem Static CRT: the DLL needs no Visual C++ runtime. The path remaps keep local paths, and with them the
rem user name, out of the panic locations stored in the DLL. RUSTFLAGS would override build.rustflags.
set "RUSTFLAGS="
set "FLAGS=build.rustflags=['-C', 'target-feature=+crt-static', '--remap-path-prefix=%CARGO_DIR%=cargo', '--remap-path-prefix=%CD%=deepfilter']"

rem Fetches the sources for every platform in Cargo.lock; cargo-about reads all of them.
cargo fetch --locked
if errorlevel 1 exit /b 1
cargo build --release --frozen --config "%FLAGS%"
if errorlevel 1 exit /b 1
copy /y "%CARGO_TARGET_DIR%\release\deepfilter_openchatmix.dll" "%OUT%\DeepFilterNet-OpenChatMix.dll" >nul
if errorlevel 1 exit /b 1

rem --frozen: Cargo.lock as is and no network, so license texts come from the crate sources only.
cargo about generate --frozen --fail --output-file "%OUT%\DeepFilterNet-NOTICES.txt" about.hbs
if errorlevel 1 (
    echo cargo-about failed. Install it with: cargo install cargo-about --locked --features cli
    exit /b 1
)

certutil -hashfile "%OUT%\DeepFilterNet-OpenChatMix.dll" SHA256 | findstr /v /c:"hash" /c:"CertUtil"
echo Built %OUT%\DeepFilterNet-OpenChatMix.dll
