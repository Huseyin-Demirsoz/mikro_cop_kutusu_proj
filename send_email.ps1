param (
    [string]$Level,
    [string]$Title,
    [string]$Message
)

# -------------------------------------------------------------
# E-POSTA AYARLARI 
# -------------------------------------------------------------
$SMTPServer = "smtp.gmail.com"
$SMTPPort = 587
$EmailFrom = "enesudemy2@gmail.com"
$EmailTo = "enesudemy2@gmail.com"
$SMTPUsername = "enesudemy2@gmail.com"
# Gmail için "Uygulama Şifresi" 
$SMTPPassword = "lgohqzatlgevatuc"
# -------------------------------------------------------------

try {
    $mail = New-Object System.Net.Mail.MailMessage
    $mail.From = New-Object System.Net.Mail.MailAddress($EmailFrom)
    $mail.To.Add($EmailTo)
    $mail.Subject = "Smart Trash Bin: $Title"
    $mail.Body = "Level: $Level`n`nMessage:`n$Message"
    $mail.IsBodyHtml = $false
    $mail.SubjectEncoding = [System.Text.Encoding]::UTF8
    $mail.BodyEncoding = [System.Text.Encoding]::UTF8

    $smtp = New-Object System.Net.Mail.SmtpClient($SMTPServer, $SMTPPort)
    $smtp.EnableSsl = $true
    $smtp.Credentials = New-Object System.Net.NetworkCredential($SMTPUsername, $SMTPPassword)
    
    $smtp.Send($mail)
    
    Write-Host "Email sent successfully."
    exit 0
} catch {
    Write-Error "Error sending email: $_"
    exit 1
}
