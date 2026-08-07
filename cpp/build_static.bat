@echo off
setlocal

REM ============================================================
REM  SJJ-COM-Tool - one-click static single-file build script
REM
REM  What it does:
REM   1. Check MinGW toolchain (w64devkit)
REM   2. If static Qt is not built yet: download sources and build
REM      qtbase / qt5compat / qtserialport (first run only, ~15 min)
REM   3. Build the app statically -> zero-dependency single exe
REM   4. Completed steps are skipped automatically (incremental)
REM
REM  Overridable config (environment variables):
REM    W64DEVKIT          MinGW toolchain dir
REM                       (default: D:\dev\toolchains\w64devkit)
REM    QT_SRC             Qt source dir (default: D:\dev\qt-src)
REM    QT_STATIC_PREFIX   Qt static install dir
REM                       (default: D:\dev\qt-static-6.8.2)
REM ============================================================

set "QT_VERSION=6.8.2"

REM ---------- paths ----------
if "%W64DEVKIT%"=="" set "W64DEVKIT=D:\dev\toolchains\w64devkit"
if "%QT_SRC%"=="" set "QT_SRC=D:\dev\qt-src"
if "%QT_STATIC_PREFIX%"=="" set "QT_STATIC_PREFIX=D:\dev\qt-static-%QT_VERSION%"

set "TOOLCHAIN_BIN=%W64DEVKIT%\bin"
set "APP_DIR=%~dp0"

echo.
echo ==== SJJ-COM-Tool static build ====
echo.

REM ---------- [1/6] toolchain check ----------
echo [1/6] Checking MinGW toolchain...
if not exist "%TOOLCHAIN_BIN%\gcc.exe" (
    echo   [ERROR] Toolchain not found: %TOOLCHAIN_BIN%
    echo   Please download w64devkit v1.23.0 and extract to %W64DEVKIT%:
    echo   https://github.com/skeeto/w64devkit/releases/download/v1.23.0/w64devkit-1.23.0.zip
    exit /b 1
)
set "PATH=%TOOLCHAIN_BIN%;%PATH%"
set "CC=%TOOLCHAIN_BIN%\gcc.exe"
set "CXX=%TOOLCHAIN_BIN%\g++.exe"
for /f "delims=" %%v in ('"%TOOLCHAIN_BIN%\gcc.exe" --version ^| findstr /b "gcc"') do echo   [OK] %%v

REM ---------- static Qt ready? ----------
set "QT_CORE=%QT_STATIC_PREFIX%\lib\libQt6Core.a"
set "QT_CORE5=%QT_STATIC_PREFIX%\lib\libQt6Core5Compat.a"
set "QT_SERIAL=%QT_STATIC_PREFIX%\lib\libQt6SerialPort.a"

if exist "%QT_CORE%" if exist "%QT_CORE5%" if exist "%QT_SERIAL%" (
    echo   [SKIP] Static Qt already installed: %QT_STATIC_PREFIX%
    goto build_app
)

REM ---------- [2/6] sources ----------
echo.
echo [2/6] Preparing Qt %QT_VERSION% sources (first run, needs network ~2 min)...
if not exist "%QT_SRC%\qtbase" (
    git clone --depth 1 --branch v%QT_VERSION% https://github.com/qt/qtbase.git "%QT_SRC%\qtbase"
    if errorlevel 1 echo   [ERROR] failed to download qtbase & exit /b 1
)
if not exist "%QT_SRC%\qt5compat" (
    git clone --depth 1 --branch v%QT_VERSION% https://github.com/qt/qt5compat.git "%QT_SRC%\qt5compat"
    if errorlevel 1 echo   [ERROR] failed to download qt5compat & exit /b 1
)
if not exist "%QT_SRC%\qtserialport" (
    git clone --depth 1 --branch v%QT_VERSION% https://github.com/qt/qtserialport.git "%QT_SRC%\qtserialport"
    if errorlevel 1 echo   [ERROR] failed to download qtserialport & exit /b 1
)
echo   [OK] sources ready

set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"

