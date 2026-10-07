@echo off
rem ===== ScopeAgent automated test script =====
rem [1/6] Python schema unit test (no network)
rem [2/6] MCP protocol test (no network)
rem [3/6] C++ driver unit test (no network)
rem [4/6] C++ SCPI/VISA driver test (no network)
rem [5/6] Offscreen render test (no network)
rem [6/6] C++ Agent integration test (network + DEEPSEEK_API_KEY)
setlocal

set QT_BIN=C:\Qt\Qt5.12.2\5.12.2\mingw73_64\bin
set MINGW_BIN=C:\Qt\Qt5.12.2\Tools\mingw730_64\bin
set QT_INC=C:\Qt\Qt5.12.2\5.12.2\mingw73_64\include
set QT_LIB=C:\Qt\Qt5.12.2\5.12.2\mingw73_64\lib

set PATH=%QT_BIN%;%MINGW_BIN%;%PATH%
cd /d "%~dp0.."
if not exist build mkdir build
cd build

echo ============ [1/6] Python schema unit test ============
python ..\agent\schema_test.py
if errorlevel 1 ( echo [FAIL] schema test & exit /b 1 )

echo ============ [1b/6] Python knowledge base test (no network) ============
python ..\agent\knowledge_test.py
if errorlevel 1 ( echo [FAIL] knowledge test & exit /b 1 )

echo.
echo ============ [2/6] MCP protocol test ============
python ..\agent\mcp_test.py
if errorlevel 1 ( echo [FAIL] mcp test & exit /b 1 )

echo.
echo ============ [3/6] C++ driver unit test ============
g++ -std=c++14 -DUNICODE -D_UNICODE -I..\app\src -I..\app\drivers -I%QT_INC% -I%QT_INC%\QtCore ^
    ..\tests\test_driver.cpp ..\app\src\deviceconfig.cpp ..\app\drivers\virtualscopedriver.cpp ^
    -L%QT_LIB% -lQt5Core -o test_driver.exe
if errorlevel 1 ( echo [FAIL] driver test compile & exit /b 1 )
test_driver.exe
if errorlevel 1 ( echo [FAIL] driver test & exit /b 1 )

echo.
echo ============ [4/6] C++ SCPI/VISA driver test ============
g++ -std=c++14 -DUNICODE -D_UNICODE -I..\app\src -I..\app\drivers -I%QT_INC% -I%QT_INC%\QtCore ^
    ..\tests\test_scpi.cpp ..\app\src\deviceconfig.cpp ..\app\drivers\scpidriver.cpp ^
    -L%QT_LIB% -lQt5Core -o test_scpi.exe
if errorlevel 1 ( echo [FAIL] scpi test compile & exit /b 1 )
test_scpi.exe
if errorlevel 1 ( echo [FAIL] scpi test & exit /b 1 )

echo.
echo ============ [5/6] Offscreen render test ============
moc ..\app\src\waveformwidget.h -o moc_waveform_test.cpp
g++ -std=c++14 -DUNICODE -D_UNICODE -I..\app\src -I..\app\drivers -I%QT_INC% -I%QT_INC%\QtCore -I%QT_INC%\QtGui -I%QT_INC%\QtWidgets ^
    ..\tests\render_test.cpp ..\app\src\waveformwidget.cpp ..\app\src\deviceconfig.cpp ..\app\drivers\virtualscopedriver.cpp ^
    moc_waveform_test.cpp -L%QT_LIB% -lQt5Widgets -lQt5Gui -lQt5Core -o render_test.exe
if errorlevel 1 ( echo [FAIL] render test compile & exit /b 1 )
render_test.exe
if errorlevel 1 ( echo [FAIL] render test & exit /b 1 )

echo.
echo ============ [6/6] C++ Agent integration test (network) ============
if "%DEEPSEEK_API_KEY%"=="" (
    echo [SKIP] DEEPSEEK_API_KEY not set, skip agent integration test
) else (
    moc ..\app\src\agentclient.h -o moc_agentclient_test.cpp
    g++ -std=c++14 -DUNICODE -D_UNICODE -I..\app\src -I..\app\drivers -I%QT_INC% -I%QT_INC%\QtCore -I%QT_INC%\QtNetwork ^
        ..\tests\test_agent.cpp ..\app\src\agentclient.cpp ..\app\src\deviceconfig.cpp ..\app\drivers\virtualscopedriver.cpp ^
        moc_agentclient_test.cpp -L%QT_LIB% -lQt5Network -lQt5Core -o test_agent.exe
    if errorlevel 1 ( echo [FAIL] agent test compile & exit /b 1 )
    test_agent.exe
    if errorlevel 1 ( echo [FAIL] agent integration test & exit /b 1 )
)

echo.
echo ============ ALL TESTS PASSED ============
endlocal
