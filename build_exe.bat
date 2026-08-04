@echo off
setlocal
cd /d "%~dp0"

echo ==========================================
echo   SJJ-COM-Tool Build Script (PyInstaller)
echo ==========================================
echo.

set "PY=venv\Scripts\python.exe"
if not exist "%PY%" (
    echo [ERROR] venv not found: %PY%
    echo         Run: python -m venv venv
    pause
    exit /b 1
)

echo [1/4] Checking dependencies...
"%PY%" -c "import serial" >nul 2>&1 || ( echo        Installing pyserial... & "%PY%" -m pip install pyserial )
"%PY%" -c "import PySide6" >nul 2>&1 || ( echo        Installing PySide6... & "%PY%" -m pip install PySide6 )
"%PY%" -c "import PyInstaller" >nul 2>&1 || ( echo        Installing PyInstaller... & "%PY%" -m pip install pyinstaller )
"%PY%" -c "import PIL" >nul 2>&1 || ( echo        Installing Pillow... & "%PY%" -m pip install pillow )

echo [2/4] Preparing icon (ic_xue_xi.ico)...
if not exist ic_xue_xi.png (
    echo [ERROR] ic_xue_xi.png not found
    pause
    exit /b 1
)
if not exist ic_xue_xi.ico (
    "%PY%" -c "from PIL import Image; Image.open('ic_xue_xi.png').convert('RGBA').save('ic_xue_xi.ico', sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])"
    echo        ICO generated from PNG
)

echo [3/4] Cleaning old build...
if exist "dist\SJJ-COM-Tool.exe" del /q "dist\SJJ-COM-Tool.exe"
if exist "dist\_obsolete" rmdir /s /q "dist\_obsolete"
if exist "build" rmdir /s /q "build"

echo [4/4] Building (about 1 min)...
"%PY%" -m PyInstaller --onefile --windowed --icon=ic_xue_xi.ico --name "SJJ-COM-Tool" --add-data "ic_xue_xi.png;." sjj_com_tool.py
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed, check output above
    pause
    exit /b 1
)

echo.
echo ==========================================
echo   Build OK!
echo   Output: %cd%\dist\SJJ-COM-Tool.exe
echo ==========================================
pause
