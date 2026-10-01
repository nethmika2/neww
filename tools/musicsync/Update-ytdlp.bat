@echo off
rem YouTube changes often; if downloads start failing, run this.
cd /d "%~dp0"
where py >nul 2>nul && (py cydmusic.py --update) || (python cydmusic.py --update)
pause
