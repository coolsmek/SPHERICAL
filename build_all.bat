@echo off
setlocal

rem Usage:
rem   build_all.bat [Config] [BuildDir] [RunMode]
rem Example:
rem   build_all.bat Debug cmake-build-spherical_debug
rem   build_all.bat Release cmake-build-spherical_debug --no-run

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "BUILD_DIR=%~2"
if "%BUILD_DIR%"=="" set "BUILD_DIR=cmake-build-spherical_debug"

set "RUN_MODE=%~3"
if "%RUN_MODE%"=="" set "RUN_MODE=run"

set "SKIP_RUN=0"
if /I "%RUN_MODE%"=="--no-run" set "SKIP_RUN=1"
if /I "%RUN_MODE%"=="norun" set "SKIP_RUN=1"

set "ROOT_DIR=%~dp0"
set "ROOT_DIR=%ROOT_DIR:~0,-1%"

echo [INFO] Root: %ROOT_DIR%
echo [INFO] Config: %CONFIG%
echo [INFO] Build dir: %BUILD_DIR%
echo [INFO] Run mode: %RUN_MODE%

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake not found on PATH.
    exit /b 1
)

set "TOOLCHAIN_ARG="
if exist "%ROOT_DIR%\cmake\vcpkg-toolchain.cmake" (
    set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%ROOT_DIR%\cmake\vcpkg-toolchain.cmake"
    echo [INFO] Using toolchain: %ROOT_DIR%\cmake\vcpkg-toolchain.cmake
) else (
    echo [INFO] Local vcpkg toolchain not found. Continuing without explicit toolchain arg.
)

echo [STEP] Configure CMake...
cmake -S "%ROOT_DIR%" -B "%ROOT_DIR%\%BUILD_DIR%" %TOOLCHAIN_ARG%
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [INFO] Shaders are built automatically as part of the Spherical target.

echo [STEP] Build SDK target (Spherical)...
cmake --build "%ROOT_DIR%\%BUILD_DIR%" --config %CONFIG% --target Spherical
if errorlevel 1 (
    echo [ERROR] SDK build failed.
    exit /b 1
)

echo [STEP] Build test target (SPHERICAL_Test)...
cmake --build "%ROOT_DIR%\%BUILD_DIR%" --config %CONFIG% --target SPHERICAL_Test
if errorlevel 1 (
    echo [ERROR] Test target build failed.
    exit /b 1
)

if "%SKIP_RUN%"=="1" (
    echo [INFO] Skipping test execution.
    echo [OK] Build completed successfully.
    exit /b 0
)

set "TEST_EXE=%ROOT_DIR%\%BUILD_DIR%\SPHERICAL-TEST\%CONFIG%\SPHERICAL_Test.exe"
if not exist "%TEST_EXE%" (
    echo [ERROR] Test executable not found: %TEST_EXE%
    exit /b 1
)

echo [STEP] Run SPHERICAL_Test...
"%TEST_EXE%"
if errorlevel 1 (
    echo [ERROR] SPHERICAL_Test failed.
    exit /b 1
)

echo [OK] Build and test run completed successfully.
exit /b 0


