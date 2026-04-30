#!/usr/bin/env bash
# Smart Trash Bin notification hook example for Linux/macOS.
# Rename this file to notify_hook.sh and run: chmod +x notify_hook.sh
# Arguments:
#   $1 = LEVEL
#   $2 = TITLE
#   $3 = MESSAGE

LEVEL="$1"
TITLE="$2"
MESSAGE="$3"

echo "[$(date)] $LEVEL - $TITLE - $MESSAGE" >> notification_log.txt

# Example 1: Telegram mobile notification
if [[ -n "$TELEGRAM_BOT_TOKEN" && -n "$TELEGRAM_CHAT_ID" ]]; then
  curl -s -X POST "https://api.telegram.org/bot${TELEGRAM_BOT_TOKEN}/sendMessage" \
    -d "chat_id=${TELEGRAM_CHAT_ID}" \
    -d "text=${TITLE} - ${MESSAGE}" > /dev/null
fi

# Example 2: SMTP e-mail with curl
if [[ -n "$SMTP_URL" && -n "$EMAIL_TO" ]]; then
  {
    echo "Subject: ${TITLE}"
    echo
    echo "${MESSAGE}"
  } > email_payload.txt

  curl --url "$SMTP_URL" --ssl-reqd \
    --mail-from "$EMAIL_FROM" \
    --mail-rcpt "$EMAIL_TO" \
    --user "$SMTP_USER:$SMTP_PASS" \
    --upload-file email_payload.txt > /dev/null
fi

exit 0
