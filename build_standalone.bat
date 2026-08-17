@echo off
echo ============================================
echo  ClusterAxe Build Script - Gamma 601 Standalone
echo  Single BM1370, no cluster (solo mining)
echo  Build dir: build_standalone  Config: sdkconfig.standalone
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

:: Build into a dedicated dir with the standalone config as SDKCONFIG. A
:: per-variant build dir keeps variants separate AND sidesteps the Windows
:: "copy preserves mtime" trap that stopped ninja from reconfiguring.
%IDF_PYTHON% %IDF_PATH%\tools\idf.py -B build_standalone -D SDKCONFIG_DEFAULTS="%CD%\sdkconfig.defaults;%CD%\sdkconfig.defaults.standalone" -D SDKCONFIG=%CD%\build_standalone\sdkconfig -D NPM_EXECUTABLE=%NPM_EXE% build
set BUILD_RESULT=%ERRORLEVEL%

if %BUILD_RESULT% NEQ 0 (
    echo.
    echo ============================================
    echo  Build Failed! Check errors above.
    echo ============================================
    exit /b 1
)

:: Variant-specific name. Standalone has no cluster mode, so project() names the
:: app just "zombie-os" (see the cluster-mode check in CMakeLists).
copy /Y build_standalone\zombie-os.bin build_standalone\clusteraxe-gamma601-standalone.bin >nul

echo.
echo ============================================
echo  Build Successful!
echo ============================================
echo.
echo Firmware files located in: build_standalone\
echo  - clusteraxe-gamma601-standalone.bin (copy of app, variant-named^)
echo  - zombie-os.bin (main application^)
echo  - bootloader\bootloader.bin
echo  - partition_table\partition-table.bin
echo  - www.bin
echo.
echo Flash from build_standalone\ (see the esptool line idf.py printed above^).
echo.