REM ---------- [3/6] qtbase ----------
if not exist "%QT_CORE%" (
    echo.
    echo [3/6] Building static qtbase (first run ~12 min, please wait)...
    if not exist "%QT_SRC%\qtbase-static-build" mkdir "%QT_SRC%\qtbase-static-build"
    pushd "%QT_SRC%\qtbase-static-build"
    call "%QT_SRC%\qtbase\configure.bat" -static -release -opensource -confirm-license -prefix "%QT_STATIC_PREFIX%" -nomake examples -nomake tests -no-opengl -no-dbus -no-feature-vulkan
    if errorlevel 1 (
        popd
        echo   [ERROR] qtbase configure failed, see log above
        exit /b 1
    )
    ninja -j%JOBS%
    if errorlevel 1 (
        popd
        echo   [ERROR] qtbase build failed
        exit /b 1
    )
    cmake --install .
    if errorlevel 1 (
        popd
        echo   [ERROR] qtbase install failed
        exit /b 1
    )
    popd
    echo   [OK] qtbase installed
)

REM ---------- [4/6] qt5compat ----------
if not exist "%QT_CORE5%" (
    echo.
    echo [4/6] Building static qt5compat...
    if not exist "%QT_SRC%\qt5compat-static-build" mkdir "%QT_SRC%\qt5compat-static-build"
    pushd "%QT_SRC%\qt5compat-static-build"
    cmake -S "%QT_SRC%\qt5compat" -B . -G Ninja -DCMAKE_PREFIX_PATH="%QT_STATIC_PREFIX%" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    if errorlevel 1 (
        popd
        echo   [ERROR] qt5compat configure failed
        exit /b 1
    )
    ninja -j%JOBS%
    if errorlevel 1 (
        popd
        echo   [ERROR] qt5compat build failed
        exit /b 1
    )
    cmake --install .
    if errorlevel 1 (
        popd
        echo   [ERROR] qt5compat install failed
        exit /b 1
    )
    popd
    echo   [OK] qt5compat installed
)

REM ---------- [5/6] qtserialport ----------
if not exist "%QT_SERIAL%" (
    echo.
    echo [5/6] Building static qtserialport...
    if not exist "%QT_SRC%\qtserialport-static-build" mkdir "%QT_SRC%\qtserialport-static-build"
    pushd "%QT_SRC%\qtserialport-static-build"
    cmake -S "%QT_SRC%\qtserialport" -B . -G Ninja -DCMAKE_PREFIX_PATH="%QT_STATIC_PREFIX%" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
    if errorlevel 1 (
        popd
        echo   [ERROR] qtserialport configure failed
        exit /b 1
    )
    ninja -j%JOBS%
    if errorlevel 1 (
        popd
        echo   [ERROR] qtserialport build failed
        exit /b 1
    )
    cmake --install .
    if errorlevel 1 (
        popd
        echo   [ERROR] qtserialport install failed
        exit /b 1
    )
    popd
    echo   [OK] qtserialport installed
)

REM ---------- [6/6] build app ----------
:build_app
echo.
echo [6/6] Building app (static)...
pushd "%APP_DIR%"
REM rebuild cache if Qt prefix changed
if exist build_static\CMakeCache.txt (
    set "QT_STATIC_FWD=%QT_STATIC_PREFIX:\=/%"
    findstr /C:"%QT_STATIC_FWD%" build_static\CMakeCache.txt >nul
    if errorlevel 1 (
        echo   [INFO] Qt prefix changed, recreating build_static cache...
        rmdir /s /q build_static
    )
)
cmake -S . -B build_static -G Ninja -DCMAKE_PREFIX_PATH="%QT_STATIC_PREFIX%" -DCMAKE_C_COMPILER="%TOOLCHAIN_BIN%\gcc.exe" -DCMAKE_CXX_COMPILER="%TOOLCHAIN_BIN%\g++.exe" -DCMAKE_MAKE_PROGRAM="%TOOLCHAIN_BIN%\ninja.exe"
if errorlevel 1 (
    popd
    echo   [ERROR] app configure failed; try deleting cpp\build_static and rerun
    exit /b 1
)
cmake --build build_static
if errorlevel 1 (
    popd
    echo   [ERROR] app build failed
    exit /b 1
)
popd

REM ---------- output ----------
if not exist "%APP_DIR%dist_static" mkdir "%APP_DIR%dist_static"
copy /y "%APP_DIR%build_static\SJJ-COM-Tool.exe" "%APP_DIR%dist_static\SJJ-COM-Tool.exe" >nul

echo.
echo ============================================================
echo  BUILD OK!
echo  Single-file exe: %APP_DIR%dist_static\SJJ-COM-Tool.exe
echo  (~48MB, zero-dependency, runs on any 64-bit Win10/11)
echo ============================================================
endlocal
exit /b 0
