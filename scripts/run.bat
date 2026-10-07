@echo off
rem ===== ScopeAgent dev run (uses Qt bin from build) =====
setlocal
set QT_BIN=C:\Qt\Qt5.12.2\5.12.2\mingw73_64\bin
set PATH=%QT_BIN%;%PATH%
cd /d "%~dp0.."
if not exist build\release\ScopeAgent.exe (
    echo [Error] build\release\ScopeAgent.exe not found. Run scripts\build.bat first.
    pause
    exit /b 1
)
start "" build\release\ScopeAgent.exe
endlocal
