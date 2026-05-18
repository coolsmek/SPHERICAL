@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Preset-based build commands:
rem .\build_all.bat Debug --run
rem .\build_all.bat Release --no-run
rem .\build_all.bat Debug cmake-build-spherical-debug run  (legacy BuildDir arg is ignored)

rem Usage:
rem   build_all.bat [Config] [RunMode]
rem   build_all.bat [Config] [LegacyBuildDir] [RunMode]
rem Example:
rem   build_all.bat Debug
rem   build_all.bat Release --run

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "ARG2=%~2"
set "ARG3=%~3"
set "LEGACY_BUILD_DIR="
set "RUN_MODE=--no-run"

if not "%ARG2%"=="" (
    call :is_run_mode "%ARG2%"
    if "!IS_RUN_MODE!"=="1" (
        set "RUN_MODE=%ARG2%"
    ) else (
        set "LEGACY_BUILD_DIR=%ARG2%"
        if not "%ARG3%"=="" set "RUN_MODE=%ARG3%"
    )
)

if /I "%CONFIG%"=="Debug" (
    set "CONFIG=Debug"
    set "CONFIGURE_PRESET=vs2022-debug"
    set "BUILD_PRESET_SDK=build-debug-sdk"
    set "BUILD_PRESET_TEST=build-debug-test"
) else if /I "%CONFIG%"=="Release" (
    set "CONFIG=Release"
    set "CONFIGURE_PRESET=vs2022-release"
    set "BUILD_PRESET_SDK=build-release-sdk"
    set "BUILD_PRESET_TEST=build-release-test"
) else (
    echo [ERROR] Invalid Config: %CONFIG%
    echo [INFO] Valid Config values: Debug, Release
    exit /b 1
)

set "SKIP_RUN=0"
if /I "%RUN_MODE%"=="run" set "SKIP_RUN=0"
if /I "%RUN_MODE%"=="--run" set "SKIP_RUN=0"
if /I "%RUN_MODE%"=="--no-run" set "SKIP_RUN=1"
if /I "%RUN_MODE%"=="norun" set "SKIP_RUN=1"
if /I "%RUN_MODE%"=="build" set "SKIP_RUN=1"

for %%I in ("%~dp0..") do set "ROOT_DIR=%%~fI"
set "BUILD_DIR=%ROOT_DIR%\out\build\%CONFIGURE_PRESET%"

echo [INFO] Root: %ROOT_DIR%
echo [INFO] Config: %CONFIG%
echo [INFO] Configure preset: %CONFIGURE_PRESET%
echo [INFO] SDK build preset: %BUILD_PRESET_SDK%
echo [INFO] Test build preset: %BUILD_PRESET_TEST%
echo [INFO] Build dir: %BUILD_DIR%
echo [INFO] Run mode: %RUN_MODE%

if defined LEGACY_BUILD_DIR (
    echo [WARN] Ignoring legacy BuildDir argument: %LEGACY_BUILD_DIR%
    echo [WARN] The script now uses CMakePresets.json to choose the build directory.
)

if /I not "%RUN_MODE%"=="--no-run" if /I not "%RUN_MODE%"=="norun" if /I not "%RUN_MODE%"=="build" if /I not "%RUN_MODE%"=="run" if /I not "%RUN_MODE%"=="--run" (
    echo [ERROR] Invalid RunMode: %RUN_MODE%
    echo [INFO] Valid RunMode values: --no-run, build, norun, --run, run
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake not found on PATH.
    exit /b 1
)

echo [STEP] Configure CMake preset (%CONFIGURE_PRESET%)...
cmake --preset %CONFIGURE_PRESET%
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [INFO] Shaders are built automatically as part of the Spherical target.

echo [STEP] Build SDK target (Spherical) via preset %BUILD_PRESET_SDK%...
cmake --build --preset %BUILD_PRESET_SDK%
if errorlevel 1 (
    echo [ERROR] SDK build failed.
    exit /b 1
)

echo [STEP] Build test target (SPHERICAL_Test) via preset %BUILD_PRESET_TEST%...
cmake --build --preset %BUILD_PRESET_TEST%
if errorlevel 1 (
    echo [ERROR] Test target build failed.
    exit /b 1
)

if "%SKIP_RUN%"=="1" (
    echo [INFO] Build-only mode: skipping test execution.
    echo [OK] Build completed successfully.
    exit /b 0
)

set "TEST_EXE=%BUILD_DIR%\SPHERICAL-TEST\%CONFIG%\SPHERICAL_Test.exe"
if not exist "%TEST_EXE%" (
    echo [ERROR] Test executable not found: %TEST_EXE%
    exit /b 1
)

echo [STEP] Launching SPHERICAL_Test (interactive GUI - close window to continue)...
echo [INFO] App: %TEST_EXE%
echo [INFO] This is an interactive demo. Close the window when done.
"%TEST_EXE%"
set "APP_EXIT=%ERRORLEVEL%"

rem Exit code 0 = closed cleanly. -1073741510 = closed by Ctrl+C. Any other code = warning.
if "%APP_EXIT%"=="0" goto app_exit_clean
if "%APP_EXIT%"=="-1073741510" goto app_exit_ctrlc
echo [WARN] Application exited with code: %APP_EXIT%
goto app_exit_done

:app_exit_clean
echo [OK] Application exited cleanly.
goto app_exit_done

:app_exit_ctrlc
echo [OK] Application closed by user (Ctrl+C).

:app_exit_done

echo [OK] Build and run completed successfully.
exit /b 0

:is_run_mode
set "IS_RUN_MODE=0"
if /I "%~1"=="run" set "IS_RUN_MODE=1"
if /I "%~1"=="--run" set "IS_RUN_MODE=1"
if /I "%~1"=="--no-run" set "IS_RUN_MODE=1"
if /I "%~1"=="norun" set "IS_RUN_MODE=1"
if /I "%~1"=="build" set "IS_RUN_MODE=1"
exit /b 0


