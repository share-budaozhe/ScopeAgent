@echo off
rem ===== ScopeAgent launcher (packaged version) =====
rem If sidecar exe is bundled, no Python is needed.
if exist "%~dp0agent\scopeagent-sidecar.exe" goto :run

where python >nul 2>&1
if errorlevel 1 (
    echo [Error] Neither bundled sidecar exe nor Python found.
    echo Expected: agent\scopeagent-sidecar.exe  OR  Python 3.8+ on PATH.
    pause
    exit /b 1
)

:run
start "" "%~dp0ScopeAgent.exe"
