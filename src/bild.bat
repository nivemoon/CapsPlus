@echo off
echo Building CapsPlus.exe...

gcc caps_lang.c -luser32 -mwindows -o CapsPlus.exe
if errorlevel 1 (
    echo Build failed.
) else (
    echo Build succeeded.
)

pause
