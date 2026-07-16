@echo off
setlocal
set "GMAKE=C:\ti\ccs2041\ccs\utils\bin\gmake.exe"
set "GMAKE_FWD=C:/ti/ccs2041/ccs/utils/bin/gmake.exe"

if not exist "%GMAKE%" (
    echo error: CCS gmake not found at "%GMAKE%"
    exit /b 1
)

"%GMAKE%" MAKE_COMMAND=%GMAKE_FWD% %*
exit /b %ERRORLEVEL%
