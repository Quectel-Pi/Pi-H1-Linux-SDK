@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem ============================================================
rem  Quectel PI H1 (QCS6490) Yocto SDK — Windows QFIL 后端烧录
rem  由 scripts/flash.sh 在 WSL 下自动调用 (也可在 Windows 直接运行)
rem  用法: scripts\flash.bat [ufs|emmc]      默认 ufs
rem
rem  固件目录: <SDK>\quectel_build\<QUECTEL_PROJECT_REV>
rem    rev 从 quectel-buildconfig-gen.h 读取; 可用 QPI_FW_DIR 覆盖
rem  环境变量: QPI_NO_RESET=1 跳过烧后复位
rem ============================================================

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "SDK_ROOT=%%~fI"

rem ---- 固件目录: 从 buildconfig 配置头解析 QUECTEL_PROJECT_REV ----
set "GEN_H=%SDK_ROOT%\quectel_build\compile\quectel-features-config\quectel-buildconfig-gen.h"
set "FW_REV="
if exist "%GEN_H%" (
    for /f "tokens=3" %%A in ('findstr /r /c:"QUECTEL_PROJECT_REV" "%GEN_H%"') do set "FW_REV=%%~A"
)
set "FW_DIR=%SDK_ROOT%\quectel_build\%FW_REV%"
if defined QPI_FW_DIR set "FW_DIR=%QPI_FW_DIR%"

rem ---- QFIL 后端 (本 SDK 自带, 无需安装 QPST) ----
set "QFIL_DIR=%SDK_ROOT%\tools\qfil"
if defined QPI_QFIL_DIR set "QFIL_DIR=%QPI_QFIL_DIR%"

set "FS_TYPE=%~1"
if "%FS_TYPE%"=="" set "FS_TYPE=ufs"

rem Reboot the device after flashing. The vendor XMLs carry no <power> tag,
rem and fh_loader does not reset on its own under --noprompt, so without this
rem the target stays in Firehose and needs a manual power cycle.
rem QPI_NO_RESET=1 skips it (e.g. to chain further operations).
set "DO_RESET=1"
if defined QPI_NO_RESET set "DO_RESET=0"
set "RESET_XML=%QFIL_DIR%\reset.xml"

set "FIREHOSE=prog_firehose_Qcm6490_ddr.elf"
set "SAHARA=%QFIL_DIR%\QSaharaServer.exe"
set "FHLOADER=%QFIL_DIR%\fh_loader.exe"

echo ==========================================
echo [h1-yocto] Windows QFIL Backend Flash
echo   FW_DIR : %FW_DIR%
echo   QFIL   : %QFIL_DIR%
echo   STORAGE: %FS_TYPE%
echo   RESET  : %DO_RESET%  (QPI_NO_RESET=1 to skip)
echo ==========================================
echo.

if not exist "%SAHARA%"   ( echo [ERROR] missing backend: %SAHARA%   & exit /b 1 )
if not exist "%FHLOADER%" ( echo [ERROR] missing backend: %FHLOADER% & exit /b 1 )

if not exist "%GEN_H%" (
    echo [WARN] buildconfig header not found: %GEN_H%
    echo        ^(fine if you set QPI_FW_DIR explicitly^)
)

if "%FW_REV%"=="" (
    if not defined QPI_FW_DIR (
        echo [ERROR] cannot resolve QUECTEL_PROJECT_REV from %GEN_H%
        echo         run buildconfig first, or set QPI_FW_DIR to a firmware package
        exit /b 1
    )
)

if not exist "%FW_DIR%" (
    echo [ERROR] firmware dir not found: %FW_DIR%
    echo         build first: full build ^+ buildpackage
    echo         or set QPI_FW_DIR to an external firmware package
    exit /b 1
)

for %%F in ("%FIREHOSE%" "efi.bin" "system.img" "dtb.bin") do (
    if not exist "%FW_DIR%\%%~F" (
        echo [ERROR] firmware missing %%~F
        echo         dir: %FW_DIR%
        exit /b 1
    )
)

set "PART_DIR=%FW_DIR%\partition_%FS_TYPE%"
if not exist "%PART_DIR%" (
    echo [ERROR] partition dir missing: %PART_DIR%
    exit /b 1
)

echo [1/3] Looking for Qualcomm 9008 EDL device ...
set "PORT="
set "PS_CMD=$d=Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -like '*VID_05C6&PID_9008*' } | Select-Object -First 1; if($d -and $d.Name -match '(COM\d+)'){ $matches[1] }"
for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "%PS_CMD%"`) do set "PORT=%%P"

if "%PORT%"=="" (
    echo [ERROR] no 9008 EDL device found
    echo         check:
    echo          - device in EDL: on a running system run  adb shell reboot edl
    echo          - panic state: power cycle while holding the EDL keys
    echo          - Qualcomm USB driver installed ^(QPST / LIBUSB driver package^)
    exit /b 1
)

rem Windows needs the \\.\COMn form at COM10 and above; a bare "COM10" fails
rem to open ^(QSaharaServer: port_connect Failed to open com port handle^).
set "PORTARG=\\.\%PORT%"
echo       detected: %PORT%  ^(using %PORTARG%^)
echo.

pushd "%FW_DIR%" || exit /b 1

echo [2/3] Sahara: push firehose programmer ^(%FIREHOSE%^) ...
"%SAHARA%" -p "%PORTARG%" -s 13:%FIREHOSE% -v 1
if errorlevel 1 (
    echo [ERROR] Sahara failed
    popd
    exit /b 1
)
echo.

echo [3/3] Firehose: full flash ^(LUN 0-5 rawprogram + patch^) ...
set "XMLLIST="
for %%L in (0 1 2 3 4 5) do (
    if exist "partition_%FS_TYPE%\rawprogram%%L.xml" (
        if defined XMLLIST (
            set "XMLLIST=!XMLLIST!,partition_%FS_TYPE%\rawprogram%%L.xml"
        ) else (
            set "XMLLIST=partition_%FS_TYPE%\rawprogram%%L.xml"
        )
    )
    if exist "partition_%FS_TYPE%\patch%%L.xml" (
        set "XMLLIST=!XMLLIST!,partition_%FS_TYPE%\patch%%L.xml"
    )
)

if "%XMLLIST%"=="" (
    echo [ERROR] no rawprogram/patch xml found
    popd
    exit /b 1
)

rem Append the reset directive last. fh_loader sorts <power> after <patch>,
rem so it runs once every write has landed.
if "%DO_RESET%"=="1" (
    if exist "%RESET_XML%" (
        set "XMLLIST=!XMLLIST!,%RESET_XML%"
        echo       reset directive: %RESET_XML%
    ) else (
        echo       [WARN] %RESET_XML% not found, skipping reset
    )
)

"%FHLOADER%" --port="%PORTARG%" --sendxml="%XMLLIST%" --search_path="%FW_DIR%" --noprompt --memoryname=%FS_TYPE% --loglevel=1
set "RC=%ERRORLEVEL%"
popd

echo.
if not "%RC%"=="0" (
    echo [ERROR] fh_loader failed ^(rc=%RC%^)
    exit /b %RC%
)

echo ==========================================
echo [h1-yocto] flash done
if "%DO_RESET%"=="1" (
    echo   the device was reset and should boot the new firmware
) else (
    echo   reset skipped ^(QPI_NO_RESET=1^) -- power cycle to boot
)
echo ==========================================
endlocal
exit /b 0
