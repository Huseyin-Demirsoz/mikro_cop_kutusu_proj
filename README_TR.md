# Akıllı Çöp Kutusu Doluluk + Koku İzleme - Tamamlanmış Kod Paketi

Bu paket, proje gereksinimlerinde eksik kalan özellikleri kapatacak şekilde yeniden düzenlenmiş üç ana parçadan oluşur:

1. `smart_trash_esp32.ino`  
   ESP32 üzerinde çalışır. HC-SR04, MQ gaz sensörü, LCD, 4x4 keypad, LED/buzzer, kalibrasyon ve HTTP POST işlemlerini yapar.

2. `server.cpp`  
   PC üzerinde çalışır. ESP32'den gelen verileri alır, SQLite veritabanına kaydeder, web dashboard'a API sağlar ve bildirim olaylarını üretir.

3. `public/index.html`  
   Web dashboard'dur. Canlı doluluk/gaz değerlerini, günlük istatistikleri, geçmiş grafiğini, son kayıtları ve bildirim olaylarını gösterir.

---

## 1. Donanım Bağlantıları

### HC-SR04 Ultrasonik Sensör

| HC-SR04 | ESP32 |
|---|---|
| VCC | 5V |
| GND | GND |
| TRIG | GPIO5 |
| ECHO | GPIO18 üzerinden voltaj bölücü / level shifter |

**Önemli:** HC-SR04 ECHO pini 5V çıkış verebilir. ESP32 pinleri 3.3V toleranslıdır. Bu yüzden ECHO için voltaj bölücü kullanılmalıdır.

### MQ / Flying Fish Gaz Sensörü

| MQ Modülü | ESP32 |
|---|---|
| VCC | 5V |
| GND | GND |
| AO | GPIO34 üzerinden voltaj bölücü |
| DO | GPIO32 üzerinden voltaj bölücü / level shifter |

### LED ve Buzzer

| Bileşen | ESP32 |
|---|---|
| Yeşil LED | GPIO25 |
| Sarı LED | GPIO26 |
| Kırmızı LED | GPIO27 |
| Buzzer | GPIO33 |

### LCD I2C

| LCD I2C | ESP32 |
|---|---|
| SDA | GPIO21 |
| SCL | GPIO22 |
| VCC | 5V veya modüle göre 3.3V |
| GND | GND |

Kodda LCD adresi `0x27` olarak ayarlanmıştır. Ekran çalışmazsa `0x3F` denenebilir.

### 4x4 Keypad

Varsayılan pinler:

```cpp
byte rowPins[ROWS] = {4, 16, 17, 19};
byte colPins[COLS] = {23, 13, 12, 14};
```

Kendi bağlantınıza göre değiştirebilirsiniz.

---

## 2. Keypad Kullanımı

| Tuş | Görev |
|---|---|
| A | Boş çöp kutusu mesafesini kalibre eder |
| B | Dolu çöp kutusu mesafesini kalibre eder |
| C | Temiz hava gaz baseline değerini kalibre eder |
| D | Manuel boşaltma olayı gönderir |
| # | LCD sayfasını değiştirir |
| * | Veriyi hemen server'a gönderir |

---

## 3. ESP32 Kodunda Değiştirilecek Alanlar

