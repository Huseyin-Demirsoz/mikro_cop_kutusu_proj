@echo off
REM Smart Trash Bin notification hook example for Windows.
REM Rename this file to notify_hook.bat to activate it.
REM Arguments:
REM   %1 = LEVEL
REM   %2 = TITLE
REM   %3 = MESSAGE

set LEVEL=%~1
set TITLE=%~2
set MESSAGE=%~3

echo [%date% %time%] %LEVEL% - %TITLE% - %MESSAGE% >> notification_log.txt

REM ------------------------------------------------------------
REM Example 1: Telegram mobile notification
REM Create a Telegram bot with BotFather, then set:
REM   set TELEGRAM_BOT_TOKEN=123456:ABC...
REM   set TELEGRAM_CHAT_ID=123456789
REM ------------------------------------------------------------
if not "%TELEGRAM_BOT_TOKEN%"=="" if not "%TELEGRAM_CHAT_ID%"=="" (
  curl -s -X POST "https://api.telegram.org/bot%TELEGRAM_BOT_TOKEN%/sendMessage" ^
    -d "chat_id=%TELEGRAM_CHAT_ID%" ^
    -d "text=%TITLE% - %MESSAGE%" > nul
)

REM ------------------------------------------------------------
REM Example 2: SMTP e-mail using curl
REM Required environment variables:
REM   SMTP_URL=smtps://smtp.gmail.com:465
REM   SMTP_USER=your_email@gmail.com
REM   SMTP_PASS=your_app_password
REM   EMAIL_FROM=your_email@gmail.com
REM   EMAIL_TO=target_email@example.com
REM ------------------------------------------------------------
if not "%SMTP_URL%"=="" if not "%EMAIL_TO%"=="" (
  > email_payload.txt echo Subject: %TITLE%
  >> email_payload.txt echo.
  >> email_payload.txt echo %MESSAGE%
  curl --url "%SMTP_URL%" --ssl-reqd ^
    --mail-from "%EMAIL_FROM%" ^
    --mail-rcpt "%EMAIL_TO%" ^
    --user "%SMTP_USER%:%SMTP_PASS%" ^
    --upload-file email_payload.txt > nul
)

exit /b 0
