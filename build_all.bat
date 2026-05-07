@echo off
setlocal

rem build commands:
rem .\build_all.bat Debug cmake-build-spherical_debug run
rem .\build_all.bat Debug cmake-build-spherical_debug norun

rem Usage:
rem   build_all.bat [Config] [BuildDir] [RunMode]
rem Example:
rem   build_all.bat Debug cmake-build-spherical_debug
rem   build_all.bat Release cmake-build-spherical_debug --run

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "BUILD_DIR=%~2"
if "%BUILD_DIR%"=="" set "BUILD_DIR=cmake-build-spherical_debug"

set "RUN_MODE=%~3"
if "%RUN_MODE%"=="" set "RUN_MODE=--no-run"

set "SKIP_RUN=0"
if /I "%RUN_MODE%"=="run" set "SKIP_RUN=0"
if /I "%RUN_MODE%"=="--run" set "SKIP_RUN=0"
if /I "%RUN_MODE%"=="--no-run" set "SKIP_RUN=1"
if /I "%RUN_MODE%"=="norun" set "SKIP_RUN=1"
if /I "%RUN_MODE%"=="build" set "SKIP_RUN=1"

set "ROOT_DIR=%~dp0"
set "ROOT_DIR=%ROOT_DIR:~0,-1%"

echo [INFO] Root: %ROOT_DIR%
echo [INFO] Config: %CONFIG%
echo [INFO] Build dir: %BUILD_DIR%
echo [INFO] Run mode: %RUN_MODE%

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

set "TOOLCHAIN_ARG="
set "CACHE_FILE=%ROOT_DIR%\%BUILD_DIR%\CMakeCache.txt"

if exist "%CACHE_FILE%" (
    echo [INFO] Existing CMake cache detected. Reusing configured generator/toolchain.
) else (
    if defined VCPKG_ROOT (
        if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
            set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
            echo [INFO] Using VCPKG_ROOT toolchain: %VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
        ) else (
            if exist "%ROOT_DIR%\cmake\vcpkg-toolchain.cmake" (
                set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%ROOT_DIR%\cmake\vcpkg-toolchain.cmake"
                echo [INFO] Using toolchain: %ROOT_DIR%\cmake\vcpkg-toolchain.cmake
            ) else (
                echo [INFO] No explicit toolchain selected. Continuing with CMake defaults.
            )
        )
    ) else (
        if exist "%ROOT_DIR%\cmake\vcpkg-toolchain.cmake" (
            set "TOOLCHAIN_ARG=-DCMAKE_TOOLCHAIN_FILE=%ROOT_DIR%\cmake\vcpkg-toolchain.cmake"
            echo [INFO] Using toolchain: %ROOT_DIR%\cmake\vcpkg-toolchain.cmake
        ) else (
            echo [INFO] No explicit toolchain selected. Continuing with CMake defaults.
        )
    )
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
    echo [INFO] Build-only mode: skipping test execution.
    echo [OK] Build completed successfully.
    exit /b 0
)

set "TEST_EXE=%ROOT_DIR%\%BUILD_DIR%\SPHERICAL-TEST\%CONFIG%\SPHERICAL_Test.exe"
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


