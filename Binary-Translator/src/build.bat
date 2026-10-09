@echo off
title Build - Binary Translator
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ compiler not found in PATH.
    echo Install MinGW-w64 and try again.
    pause
    exit /b 1
)

set RES=
where windres >nul 2>nul
if errorlevel 1 (
    echo [WARN] windres not found - building without icon.
) else (
    echo Compiling resources ...
    windres app.rc -O coff -o app.res
    if errorlevel 1 (
        echo.
        echo [ERROR] Resource build failed.
        pause
        exit /b 1
    )
    set RES=app.res
)

echo Compiling main.cpp ...
g++ -std=c++14 -O2 -static -mwindows -o "..\BinaryTranslator.exe" main.cpp %RES%

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

if exist app.res del app.res

echo.
echo [OK] Build successful --^> ..\BinaryTranslator.exe
pause
