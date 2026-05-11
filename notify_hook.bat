@echo off
REM Windows için e-posta bildirim hook'u
REM Argümanlar:
REM   %1 = LEVEL
REM   %2 = TITLE
REM   %3 = MESSAGE

set LEVEL=%~1
set TITLE=%~2
set MESSAGE=%~3

echo [%date% %time%] %LEVEL% - %TITLE% - %MESSAGE% >> notification_log.txt

REM PowerShell betiğini çağırarak e-postayı gönderiyoruz.
powershell.exe -ExecutionPolicy Bypass -File "%~dp0send_email.ps1" -Level "%LEVEL%" -Title "%TITLE%" -Message "%MESSAGE%"

exit /b %ERRORLEVEL%
