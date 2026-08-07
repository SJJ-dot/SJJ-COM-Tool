@echo off
set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=8"
echo JOBS=[%JOBS%]
for /f "delims=" %%v in ('"D:/dev/toolchains/w64devkit/bin/gcc.exe" --version ^| findstr /b "gcc"') do echo   [OK] %%v
echo END_TEST
