@echo off
rem Shadow of the Colossus PC: settings in sotc.ini (see sotc.py)
cd /d "%~dp0"
python sotc.py %*
rem Any non-zero code, also the negative ones of a crash (0xC0000409 reads as -1073740791).
if %errorlevel% neq 0 pause
