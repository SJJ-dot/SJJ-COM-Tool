@echo off
set "QT_VERSION=6.8.2"
set "QT_SRC=D:/nonexist/qt-src"
set "QT_CORE=D:/nonexist/qt-static/lib/libQt6Core.a"
if not exist "%QT_SRC%\qtbase" (
    echo   [SKIP] simulated
    if errorlevel 1 echo   [ERROR] failed & exit /b 1
)
echo   [OK] sources ready
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"
if not exist "%QT_CORE%" (
    echo.
    echo [3/6] Building static qtbase, first run ~12 min, please wait...
    if not exist "%QT_SRC%\qtbase-static-build" mkdir "%QT_SRC%\qtbase-static-build"
    pushd "%QT_SRC%\qtbase-static-build"
    if errorlevel 1 (
        popd
        echo   [ERROR] qtbase configure failed
        exit /b 1
    )
    popd
    echo   [OK] qtbase installed
)
echo DONE_OK
