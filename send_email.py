import smtplib
import sys
from email.message import EmailMessage

if len(sys.argv) < 4:
    print("Usage: python3 send_email.py <LEVEL> <TITLE> <MESSAGE>")
    sys.exit(1)

level = sys.argv[1]
title = sys.argv[2]
message = sys.argv[3]

# -------------------------------------------------------------
# E-POSTA AYARLARI 
# -------------------------------------------------------------
SMTP_SERVER = "smtp.gmail.com"
SMTP_PORT = 587
EMAIL_FROM = "enesudemy2@gmail.com"
EMAIL_TO = "enesudemy2@gmail.com"
SMTP_USERNAME = "enesudemy2@gmail.com"
SMTP_PASSWORD = "lgohqzatlgevatuc"
# -------------------------------------------------------------

msg = EmailMessage()
msg.set_content(f"Level: {level}\n\nMessage:\n{message}")
msg['Subject'] = f"Smart Trash Bin: {title}"
msg['From'] = EMAIL_FROM
msg['To'] = EMAIL_TO

try:
    with smtplib.SMTP(SMTP_SERVER, SMTP_PORT) as server:
        server.starttls()
        server.login(SMTP_USERNAME, SMTP_PASSWORD)
        server.send_message(msg)
    print("Email sent successfully.")
except Exception as e:
    print(f"Error sending email: {e}")
    sys.exit(1)
