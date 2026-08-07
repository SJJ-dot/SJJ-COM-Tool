@echo off
set "QT_VERSION=6.8.2"
set "QT_SRC=D:/nonexist/qt-src"
set "QT_CORE=D:/nonexist/qt-static/lib/libQt6Core.a"
if not exist "%QT_SRC%\qtbase" (
    git clone --depth 1 --branch v%QT_VERSION% https://github.com/qt/qtbase.git "%QT_SRC%\qtbase"
    if errorlevel 1 echo   [ERROR] failed to download qtbase & exit /b 1
)
echo   [OK] sources ready
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"
if not exist "%QT_CORE%" (
    echo.
    echo [3/6] Building static qtbase (first run ~12 min, please wait)...
    if not exist "%QT_SRC%\qtbase-static-build" mkdir "%QT_SRC%\qtbase-static-build"
    pushd "%QT_SRC%\qtbase-static-build"
    call "%QT_SRC%\qtbase\configure.bat" -static -release -opensource -confirm-license -prefix "D:/x" -nomake examples -nomake tests -no-opengl -no-dbus -no-feature-vulkan
    if errorlevel 1 (
        popd
        echo   [ERROR] qtbase configure failed, see log above
        exit /b 1
    )
    popd
    echo   [OK] qtbase installed
)
echo DONE
