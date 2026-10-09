@echo off
title Binary Translator
cd /d "%~dp0"

if not exist "..\BinaryTranslator.exe" (
    echo BinaryTranslator.exe not found. Building first...
    call build.bat
)

start "" "..\BinaryTranslator.exe"
