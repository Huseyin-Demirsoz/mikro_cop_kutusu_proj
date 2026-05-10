#!/usr/bin/env bash
# Linux/macOS için e-posta bildirim hook'u
# Argümanlar:
#   $1 = LEVEL
#   $2 = TITLE
#   $3 = MESSAGE

LEVEL="$1"
TITLE="$2"
MESSAGE="$3"

echo "[$(date)] $LEVEL - $TITLE - $MESSAGE" >> notification_log.txt

# Python betiğini çağırarak e-postayı gönderiyoruz.
python3 "$(dirname "$0")/send_email.py" "$LEVEL" "$TITLE" "$MESSAGE"

exit $?
