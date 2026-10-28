@echo off
rem Double-click this to start the PC Stats Dashboard. The window stays open so you can read any errors.
cd /d "%~dp0"
title PC Stats Dashboard

where py >nul 2>nul
if %errorlevel%==0 (set "PY=py -3") else (set "PY=python")

echo Using Python:
%PY% --version
if errorlevel 1 (
    echo.
    echo Python was not found. Install it from https://www.python.org/downloads/
    echo and tick "Add python.exe to PATH" during setup.
    echo.
    pause
    exit /b 1
)
echo.

%PY% cyd_stats.py %*

echo.
echo The script has stopped. Any error is shown above.
pause
