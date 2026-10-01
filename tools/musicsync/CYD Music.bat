@echo off
rem Double-click, paste links. You can also drag audio files/folders onto this file.
cd /d "%~dp0"
where py >nul 2>nul && (py cydmusic.py %*) || (python cydmusic.py %*)
echo.
pause
