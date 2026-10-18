@echo off
rem One command: toolchain, build and launcher (see packaging\windows\dev_setup.ps1).
rem   build.cmd             build, then start the launcher
rem   build.cmd -NoLaunch   build only
rem   build.cmd -Package    also the player zip and installer in dist\
setlocal
rem Started from PowerShell 7, Windows PowerShell would load 7's modules (no Get-FileHash).
set "PSModulePath="
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\windows\dev_setup.ps1" %*
if errorlevel 1 (
    echo.
    echo Build failed, see the messages above.
    pause
    exit /b 1
)
