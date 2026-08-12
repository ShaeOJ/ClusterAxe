@echo off
echo ============================================
echo  ClusterAxe Build Script - GT Standalone (801)
echo  Gamma Turbo, 2x BM1370, no cluster mode
echo ============================================
echo.

:: Set ESP-IDF paths
set IDF_PATH=C:\Users\onawa\esp\esp-idf
set IDF_PYTHON=C:\Users\onawa\.espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe

:: Initialize ESP-IDF environment
echo Initializing ESP-IDF environment...
call "%IDF_PATH%\export.bat"

:: Resolve npm explicitly. CMake caches NPM_EXECUTABLE (find_program) and the
:: old "C:\Program Files\nodejs\npm" path goes stale when Node is moved/managed
:: by nvm4w, causing the Web UI build step to fail with
:: "The system cannot find the path specified." Pass the current npm to CMake.
set NPM_EXE=C:/nvm4w/nodejs/npm.cmd
if not exist "%NPM_EXE%" (
    for /f "delims=" %%i in ('where npm.cmd 2^>nul') do set NPM_EXE=%%i
)
echo Using npm: %NPM_EXE%

echo.
echo Building firmware...
echo.

:: Run the build
%IDF_PYTHON% %IDF_PATH%\tools\idf.py -D NPM_EXECUTABLE=%NPM_EXE% build
set BUILD_RESULT=%ERRORLEVEL%

if %BUILD_RESULT% EQU 0 (
    echo.
    echo ============================================
    echo  Build Successful!
    echo ============================================
    echo.
    echo Firmware files located in: build\
    echo  - esp-miner.bin (main application)
    echo  - bootloader\bootloader.bin
    echo  - partition_table\partition-table.bin
    echo.
    echo To create merged binary for web flashing:
    echo   Run: merge_bin.bat
    echo.
) else (
    echo.
    echo ============================================
    echo  Build Failed! Check errors above.
    echo ============================================
    exit /b 1
)

pause
