@echo off
rem Run once. Installs Python (if missing), yt-dlp and ffmpeg.
cd /d "%~dp0"
where py >nul 2>nul && goto havepy
where python >nul 2>nul && goto havepy
echo Installing Python...
winget install -e --id Python.Python.3.12 --accept-package-agreements --accept-source-agreements
echo.
echo Python installed. Close this window and run Setup.bat again.
pause
exit /b

:havepy
where py >nul 2>nul && (set PY=py) || (set PY=python)
%PY% -m pip install -U yt-dlp imageio-ffmpeg
echo.
echo Done. Now double-click "CYD Music.bat".
pause
