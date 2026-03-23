@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
set "WINDRES=%SCRIPT_DIR%rc.cmd"
set "AR=%SCRIPT_DIR%llvm-ar-no-thin.cmd"
meson %*
exit /b %ERRORLEVEL%
