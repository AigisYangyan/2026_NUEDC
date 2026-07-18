@echo off
rem ============================================================
rem  MSPM0G3519 一键烧录 —— 双击本文件即可（先编译，通过后烧录）
rem  串口号等在同目录 flash_config.ini 里改
rem ============================================================
chcp 65001 >nul
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
    py bsl_flash.py %*
) else (
    python bsl_flash.py %*
)

echo.
echo 按任意键关闭窗口 . . .
pause >nul
