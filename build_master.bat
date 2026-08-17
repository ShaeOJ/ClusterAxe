@echo off
echo ============================================
echo  ClusterAxe Build Script - Gamma Master (601)
echo  Single BM1370, cluster master, ESP-NOW
echo  Build dir: build_master  Config: sdkconfig.master
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

:: Seed the config from the shared base + this variant's overrides via
:: SDKCONFIG_DEFAULTS (IDF reads these but never rewrites them), and let the
:: generated sdkconfig live inside the build dir so builds never clobber the
:: tracked defaults files. A per-variant build dir also keeps outputs separate.
%IDF_PYTHON% %IDF_PATH%\tools\idf.py -B build_master -D SDKCONFIG_DEFAULTS="%CD%\sdkconfig.defaults;%CD%\sdkconfig.defaults.master" -D SDKCONFIG=%CD%\build_master\sdkconfig -D NPM_EXECUTABLE=%NPM_EXE% build
set BUILD_RESULT=%ERRORLEVEL%

if %BUILD_RESULT% NEQ 0 (
    echo.
    echo ============================================
    echo  Build Failed! Check errors above.
    echo ============================================
    exit /b 1
)

:: Give the app binary a variant-specific name so master/slave never get mixed up.
:: (project() names it zombie-os-master via the cluster-mode check in CMakeLists.)
copy /Y build_master\zombie-os-master.bin build_master\clusteraxe-gamma601-master.bin >nul

echo.
echo ============================================
echo  Build Successful!
echo ============================================
echo.
echo Firmware files located in: build_master\
echo  - clusteraxe-gamma601-master.bin (copy of app, variant-named^)
echo  - zombie-os.bin (main application^)
echo  - bootloader\bootloader.bin
echo  - partition_table\partition-table.bin
echo  - www.bin
echo.
echo Flash from build_master\ (see the esptool line idf.py printed above^).
echo.
