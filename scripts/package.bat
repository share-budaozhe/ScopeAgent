@echo off
rem ===== ScopeAgent package script (windeployqt + sidecar exe -> dist/) =====
setlocal
set QT_BIN=C:\Qt\Qt5.12.2\5.12.2\mingw73_64\bin
set PATH=%QT_BIN%;%PATH%
cd /d "%~dp0.."

if not exist build\release\ScopeAgent.exe (
    echo [Error] build\release\ScopeAgent.exe not found. Run scripts\build.bat first.
    exit /b 1
)

echo [1/4] Clean dist
if exist dist rmdir /s /q dist
mkdir dist

echo [2/4] Copy Qt app + deploy DLLs
copy /y build\release\ScopeAgent.exe dist\ >nul
windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw dist\ScopeAgent.exe >nul
if errorlevel 1 ( echo [Error] windeployqt failed & exit /b 1 )

echo [3/4] Build sidecar exe (PyInstaller, optional)
where pyinstaller >nul 2>&1
if errorlevel 1 (
    echo [WARN] pyinstaller not found; runtime will fall back to Python.
) else (
    pushd agent
    if exist models (
        pyinstaller --onefile --name scopeagent-sidecar --clean --noconfirm ^
            --hidden-import pypdf --hidden-import onnxruntime --hidden-import numpy --hidden-import tokenizers ^
            --add-data "models;models" server.py >nul 2>&1
    ) else (
        pyinstaller --onefile --name scopeagent-sidecar --clean --noconfirm ^
            --hidden-import pypdf --hidden-import onnxruntime --hidden-import numpy --hidden-import tokenizers ^
            server.py >nul 2>&1
    )
    popd
)

echo [4/4] Copy sidecar (exe + python sources) and launcher
xcopy /e /i /y agent dist\agent >nul
if exist dist\agent\schema_test.py del dist\agent\schema_test.py
if exist dist\agent\mcp_test.py del dist\agent\mcp_test.py
if exist dist\agent\__pycache__ rmdir /s /q dist\agent\__pycache__
if exist dist\agent\build rmdir /s /q dist\agent\build
if exist dist\agent\scopeagent-sidecar.spec del dist\agent\scopeagent-sidecar.spec
if exist dist\agent\dist\scopeagent-sidecar.exe (
    move /y dist\agent\dist\scopeagent-sidecar.exe dist\agent\scopeagent-sidecar.exe >nul
    rmdir /s /q dist\agent\dist
)

copy /y scripts\run_dist.bat dist\run.bat >nul

echo.
echo ===== Package done =====
if exist dist\agent\scopeagent-sidecar.exe (
    echo sidecar: bundled exe - no Python needed at runtime
) else (
    echo sidecar: python sources - requires Python 3.8+ at runtime
)
echo dist\run.bat   (double-click to launch)
echo Requires DEEPSEEK_API_KEY (env var or agent\config.json).
endlocal
