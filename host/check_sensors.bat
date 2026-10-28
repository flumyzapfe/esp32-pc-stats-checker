@echo off
rem Shows every sensor this PC exposes (CPU temps, fans, GPU, drives) and the health checks.
call "%~dp0run_dashboard.bat" --sensors