`smart_trash_esp32.ino` içinde şunları değiştirin:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* API_URL = "http://192.168.1.100:8080/api/trash";
```

`192.168.1.100` yerine C++ server'ın çalıştığı bilgisayarın yerel IP adresini yazın.

Windows'ta IP öğrenmek için:

```bash
ipconfig
```

---

## 4. Server Çalıştırma

### Gerekli dosyalar

Server klasöründe şunlar bulunmalıdır:

```text
server.cpp
httplib.h
sqlite3.h
sqlite3.c
public/index.html
```

`httplib.h` dosyası cpp-httplib kütüphanesinden alınmalıdır.

### MinGW / g++ ile derleme örneği

Windows MinGW kullanıyorsanız:

```bash
g++ -std=c++17 server.cpp sqlite3.c -o smart_trash_server.exe -lws2_32
```

Linux/macOS tarafında sistem SQLite kütüphanesi varsa:

```bash
g++ -std=c++17 server.cpp -o smart_trash_server -lsqlite3 -pthread
```

Sonra çalıştırın:

```bash
./smart_trash_server
```

Windows'ta:

```bash
smart_trash_server.exe
```

Dashboard:

```text
http://localhost:8080/
```

ESP32 aynı ağa bağlıysa server'a şu formatta veri gönderir:

```text
http://PC_IP_ADRESI:8080/api/trash
```

---

## 5. API Endpointleri

| Endpoint | Görev |
|---|---|
| `POST /api/trash` | ESP32 sensör verisini kaydeder |
| `GET /api/live` | Son sensör kaydını döndürür |
| `GET /api/history?limit=100` | Son N kaydı döndürür |
| `GET /api/stats` | Günlük istatistikleri döndürür |
| `GET /api/notifications?after_id=0` | Bildirim olaylarını döndürür |
| `POST /api/test-notification` | Test bildirimi üretir |
| `GET /api/export.csv` | Verileri CSV olarak indirir |
| `GET /health` | Server durumunu kontrol eder |

---

## 6. Bildirim Sistemi

Bu paket üç seviyeli bildirim sağlar:

1. **Cihaz üzeri lokal uyarı:** LED + buzzer ESP32 tarafında çalışır.
2. **Web / mobil dashboard bildirimi:** Dashboard açıkken tarayıcı bildirimi alınabilir.
3. **E-posta / Telegram / IFTTT gibi dış bildirim:** `notify_hook.bat` veya `notify_hook.sh` ile opsiyonel olarak yapılır.

Server, alarm veya warning oluştuğunda aynı klasörde `notify_hook.bat` veya Linux/macOS için `notify_hook.sh` dosyasını arar. Dosya varsa şu argümanlarla çağırır:

```text
notify_hook LEVEL TITLE MESSAGE
```

Windows için `notify_hook_example.bat` dosyasını `notify_hook.bat` olarak yeniden adlandırıp içini kendi token/e-posta bilgilerinize göre düzenleyebilirsiniz.

---

## 7. Proje Gereksinimi Kapsama Durumu

| Gereksinim | Bu paketteki karşılığı |
|---|---|
| Mikrodenetleyici | ESP32 firmware |
| Ultrasonik sensör | HC-SR04 mesafe + doluluk yüzdesi |
| Gaz sensörü | MQ analog + digital reading |
| Wi-Fi + bulut/servis | Wi-Fi HTTP POST + REST API |
| LCD + dokunmatik/keypad | I2C LCD + 4x4 keypad |
| LED/buzzer | Yerel alarm göstergesi |
| Doluluk yüzdesi hesaplama | ESP32 tarafında kalibrasyonlu hesaplama |
| Eşik aşımı uyarısı | NORMAL / WARNING / ALARM |
| Mobil bildirim | Dashboard browser notification + hook |
| E-posta bildirimi | notify_hook ile yapılandırılabilir |
| Günlük doluluk istatistiği | `/api/stats` |
| Sensör kalibrasyon modu | Keypad A/B/C |
| Web istatistik arayüzü | `public/index.html` |

---

## 8. Demo Akışı

1. Server'ı çalıştırın.
2. Dashboard'u `http://localhost:8080/` üzerinden açın.
3. ESP32 kodunda Wi-Fi ve server IP bilgilerini düzenleyin.
4. ESP32'ye kodu yükleyin.
5. LCD'de canlı değerleri kontrol edin.
6. Keypad ile kalibrasyon yapın:
   - Boş kutuda `A`
   - Dolu kabul edilen seviyede `B`
   - Temiz havada `C`
7. Doluluk veya gaz eşiği aşıldığında:
   - LED/buzzer çalışır.
   - Server kayıt alır.
   - Dashboard alarm gösterir.
   - Bildirim olayı oluşur.
   - Hook dosyası ayarlanmışsa e-posta/Telegram/IFTTT gönderimi tetiklenir.
