@echo off
rem PC Stats Display launcher for Windows.
rem Double-click to start. Installs the Python libraries on first run, and keeps
rem the window open so you can read any errors. Extra arguments are passed on,
rem e.g.  run_dashboard.bat --port COM5
cd /d "%~dp0"
title PC Stats Display

where py >nul 2>nul
if %errorlevel%==0 (set "PY=py -3") else (set "PY=python")

%PY% --version >nul 2>nul
if errorlevel 1 (
    echo Python was not found. Install it from https://www.python.org/downloads/
    echo and tick "Add python.exe to PATH" during setup, then run this again.
    echo.
    pause
    exit /b 1
)

%PY% -c "import psutil, serial, pynvml" >nul 2>nul
if errorlevel 1 (
    echo Installing required libraries...
    %PY% -m pip install --user -r requirements.txt
    if errorlevel 1 (
        echo.
        echo Could not install the libraries. See the error above.
        pause
        exit /b 1
    )
    echo.
)

%PY% pc_stats.py %*

echo.
echo The script has stopped. Any error is shown above.
pause
