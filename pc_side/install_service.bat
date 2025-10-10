@echo off
setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set TARGET_DIR=%ProgramData%\ExodusAI
set VENV_DIR=%TARGET_DIR%\venv

set PYTHON=py -3
%PYTHON% -V >nul 2>&1
if errorlevel 1 set PYTHON=python

if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
if not exist "%TARGET_DIR%\logs" mkdir "%TARGET_DIR%\logs"
if not exist "%TARGET_DIR%\bin" mkdir "%TARGET_DIR%\bin"
if not exist "%TARGET_DIR%\presets" mkdir "%TARGET_DIR%\presets"
if not exist "%TARGET_DIR%\models" mkdir "%TARGET_DIR%\models"

copy /Y "%SCRIPT_DIR%\..\docs\*" "%TARGET_DIR%\" >nul 2>&1
xcopy /Y /Q "%SCRIPT_DIR%\..\windows_binaries\*" "%TARGET_DIR%\bin\" >nul 2>&1
xcopy /Y /Q "%SCRIPT_DIR%\..\presets\*" "%TARGET_DIR%\presets\" >nul 2>&1
xcopy /Y /Q "%SCRIPT_DIR%\..\models\*" "%TARGET_DIR%\models\" >nul 2>&1

if not exist "%VENV_DIR%" (
    %PYTHON% -m venv "%VENV_DIR%"
)
"%VENV_DIR%\Scripts\python.exe" -m pip install --upgrade pip
"%VENV_DIR%\Scripts\pip.exe" install -r "%SCRIPT_DIR%\requirements.txt"

copy /Y "%SCRIPT_DIR%\config.yaml" "%TARGET_DIR%\config.yaml"
copy /Y "%SCRIPT_DIR%\exodus_service.py" "%TARGET_DIR%\exodus_service.py"

"%VENV_DIR%\Scripts\python.exe" "%TARGET_DIR%\exodus_service.py" install
net start ExodusAIService

echo Installation complete.
