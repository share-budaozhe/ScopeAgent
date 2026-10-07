@echo off
rem ===== ScopeAgent 一键编译脚本 =====
rem 依赖：Qt 5.12.2 (mingw73_64 套件)
setlocal

set QT_BIN=C:\Qt\Qt5.12.2\5.12.2\mingw73_64\bin
set MINGW_BIN=C:\Qt\Qt5.12.2\Tools\mingw730_64\bin

if not exist "%QT_BIN%\qmake.exe" (
    echo [错误] 未找到 Qt：%QT_BIN%\qmake.exe
    echo 请安装 Qt 5.12.2 并选择 mingw73_64 套件，或修改本脚本中的 QT_BIN。
    exit /b 1
)

set PATH=%QT_BIN%;%MINGW_BIN%;%PATH%
cd /d "%~dp0.."
if not exist build mkdir build
cd build

echo [1/2] 运行 qmake ...
qmake ..\app\ScopeAgent.pro || exit /b 1
echo [2/2] 编译 ...
mingw32-make -j4 || exit /b 1

echo.
echo 编译完成：build\release\ScopeAgent.exe （或 build\debug\ScopeAgent.exe）
echo 运行 scripts\run.bat 启动（会一并启动 Agent sidecar 与打包）。
endlocal
