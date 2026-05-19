@echo off
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
cmake -DPROJECT_SOURCE_DIR="%SCRIPT_DIR%" -P "%SCRIPT_DIR%\cmake\UpdateAllVersions.cmake"
pause