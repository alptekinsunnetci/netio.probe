/* =====================================================================================
 *  netio.probe — ESP8266 NodeMCU DS18B20 SNMP v2c Agent  (v1.2) - Alptekin Sünnetci   *
 * =====================================================================================
 *
 *  Bu firmware, ESP8266 (NodeMCU) üzerinde DS18B20'den sıcaklık okur ve bu değeri
 *  GERÇEK bir SNMP v2c agent olarak yayınlar. Ek olarak ilk açılışta bir WiFi
 *  yapılandırma portalı (Access Point + captive portal) sunar; kullanıcı SSID/şifre
 *  girince cihaz STA (istemci) moduna geçip ağa bağlanır.
 *
 *  ÇALIŞMA MANTIĞI (AP -> STA):
 *    1) Açılışta EEPROM'dan kayıtlı WiFi bilgisi okunur.
 *    2) Kayıt yoksa VEYA bağlantı başarısızsa -> AP modunda "config portalı" açılır.
 *       Telefon/PC ile "netio.probe-XXXX" ağına bağlanıp tarayıcıdan ayarlar girilir.
 *    3) Ayarlar kaydedilince cihaz yeniden başlar ve STA modunda ağa bağlanır.
 *    4) STA modunda çalışır: SNMP agent (UDP/161) + OTA + mDNS + ŞİFRELİ web yönetim
 *       arayüzü (http://<ip>/). Web UI'dan SNMP community, sysName, WiFi vb. değiştirilebilir;
 *       .bin yüklenerek firmware güncellenebilir (web OTA); fabrika ayarına dönülebilir.
 *    Çalışırken WiFi 2 dakikadan uzun koparsa cihaz yeniden başlar (tekrar portala düşer).
 *
 *  FABRİKA AYARLARINA DÖNÜŞ (WiFi/şifre dahil her şey sıfırlanır):
 *    a) Web UI'daki "Fabrika ayarları" düğmesi, VEYA
 *    b) GPIO14 (D5) pini ile GND'yi ~2 sn kısa devre yapmak (iki pin köprüleme), VEYA
 *    c) GPIO0 (FLASH butonu) 3 sn basılı tutmak.
 *
 *  ÖNEMLİ DONANIM NOTU (DS18B20):
 *    1-Wire için HARİCİ 4.7kΩ pull-up direnci DATA <-> 3V3 arasına TAKILMALIDIR.
 *    ESP8266'nın dahili pull-up'ı (~30k+) 1-Wire zamanlaması için zayıf ve güvenilmezdir;
 *    özellikle WiFi aktifken okuma hataları görülür. Zorunlu kalırsanız aşağıdaki
 *    USE_INTERNAL_PULLUP'ı 1 yapabilirsiniz ama ÖNERİLMEZ.
 *
 *  BAĞLANTI:
 *    DS18B20 VDD  -> 3V3
 *    DS18B20 GND  -> GND
 *    DS18B20 DATA -> GPIO4 (NodeMCU silkscreen: D2)   + 4.7kΩ -> 3V3
 *    FABRİKA RESET -> GPIO14 (D5) <-> GND kısa devre (~2 sn) = tüm ayarları sil
 *
 *  TEST KOMUTLARI (net-snmp):
 *    snmpwalk  -v2c -c public <IP> .1.3.6.1.2.1.1                 (System grubu)
 *    snmpwalk  -v2c -c public <IP> .1.3.6.1.2.1.99                (Entity-Sensor)
 *    snmpwalk  -v2c -c public <IP> .1.3.6.1.4.1.63333             (Özel/diagnostik)
 *    snmpget   -v2c -c public <IP> .1.3.6.1.2.1.99.1.1.1.4.1      (Sıcaklık x10, °C)
 *    snmpget   -v2c -c public <IP> .1.3.6.1.4.1.63333.10.1.0      (Sıcaklık x1000, m°C)
 *
 *  ESKİ KODA GÖRE BAŞLICA DÜZELTMELER:
 *    - SNMP katmanı tamamen yeniden yazıldı: gerçek BER/ASN.1 kodlama + GET/GETNEXT
 *      (GETBULK tek-tekrar olarak) -> snmpwalk/snmpget artık ÇALIŞIR.
 *    - 1-Wire zamanlaması standartlaştırıldı + kritik anlarda interrupt kapatma
 *      (WiFi kesintileri okumayı bozmasın diye). Bloklamayan dönüşüm (loop kilitlenmez).
 *    - Bozuk "Search ROM" kaldırıldı; tek sensör için Skip ROM kullanılıyor.
 *    - Entity-Sensor OID'i .4.1.0 yerine doğru biçimde .4.1 (sondaki .0 hataydı).
 *    - Sabit kodlanmış WiFi bilgisi kaldırıldı; AP/captive portal ile yapılandırma.
 *    - Bozuk EEPROM key/value mantığı yerine sürüm + sağlama'lı tek struct.
 *    - snmpRequestCount artık her loop'ta değil, gerçek SNMP paketinde artıyor.
 *
 *  v1.1 YENİLİKLER:
 *    - STA modunda HTTP Basic Auth korumalı web yönetim arayüzü (ayar değiştirme).
 *    - Web tabanlı OTA: tarayıcıdan .bin yükleyerek firmware güncelleme (/update).
 *    - Fabrika ayarına dönüş: web UI + GPIO14<->GND donanım köprüsü (+ GPIO0 butonu).
 *    - Yönetici kullanıcı adı/şifresi yapılandırılabilir; SNMP community canlı değişir.
 *    NOT: HTTP Basic Auth ve SNMP v2c düz metindir; güvenilir ağda kullanın.
 *
 *  v1.2 YENİLİKLER (güvenlik + gözlemlenebilirlik + dayanıklılık):
 *    - Yönetici parolası artık DÜZ saklanmaz: salt + SHA-256 hash (BearSSL).
 *    - Kaba-kuvvet kilidi: kaynak IP başına N hatadan sonra geçici kilit.
 *    - Kaynak-IP ACL (CIDR): hem SNMP hem web/metrics yalnızca izinli ağdan erişilir.
 *    - Prometheus /metrics ucu (ACL korumalı, login gerektirmez).
 *    - Syslog (RFC 5424 / UDP): boot, config, OTA, sensör arızası ve AUTH-FAIL olayları.
 *    - Web OTA için opsiyonel MD5 bütünlük doğrulaması.
 *    - DS18B20: tüm-0x00/0xFF scratchpad (hat arızası) reddi; sensör durum geçişi loglanır.
 *    - EEPROM migrasyonu: v1.1 -> v1.2 geçişinde Wi-Fi/ayarlar KORUNUR (sıfırlanmaz).
 *    - Watchdog beslemesi + heap-taban koruması (uzun süre düşük heap -> kontrollü reboot).
 *    - Portal/admin parolası alanı artık önceden DOLDURULMAZ (parola sızıntısı kapatıldı).
 *
 *  Derleme: Arduino IDE / arduino-cli, Board = "NodeMCU 1.0 (ESP-12E Module)",
 *           ESP8266 Arduino Core 3.x. Harici kütüphane GEREKMEZ (hepsi core içinde).
 * ===================================================================================== */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <bearssl/bearssl.h>   // SHA-256 (yönetici parolası hash'i için)
#include <stddef.h>            // offsetof
#include <stdarg.h>            // syslogf (vararg)

/* ===================================== AYARLAR ===================================== */

#define DS18B20_PIN          4          // GPIO4 (D2). Güvenli GPIO'lar: 4, 5, 12, 13, 14
#define USE_INTERNAL_PULLUP  0          // 1 = dahili pull-up (ÖNERİLMEZ), 0 = harici 4.7kΩ

#define SNMP_PORT            161
#define AP_PASSWORD          "snmpsetup" // Config portalı WPA2 şifresi (min 8 karakter)
#define STA_CONNECT_TIMEOUT  20000UL     // STA bağlanma timeout (ms)
#define WIFI_LOST_PORTAL_MS  120000UL    // Çalışırken WiFi bu süreden uzun koparsa portala dön
#define SENSOR_INTERVAL      5000UL      // Sıcaklık ölçüm aralığı (ms)
#define DS18B20_CONV_MS      750UL       // 12-bit dönüşüm süresi (ms)

#define CFG_MAGIC            0xA5C30004UL // EEPROM imzası (v1.2: ACL + syslog + parola hash)
#define CFG_MAGIC_PREV         0xA5C30003UL // v1.1 yapısı (migrasyon kaynağı)
#define ENTERPRISE_PEN       63333UL      // ÖRNEK private enterprise no. Üretimde IANA'dan kendi PEN'inizi alın.

#define FACTORY_RESET_PIN    14           // GPIO14 (D5). Bu pin <-> GND kısa devre = fabrika reset
#define FACTORY_RESET_HOLD_MS 2000UL      // Köprü bu kadar süre kapalı kalırsa sıfırla
#define WEB_PORT             80

#define AUTH_MAX_FAILS       5            // bu kadar hatalı girişten sonra kaynak IP kilitlenir
#define AUTH_LOCKOUT_MS      60000UL      // kilit süresi (ms)
#define HEAP_FLOOR           6000UL       // boş heap bu eşiğin altında uzun kalırsa reboot
#define HEAP_FLOOR_MS        15000UL      // ne kadar süre (ms)
#define SYSLOG_DEFAULT_PORT  514

// RFC 5424 severity seviyeleri
#define SLOG_ERR     3
#define SLOG_WARNING 4
#define SLOG_NOTICE  5
#define SLOG_INFO    6

/* ============================== KONFİGÜRASYON DEPOSU =============================== */

// NOT: Arduino otomatik-prototip üreticisi, AuthSlot* döndüren fonksiyon için
// dosyanın başına prototip ekler; bu yüzden tip ilk fonksiyondan ÖNCE tanımlanmalı.
struct AuthSlot { uint32_t ip; uint8_t fails; uint32_t until; };

struct Config {
  uint32_t magic;
  uint32_t aclNet;       // izinli ağ (host byte order); aclBits=0 ise yok sayılır
  uint16_t syslogPort;   // syslog UDP portu (0/boşsa 514)
  uint8_t  aclBits;      // CIDR prefix (0 = tüm IP'lere izin)
  uint8_t  configured;   // 1 = WiFi yapılandırıldı
  char ssid[33];
  char pass[65];
  char roComm[33];       // SNMP read-only community
  char sysName[33];      // SNMPv2-MIB::sysName
  char sysLocation[65];  // sysLocation
  char sysContact[65];   // sysContact
  char otaPass[33];      // espota (ArduinoOTA) şifresi
  char adminUser[33];    // web UI yönetici kullanıcı adı
  char adminSalt[17];    // 8 bayt -> 16 hex (parola tuzu)
  char adminHash[65];    // SHA-256(salt|parola) -> 64 hex (parola DÜZ saklanmaz)
  char syslogIp[16];     // syslog sunucu IP (boş = kapalı)
  uint8_t checksum;      // basit XOR sağlama (DAİMA EN SON ALAN)
};

Config cfg;

static uint8_t cfgChecksum(const Config &c) {
  const uint8_t *p = (const uint8_t *)&c;
  uint8_t x = 0;
  // checksum alanından ÖNCEKİ tüm baytları XOR'la (hizalama dolgusunu dışla)
  size_t n = offsetof(Config, checksum);
  for (size_t i = 0; i < n; i++) x ^= p[i];
  return x;
}

static void copyStr(char *dst, const char *src, size_t dstSize) {
  if (dstSize == 0) return;
  size_t i = 0;
  for (; src[i] && i < dstSize - 1; i++) dst[i] = src[i];
  dst[i] = '\0';
}

/* ----------------------- güvenlik / ağ yardımcıları (v1.2) ----------------------- */

static String hostName();   // ileri bildirim (syslog kullanır)

// SHA-256(salt | parola) -> 64 karakter hex
static void sha256Hex(const char *salt, const char *pass, char *out65) {
  br_sha256_context c;
  br_sha256_init(&c);
  br_sha256_update(&c, salt, strlen(salt));
  br_sha256_update(&c, pass, strlen(pass));
  uint8_t d[32];
  br_sha256_out(&c, d);
  static const char *hx = "0123456789abcdef";
  for (int i = 0; i < 32; i++) { out65[i * 2] = hx[d[i] >> 4]; out65[i * 2 + 1] = hx[d[i] & 0x0F]; }
  out65[64] = '\0';
}

// Donanım RNG'den 8 baytlık tuz (16 hex)
static void genSalt(char *out17) {
  static const char *hx = "0123456789abcdef";
  for (int i = 0; i < 8; i++) {
    uint8_t b = (uint8_t)(RANDOM_REG32 & 0xFF);
    out17[i * 2] = hx[b >> 4]; out17[i * 2 + 1] = hx[b & 0x0F];
  }
  out17[16] = '\0';
}

static void setAdminPassword(const char *pw) {
  genSalt(cfg.adminSalt);
  sha256Hex(cfg.adminSalt, pw, cfg.adminHash);
}

static bool checkAdminPassword(const char *pw) {
  char h[65];
  sha256Hex(cfg.adminSalt, pw, h);
  uint8_t diff = 0;                       // sabit-zaman karşılaştırma
  for (int i = 0; i < 64; i++) diff |= (uint8_t)(cfg.adminHash[i] ^ h[i]);
  return diff == 0 && cfg.adminHash[0] != '\0';
}

// Basic Auth başlığı için minimal base64 çözücü
static String b64Decode(const String &in) {
  String out;
  int buf = 0, bits = 0;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i]; int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A';
    else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
    else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62;
    else if (c == '/') v = 63;
    else continue;                        // '=' ve diğerlerini atla
    buf = (buf << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)((buf >> bits) & 0xFF); }
  }
  return out;
}

// ---- Kaynak-IP ACL (CIDR) ----
static bool ipAllowed(IPAddress ip) {
  if (cfg.aclBits == 0) return true;      // 0 = herkese izin
  uint32_t a = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
  uint32_t mask = (cfg.aclBits >= 32) ? 0xFFFFFFFFUL : (0xFFFFFFFFUL << (32 - cfg.aclBits));
  return (a & mask) == (cfg.aclNet & mask);
}
static bool parseCidr(const String &s, uint32_t &net, uint8_t &bits) {
  int slash = s.indexOf('/');
  if (slash < 0) return false;
  IPAddress ip;
  if (!ip.fromString(s.substring(0, slash))) return false;
  int b = s.substring(slash + 1).toInt();
  if (b < 0 || b > 32) return false;
  net = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
  bits = (uint8_t)b;
  return true;
}
static String cidrStr() {
  if (cfg.aclBits == 0) return String("0.0.0.0/0");
  IPAddress ip((cfg.aclNet >> 24) & 0xFF, (cfg.aclNet >> 16) & 0xFF, (cfg.aclNet >> 8) & 0xFF, cfg.aclNet & 0xFF);
  return ip.toString() + "/" + String(cfg.aclBits);
}

// ---- Syslog (RFC 5424 / UDP) ----
static WiFiUDP slogUdp;
static void syslogSend(int severity, const char *msg) {
  Serial.print(F("[LOG] ")); Serial.println(msg);
  if (cfg.syslogIp[0] == '\0' || WiFi.status() != WL_CONNECTED) return;
  IPAddress ip;
  if (!ip.fromString(cfg.syslogIp)) return;
  int pri = 16 * 8 + severity;            // facility local0(16) * 8 + severity
  String s = "<" + String(pri) + ">1 - " + hostName() + " netio.probe - - - " + msg;
  uint16_t port = cfg.syslogPort ? cfg.syslogPort : SYSLOG_DEFAULT_PORT;
  if (slogUdp.beginPacket(ip, port)) {
    slogUdp.write((const uint8_t *)s.c_str(), s.length());
    slogUdp.endPacket();
  }
}
static void syslogf(int severity, const char *fmt, ...) {
  char b[200];
  va_list a; va_start(a, fmt);
  vsnprintf(b, sizeof(b), fmt, a);
  va_end(a);
  syslogSend(severity, b);
}

// ---- Kaba-kuvvet kilidi (kaynak IP başına) ----  (AuthSlot tipi yukarıda tanımlı)
static AuthSlot authSlots[4];
static AuthSlot *authSlotFor(uint32_t ip) {
  AuthSlot *free = nullptr, *oldest = &authSlots[0];
  for (auto &s : authSlots) {
    if (s.ip == ip) return &s;
    if (s.ip == 0 && !free) free = &s;
    if (s.until < oldest->until) oldest = &s;
  }
  AuthSlot *slot = free ? free : oldest;
  slot->ip = ip; slot->fails = 0; slot->until = 0;
  return slot;
}
static bool authLockedOut(IPAddress ip) {
  uint32_t k = (uint32_t)ip;
  for (auto &s : authSlots) if (s.ip == k) return millis() < s.until;
  return false;
}
static void authNoteFail(IPAddress ip) {
  AuthSlot *s = authSlotFor((uint32_t)ip);
  if (s->fails < 255) s->fails++;
  if (s->fails >= AUTH_MAX_FAILS) s->until = millis() + AUTH_LOCKOUT_MS;
}
static void authNoteOK(IPAddress ip) {
  AuthSlot *s = authSlotFor((uint32_t)ip);
  s->fails = 0; s->until = 0;
}

static void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  copyStr(cfg.roComm, "public", sizeof(cfg.roComm));
  copyStr(cfg.sysName, "netio.probe", sizeof(cfg.sysName));
  copyStr(cfg.sysLocation, "", sizeof(cfg.sysLocation));
  copyStr(cfg.sysContact, "", sizeof(cfg.sysContact));
  copyStr(cfg.otaPass, "esp8266ota", sizeof(cfg.otaPass));
  copyStr(cfg.adminUser, "admin", sizeof(cfg.adminUser));
  setAdminPassword("netioprobe");          // !! İLK GİRİŞTE DEĞİŞTİRİN (hash olarak saklanır)
  cfg.aclNet = 0; cfg.aclBits = 0;          // ACL kapalı (tüm IP'lere izin)
  cfg.syslogIp[0] = '\0'; cfg.syslogPort = SYSLOG_DEFAULT_PORT;
  cfg.configured = 0;
}

// v1.1 EEPROM yapısı (yalnızca migrasyon için, AYNEN korunmalı)
struct ConfigPrev {
  uint32_t magic;
  char ssid[33]; char pass[65]; char roComm[33]; char rwComm[33];
  char sysName[33]; char sysLocation[65]; char sysContact[65]; char otaPass[33];
  char adminUser[33]; char adminPass[33];
  uint8_t configured; uint8_t checksum;
};

static void migrateFromPrev() {
  ConfigPrev old;
  EEPROM.get(0, old);
  setDefaults();                            // yeni alanlar varsayılan
  copyStr(cfg.ssid, old.ssid, sizeof(cfg.ssid));
  copyStr(cfg.pass, old.pass, sizeof(cfg.pass));
  copyStr(cfg.roComm, old.roComm, sizeof(cfg.roComm));
  copyStr(cfg.sysName, old.sysName, sizeof(cfg.sysName));
  copyStr(cfg.sysLocation, old.sysLocation, sizeof(cfg.sysLocation));
  copyStr(cfg.sysContact, old.sysContact, sizeof(cfg.sysContact));
  copyStr(cfg.otaPass, old.otaPass, sizeof(cfg.otaPass));
  copyStr(cfg.adminUser, old.adminUser[0] ? old.adminUser : "admin", sizeof(cfg.adminUser));
  setAdminPassword(old.adminPass[0] ? old.adminPass : "netioprobe");  // düz parolayı hash'e çevir
  cfg.configured = old.configured;
}

static void loadConfig() {
  uint32_t magic;
  EEPROM.get(0, magic);
  if (magic == CFG_MAGIC) {
    EEPROM.get(0, cfg);
    if (cfg.checksum == cfgChecksum(cfg)) { Serial.println(F("[CFG] Yapılandırma EEPROM'dan yüklendi.")); return; }
    Serial.println(F("[CFG] Checksum hatası -> varsayılanlar."));
    setDefaults();
  } else if (magic == CFG_MAGIC_PREV) {
    Serial.println(F("[CFG] v1.1 yapısı algılandı -> v1.2'ye taşınıyor (Wi-Fi/ayarlar korunuyor)."));
    migrateFromPrev();
    saveConfig();
  } else {
    Serial.println(F("[CFG] Geçerli yapılandırma yok, varsayılanlar yükleniyor."));
    setDefaults();
  }
}

static void saveConfig() {
  cfg.magic = CFG_MAGIC;
  cfg.checksum = cfgChecksum(cfg);
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println(F("[CFG] Yapılandırma kaydedildi."));
  syslogf(SLOG_NOTICE, "config saved");
}

static void resetConfig() {
  setDefaults();
  saveConfig();
}

/* ============================== GLOBAL DURUM/SAYAÇLAR ============================== */

enum RunMode { MODE_PORTAL, MODE_RUN };
RunMode g_mode = MODE_RUN;

unsigned long bootMillis = 0;

// SNMP'te yayınlanan canlı değerler
int32_t  g_tempDeci   = 0;   // sıcaklık * 10  (Entity-Sensor value, precision=1)
int32_t  g_tempMilli  = 0;   // sıcaklık * 1000 (özel m°C OID)
int32_t  g_sensorOper = 2;   // 1 = ok, 2 = unavailable

uint32_t g_readCount  = 0;
uint32_t g_errCount   = 0;
uint32_t g_snmpCount  = 0;

bool     g_otaAuthFail = false;   // web OTA sırasında yetki kontrol bayrağı

ESP8266WebServer server(WEB_PORT);
DNSServer        dnsServer;
WiFiUDP          udp;

static String hostName() {
  // mDNS tek-etiket adı: nokta yerine tire (tek bir etiket olmalı)
  return "netio-probe-" + String(ESP.getChipId(), HEX);
}
static String apSsid() {
  char buf[20];
  snprintf(buf, sizeof(buf), "netio.probe-%04X", (uint16_t)(ESP.getChipId() & 0xFFFF));
  return String(buf);
}

/* ================================ DS18B20 SÜRÜCÜ ================================== */
/*
 * Standart 1-Wire zamanlaması. Bit seviyesindeki kritik pencerelerde interrupt'lar
 * kısa süreli kapatılır; aralarda açıktır (WiFi yığını "aç kalır"). 750ms dönüşüm
 * BEKLEMESİ burada YAPILMAZ -> ana loop'taki durum makinesi bloklamadan yönetir.
 */
class DS18B20 {
public:
  explicit DS18B20(uint8_t p) : pin(p) {}

  void begin() {
#if USE_INTERNAL_PULLUP
    pinMode(pin, INPUT_PULLUP);
#else
    pinMode(pin, INPUT);   // harici 4.7kΩ pull-up bus'ı HIGH tutar
#endif
    Serial.print(F("[DS18B20] Pin: GPIO")); Serial.println(pin);
    Serial.println(reset() ? F("[DS18B20] Sensör algılandı (presence).")
                           : F("[DS18B20] UYARI: presence yok! Kablo/pull-up kontrol edin."));
    printRomAddress();
  }

  // Dönüşümü başlat (bloklamaz). Sensör yoksa false.
  bool startConversion() {
    if (!reset()) return false;
    writeByte(0xCC);  // Skip ROM (tek cihaz)
    writeByte(0x44);  // Convert T
    return true;
  }

  // Scratchpad'i oku ve °C döndür. Hata olursa NAN.
  float readTemperature() {
    if (!reset()) return NAN;
    writeByte(0xCC);  // Skip ROM
    writeByte(0xBE);  // Read Scratchpad

    uint8_t d[9];
    for (int i = 0; i < 9; i++) d[i] = readByte();
    if (crc8(d, 8) != d[8]) return NAN;

    // Hat arızası: tüm baytlar 0x00 (bus LOW takılı -> CRC=0 ile geçebilir) veya 0xFF
    bool allZero = true, allFF = true;
    for (int i = 0; i < 9; i++) { if (d[i] != 0x00) allZero = false; if (d[i] != 0xFF) allFF = false; }
    if (allZero || allFF) return NAN;

    int16_t raw = (int16_t)((d[1] << 8) | d[0]);
    float t = raw * 0.0625f;          // 12-bit: LSB = 1/16 °C
    if (t < -55.0f || t > 125.0f) return NAN;
    return t;
  }

  void printRomAddress() {
    if (!reset()) return;
    writeByte(0x33);                  // Read ROM (yalnızca tek cihazda geçerli)
    uint8_t rom[8];
    for (int i = 0; i < 8; i++) rom[i] = readByte();
    if (rom[0] == 0x28 && crc8(rom, 7) == rom[7]) {
      Serial.print(F("[DS18B20] ROM: "));
      for (int i = 0; i < 8; i++) { if (rom[i] < 16) Serial.print('0'); Serial.print(rom[i], HEX); Serial.print(' '); }
      Serial.println();
    }
  }

private:
  uint8_t pin;

#if USE_INTERNAL_PULLUP
  inline void release() { pinMode(pin, INPUT_PULLUP); }
#else
  inline void release() { pinMode(pin, INPUT); }
#endif

  bool reset() {
    uint8_t presence;
    noInterrupts();
    pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
    delayMicroseconds(480);
    release();
    delayMicroseconds(70);
    presence = digitalRead(pin);      // 0 = sensör var
    interrupts();
    delayMicroseconds(410);
    return (presence == 0);
  }

  void writeBit(uint8_t b) {
    noInterrupts();
    pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
    if (b) { delayMicroseconds(6);  release(); delayMicroseconds(64); }
    else   { delayMicroseconds(60); release(); delayMicroseconds(10); }
    interrupts();
  }

  uint8_t readBit() {
    uint8_t b;
    noInterrupts();
    pinMode(pin, OUTPUT); digitalWrite(pin, LOW);
    delayMicroseconds(6);
    release();
    delayMicroseconds(9);
    b = digitalRead(pin);
    interrupts();
    delayMicroseconds(55);
    return b;
  }

  void writeByte(uint8_t v) { for (int i = 0; i < 8; i++) { writeBit(v & 1); v >>= 1; } }       // LSB-first
  uint8_t readByte() { uint8_t v = 0; for (int i = 0; i < 8; i++) v |= (readBit() << i); return v; }

  static uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
      uint8_t b = data[i];
      for (uint8_t j = 0; j < 8; j++) {
        uint8_t mix = (crc ^ b) & 0x01;
        crc >>= 1; if (mix) crc ^= 0x8C; b >>= 1;
      }
    }
    return crc;
  }
};

DS18B20 ds18b20(DS18B20_PIN);

/* ================================ SNMP v2c AGENT ================================== */
/*
 * Minimal ama GERÇEK BER/ASN.1 kodlamalı SNMP v2c agent.
 * Desteklenen: GET (0xA0), GETNEXT (0xA1), GETBULK (0xA5 -> tek-tekrarlı GETNEXT gibi).
 * SET desteklenmez (read-only). snmpwalk (GETNEXT) ve snmpget ile çalışır.
 */

// ---- MIB değer tipleri (encode için) ----
enum MibId {
  MIB_SYS_DESCR, MIB_SYS_OBJECTID, MIB_SYS_UPTIME, MIB_SYS_CONTACT,
  MIB_SYS_NAME, MIB_SYS_LOCATION, MIB_SYS_SERVICES,
  MIB_SENSOR_TYPE, MIB_SENSOR_SCALE, MIB_SENSOR_PRECISION, MIB_SENSOR_VALUE, MIB_SENSOR_STATUS,
  MIB_TEMP_MILLI, MIB_FREE_HEAP, MIB_WIFI_RSSI, MIB_UPTIME_SEC,
  MIB_READ_COUNT, MIB_ERR_COUNT, MIB_SNMP_COUNT
};

struct MibNode {
  uint32_t oid[16];
  uint8_t  oidLen;
  uint8_t  id;
};

// LEXİKOGRAFİK SIRADA tutulmalı (GETNEXT için şart).
static const MibNode MIB[] = {
  // --- SNMPv2-MIB::system  (1.3.6.1.2.1.1) ---
  {{1,3,6,1,2,1,1,1,0}, 9, MIB_SYS_DESCR},
  {{1,3,6,1,2,1,1,2,0}, 9, MIB_SYS_OBJECTID},
  {{1,3,6,1,2,1,1,3,0}, 9, MIB_SYS_UPTIME},
  {{1,3,6,1,2,1,1,4,0}, 9, MIB_SYS_CONTACT},
  {{1,3,6,1,2,1,1,5,0}, 9, MIB_SYS_NAME},
  {{1,3,6,1,2,1,1,6,0}, 9, MIB_SYS_LOCATION},
  {{1,3,6,1,2,1,1,7,0}, 9, MIB_SYS_SERVICES},
  // --- ENTITY-SENSOR-MIB::entPhySensorEntry (1.3.6.1.2.1.99.1.1.1.<col>.<idx=1>) ---
  {{1,3,6,1,2,1,99,1,1,1,1,1}, 12, MIB_SENSOR_TYPE},
  {{1,3,6,1,2,1,99,1,1,1,2,1}, 12, MIB_SENSOR_SCALE},
  {{1,3,6,1,2,1,99,1,1,1,3,1}, 12, MIB_SENSOR_PRECISION},
  {{1,3,6,1,2,1,99,1,1,1,4,1}, 12, MIB_SENSOR_VALUE},     // sıcaklık x10 (°C)
  {{1,3,6,1,2,1,99,1,1,1,5,1}, 12, MIB_SENSOR_STATUS},
  // --- Özel/diagnostik (1.3.6.1.4.1.<PEN>.10.x.0) ---
  {{1,3,6,1,4,1,ENTERPRISE_PEN,10,1,0}, 10, MIB_TEMP_MILLI}, // sıcaklık x1000 (m°C)
  {{1,3,6,1,4,1,ENTERPRISE_PEN,10,2,0}, 10, MIB_FREE_HEAP},  // boş heap (Gauge32)
  {{1,3,6,1,4,1,ENTERPRISE_PEN,10,3,0}, 10, MIB_WIFI_RSSI},  // RSSI dBm (Integer32)
  {{1,3,6,1,4,1,ENTERPRISE_PEN,10,4,0}, 10, MIB_UPTIME_SEC}, // uptime saniye (Gauge32)
  {{1,3,6,1,4,1,ENTERPRISE_PEN,10,5,0}, 10, MIB_READ_COUNT}, // ölçüm sayısı (Counter32)
  {{1,3,6,1,4,1,ENTERPRISE_PEN,10,6,0}, 10, MIB_ERR_COUNT},  // hata sayısı (Counter32)
  {{1,3,6,1,4,1,ENTERPRISE_PEN,10,7,0}, 10, MIB_SNMP_COUNT}, // SNMP istek (Counter32)
};
static const int MIB_COUNT = sizeof(MIB) / sizeof(MIB[0]);

static const char *SYS_DESCR = "netio.probe v1.2 - ESP8266 DS18B20 SNMP agent";

// Yanıt için statik tamponlar (tek thread, loop içinde sıralı kullanım)
static uint8_t S_VB[600];
static uint8_t S_PDU[680];
static uint8_t S_MSG[720];

// --- BER yardımcıları ---
static size_t berLen(uint8_t *b, size_t len) {
  if (len < 0x80) { b[0] = (uint8_t)len; return 1; }
  if (len < 0x100) { b[0] = 0x81; b[1] = (uint8_t)len; return 2; }
  b[0] = 0x82; b[1] = (uint8_t)(len >> 8); b[2] = (uint8_t)(len & 0xFF); return 3;
}

static size_t b128(uint8_t *b, uint32_t v) {
  uint8_t tmp[5]; int n = 0;
  tmp[n++] = v & 0x7F; v >>= 7;
  while (v) { tmp[n++] = (v & 0x7F) | 0x80; v >>= 7; }
  for (int i = 0; i < n; i++) b[i] = tmp[n - 1 - i];
  return n;
}

static size_t berOID(uint8_t *b, const uint32_t *oid, uint8_t n) {
  size_t pos = b128(b, oid[0] * 40 + oid[1]);
  for (uint8_t i = 2; i < n; i++) pos += b128(b + pos, oid[i]);
  return pos;
}

// content'i OID tipinde (0x06) sarmalar
static size_t encOID(uint8_t *b, const uint32_t *oid, uint8_t n) {
  uint8_t tmp[48];
  size_t cl = berOID(tmp, oid, n);
  size_t pos = 0; b[pos++] = 0x06; pos += berLen(b + pos, cl);
  memcpy(b + pos, tmp, cl); return pos + cl;
}

// imzalı INTEGER (Integer32) - minimal two's complement
static size_t encInt(uint8_t *b, int32_t v, uint8_t tag = 0x02) {
  uint8_t by[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
  int s = 0;
  while (s < 3 && ((by[s] == 0x00 && !(by[s + 1] & 0x80)) ||
                   (by[s] == 0xFF &&  (by[s + 1] & 0x80)))) s++;
  size_t len = 4 - s, pos = 0;
  b[pos++] = tag; b[pos++] = (uint8_t)len;
  for (int i = s; i < 4; i++) b[pos++] = by[i];
  return pos;
}

// imzasız (Counter32=0x41, Gauge32=0x42, TimeTicks=0x43)
static size_t encUInt(uint8_t *b, uint32_t v, uint8_t tag) {
  uint8_t tmp[5]; int n = 0;
  do { tmp[n++] = v & 0xFF; v >>= 8; } while (v);
  bool pad = (tmp[n - 1] & 0x80);
  size_t pos = 0; b[pos++] = tag; b[pos++] = (uint8_t)(n + (pad ? 1 : 0));
  if (pad) b[pos++] = 0x00;
  for (int i = n - 1; i >= 0; i--) b[pos++] = tmp[i];
  return pos;
}

static size_t encStr(uint8_t *b, const char *s) {
  size_t sl = strlen(s), pos = 0;
  b[pos++] = 0x04; pos += berLen(b + pos, sl);
  memcpy(b + pos, s, sl); return pos + sl;
}

static uint32_t uptimeCentis() { return (uint32_t)((millis() - bootMillis) / 10); }
static uint32_t uptimeSec()    { return (uint32_t)((millis() - bootMillis) / 1000); }

// Bir MIB id için DEĞER TLV'sini b'ye yazar, uzunluğu döndürür.
static size_t encValue(uint8_t *b, uint8_t id) {
  switch (id) {
    case MIB_SYS_DESCR:      return encStr(b, SYS_DESCR);
    case MIB_SYS_OBJECTID:   { const uint32_t o[] = {1,3,6,1,4,1,ENTERPRISE_PEN,1}; return encOID(b, o, 8); }
    case MIB_SYS_UPTIME:     return encUInt(b, uptimeCentis(), 0x43);  // TimeTicks
    case MIB_SYS_CONTACT:    return encStr(b, cfg.sysContact);
    case MIB_SYS_NAME:       return encStr(b, cfg.sysName);
    case MIB_SYS_LOCATION:   return encStr(b, cfg.sysLocation);
    case MIB_SYS_SERVICES:   return encInt(b, 72);
    case MIB_SENSOR_TYPE:    return encInt(b, 8);   // celsius(8)
    case MIB_SENSOR_SCALE:   return encInt(b, 9);   // units(9) = 10^0
    case MIB_SENSOR_PRECISION: return encInt(b, 1); // 1 ondalık -> value/10 = °C
    case MIB_SENSOR_VALUE:   return encInt(b, g_tempDeci);
    case MIB_SENSOR_STATUS:  return encInt(b, g_sensorOper);
    case MIB_TEMP_MILLI:     return encInt(b, g_tempMilli);
    case MIB_FREE_HEAP:      return encUInt(b, ESP.getFreeHeap(), 0x42);  // Gauge32
    case MIB_WIFI_RSSI:      return encInt(b, (int32_t)WiFi.RSSI());
    case MIB_UPTIME_SEC:     return encUInt(b, uptimeSec(), 0x42);
    case MIB_READ_COUNT:     return encUInt(b, g_readCount, 0x41);       // Counter32
    case MIB_ERR_COUNT:      return encUInt(b, g_errCount, 0x41);
    case MIB_SNMP_COUNT:     return encUInt(b, g_snmpCount, 0x41);
  }
  b[0] = 0x05; b[1] = 0x00; return 2; // NULL (olmamalı)
}

// --- BER çözücü ---
static bool readTLV(const uint8_t *&p, const uint8_t *end,
                    uint8_t &tag, const uint8_t *&content, size_t &clen) {
  if (p + 2 > end) return false;
  tag = *p++;
  uint8_t l = *p++;
  if (l & 0x80) {
    uint8_t nb = l & 0x7F;
    if (nb == 0 || nb > 4 || p + nb > end) return false;
    size_t len = 0; for (uint8_t i = 0; i < nb; i++) len = (len << 8) | *p++;
    clen = len;
  } else clen = l;
  if (p + clen > end) return false;
  content = p; p += clen; return true;
}

static uint8_t decodeOID(const uint8_t *d, size_t len, uint32_t *oid, uint8_t maxn) {
  if (len == 0 || maxn < 2) return 0;
  size_t i = 0; uint32_t v = 0;
  while (i < len) { v = (v << 7) | (d[i] & 0x7F); if (!(d[i] & 0x80)) { i++; break; } i++; }
  if (v < 40) { oid[0] = 0; oid[1] = v; }
  else if (v < 80) { oid[0] = 1; oid[1] = v - 40; }
  else { oid[0] = 2; oid[1] = v - 80; }
  uint8_t n = 2;
  while (i < len && n < maxn) {
    v = 0;
    while (i < len) { v = (v << 7) | (d[i] & 0x7F); if (!(d[i] & 0x80)) { i++; break; } i++; }
    oid[n++] = v;
  }
  return n;
}

static int cmpOID(const uint32_t *a, uint8_t al, const uint32_t *b, uint8_t bl) {
  uint8_t m = al < bl ? al : bl;
  for (uint8_t i = 0; i < m; i++) { if (a[i] < b[i]) return -1; if (a[i] > b[i]) return 1; }
  if (al < bl) return -1; if (al > bl) return 1; return 0;
}

static int mibFindExact(const uint32_t *o, uint8_t n) {
  for (int i = 0; i < MIB_COUNT; i++)
    if (cmpOID(MIB[i].oid, MIB[i].oidLen, o, n) == 0) return i;
  return -1;
}
static int mibFindNext(const uint32_t *o, uint8_t n) {
  for (int i = 0; i < MIB_COUNT; i++)
    if (cmpOID(MIB[i].oid, MIB[i].oidLen, o, n) > 0) return i;  // tablo sıralı -> ilk büyük olan
  return -1;
}

// Gelen SNMP paketini işler; yanıt uzunluğunu döndürür (0 = yanıt yok).
static size_t snmpProcess(const uint8_t *in, size_t inLen, uint8_t *out) {
  const uint8_t *p = in, *end = in + inLen;
  uint8_t tag; const uint8_t *c; size_t cl;

  if (!readTLV(p, end, tag, c, cl) || tag != 0x30) return 0;     // message SEQUENCE
  const uint8_t *mp = c, *mend = c + cl;

  if (!readTLV(mp, mend, tag, c, cl) || tag != 0x02) return 0;   // version
  uint8_t version = (cl >= 1) ? c[cl - 1] : 1;                   // 0=v1, 1=v2c

  if (!readTLV(mp, mend, tag, c, cl) || tag != 0x04) return 0;   // community
  if (cl != strlen(cfg.roComm) || memcmp(c, cfg.roComm, cl) != 0) return 0; // eşleşmezse sessiz düş

  if (!readTLV(mp, mend, tag, c, cl)) return 0;                  // PDU
  uint8_t pduType = tag;
  bool isGet  = (pduType == 0xA0);
  bool isNext = (pduType == 0xA1 || pduType == 0xA5);            // GETBULK -> tek tekrar
  if (!isGet && !isNext) return 0;                               // SET vb. desteklenmez

  const uint8_t *pp = c, *pend = c + cl;

  const uint8_t *ridStart = pp;                                  // request-id (ham, echo için)
  if (!readTLV(pp, pend, tag, c, cl) || tag != 0x02) return 0;
  size_t ridRawLen = (size_t)(c - ridStart) + cl;
  if (ridRawLen == 0 || ridRawLen > 8) return 0;
  uint8_t ridRaw[8]; memcpy(ridRaw, ridStart, ridRawLen);

  if (!readTLV(pp, pend, tag, c, cl)) return 0;                  // error-status / non-repeaters
  if (!readTLV(pp, pend, tag, c, cl)) return 0;                  // error-index  / max-repetitions
  if (!readTLV(pp, pend, tag, c, cl) || tag != 0x30) return 0;   // varbind-list

  const uint8_t *vp = c, *vend = c + cl;
  size_t vbLen = 0;

  while (vp < vend) {
    const uint8_t *vc; size_t vcl;
    if (!readTLV(vp, vend, tag, vc, vcl) || tag != 0x30) break;  // varbind SEQUENCE
    const uint8_t *ip = vc, *iend = vc + vcl;
    const uint8_t *oc; size_t ocl; uint8_t t2;
    if (!readTLV(ip, iend, t2, oc, ocl) || t2 != 0x06) break;    // OID (değer TLV'sini okumaya gerek yok)

    uint32_t reqOid[24];
    uint8_t reqLen = decodeOID(oc, ocl, reqOid, 24);
    if (reqLen == 0) break;

    const uint32_t *rOid; uint8_t rLen;
    static uint8_t valBuf[300]; size_t valLen;

    int idx = isGet ? mibFindExact(reqOid, reqLen) : mibFindNext(reqOid, reqLen);
    if (idx >= 0) {
      rOid = MIB[idx].oid; rLen = MIB[idx].oidLen;
      valLen = encValue(valBuf, MIB[idx].id);
    } else {
      rOid = reqOid; rLen = reqLen;
      valBuf[0] = isGet ? 0x81 : 0x82;  // noSuchInstance / endOfMibView
      valBuf[1] = 0x00; valLen = 2;
    }

    static uint8_t inner[360]; size_t innerLen = 0;
    innerLen += encOID(inner + innerLen, rOid, rLen);
    memcpy(inner + innerLen, valBuf, valLen); innerLen += valLen;

    if (vbLen + 4 + innerLen > sizeof(S_VB)) break;               // taşma koruması
    S_VB[vbLen++] = 0x30; vbLen += berLen(S_VB + vbLen, innerLen);
    memcpy(S_VB + vbLen, inner, innerLen); vbLen += innerLen;
  }

  // ---- GET-RESPONSE PDU (0xA2) ----
  size_t pduLen = 0;
  memcpy(S_PDU + pduLen, ridRaw, ridRawLen); pduLen += ridRawLen;
  S_PDU[pduLen++] = 0x02; S_PDU[pduLen++] = 0x01; S_PDU[pduLen++] = 0x00; // error-status = 0
  S_PDU[pduLen++] = 0x02; S_PDU[pduLen++] = 0x01; S_PDU[pduLen++] = 0x00; // error-index  = 0
  S_PDU[pduLen++] = 0x30; pduLen += berLen(S_PDU + pduLen, vbLen);
  memcpy(S_PDU + pduLen, S_VB, vbLen); pduLen += vbLen;

  // ---- Message ----
  size_t msgLen = 0;
  S_MSG[msgLen++] = 0x02; S_MSG[msgLen++] = 0x01; S_MSG[msgLen++] = version;
  size_t commLen = strlen(cfg.roComm);
  S_MSG[msgLen++] = 0x04; msgLen += berLen(S_MSG + msgLen, commLen);
  memcpy(S_MSG + msgLen, cfg.roComm, commLen); msgLen += commLen;
  S_MSG[msgLen++] = 0xA2; msgLen += berLen(S_MSG + msgLen, pduLen);
  memcpy(S_MSG + msgLen, S_PDU, pduLen); msgLen += pduLen;

  size_t outLen = 0;
  out[outLen++] = 0x30; outLen += berLen(out + outLen, msgLen);
  memcpy(out + outLen, S_MSG, msgLen); outLen += msgLen;
  return outLen;
}

static void snmpHandle() {
  int packetSize = udp.parsePacket();
  if (packetSize <= 0) return;
  static uint8_t in[1024];
  int len = udp.read(in, sizeof(in));
  if (len <= 0) return;
  if (!ipAllowed(udp.remoteIP())) return;        // ACL: izinsiz kaynak IP -> paketi işleme

  static uint8_t out[768];
  size_t outLen = snmpProcess(in, (size_t)len, out);
  if (outLen == 0) return;

  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write(out, outLen);
  udp.endPacket();
  g_snmpCount++;
}

/* ============================== WIFI CONFIG PORTALI =============================== */

static String htmlEscape(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (char ch : s) {
    if (ch == '&') o += "&amp;"; else if (ch == '<') o += "&lt;";
    else if (ch == '>') o += "&gt;"; else if (ch == '"') o += "&quot;";
    else o += ch;
  }
  return o;
}

static String jsonEscape(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char ch = s[i];
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if (ch == '\n') o += "\\n";
    else if (ch == '\r') o += "\\r";
    else if (ch == '\t') o += "\\t";
    else if ((uint8_t)ch < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", (uint8_t)ch); o += b; }
    else o += ch;
  }
  return o;
}

static String buildPortalPage() {
  // Ağ taraması artık sunucuda DEĞİL: sayfa anında açılır, ağlar /scan'den async gelir.
  String p; p.reserve(7200);
  p += F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>netio.probe · kurulum</title><style>"
         ":root{--ink:#e6edf6;--dim:#7f8ea8;--accent:#2ee6c4;--line:rgba(127,142,168,.16);"
         "--mono:ui-monospace,'SF Mono',SFMono-Regular,Menlo,Consolas,monospace}"
         "*{box-sizing:border-box}html,body{margin:0;min-height:100%}"
         "body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;color:var(--ink);"
         "display:flex;align-items:center;justify-content:center;padding:20px;position:relative;overflow-x:hidden;"
         "background:#06080f radial-gradient(820px 460px at 50% -8%,rgba(46,230,196,.12),transparent 62%) no-repeat}"
         "body::before{content:'';position:fixed;inset:0;pointer-events:none;z-index:0;"
         "background:linear-gradient(rgba(127,142,168,.05) 1px,transparent 1px) 0 0/100% 30px,"
         "linear-gradient(90deg,rgba(127,142,168,.05) 1px,transparent 1px) 0 0/30px 100%;"
         "-webkit-mask:radial-gradient(680px 420px at 50% 8%,#000,transparent 78%);"
         "mask:radial-gradient(680px 420px at 50% 8%,#000,transparent 78%)}"
         ".card{position:relative;z-index:1;width:100%;max-width:440px;background:rgba(15,21,35,.72);"
         "-webkit-backdrop-filter:blur(14px);backdrop-filter:blur(14px);border:1px solid var(--line);"
         "border-radius:18px;padding:24px 22px 22px;"
         "box-shadow:0 30px 80px -30px rgba(0,0,0,.85),inset 0 0 0 1px rgba(46,230,196,.04)}"
         "@keyframes rise{from{opacity:0;transform:translateY(9px)}to{opacity:1;transform:none}}"
         ".card>*{animation:rise .5s both}"
         ".card>*:nth-child(2){animation-delay:.04s}.card>*:nth-child(3){animation-delay:.08s}"
         ".card>*:nth-child(4){animation-delay:.12s}.card>*:nth-child(5){animation-delay:.16s}"
         ".hd{display:flex;align-items:center;gap:10px;margin:0 0 4px}"
         ".dot{width:9px;height:9px;border-radius:50%;background:var(--accent);animation:pulse 2.2s infinite}"
         "@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(46,230,196,.55)}70%{box-shadow:0 0 0 9px rgba(46,230,196,0)}100%{box-shadow:0 0 0 0 rgba(46,230,196,0)}}"
         ".wm{font:700 19px/1 var(--mono);letter-spacing:.3px}.wm b{color:var(--accent)}"
         ".tag{font:600 10px/1 var(--mono);letter-spacing:2.4px;text-transform:uppercase;color:var(--dim);margin:0 0 18px}"
         ".sec{display:flex;align-items:center;justify-content:space-between;margin:0 0 8px}"
         ".lbl{display:block;font:600 10.5px/1 var(--mono);letter-spacing:1.4px;text-transform:uppercase;color:var(--dim);margin:14px 0 6px}"
         ".sec .lbl{margin:0}"
         ".resc{background:transparent;border:1px solid var(--line);color:var(--dim);border-radius:9px;"
         "padding:6px 10px;font:600 11px/1 var(--mono);letter-spacing:.5px;cursor:pointer;transition:.15s}"
         ".resc:hover{color:var(--ink);border-color:var(--accent)}"
         ".nets{border:1px solid var(--line);border-radius:12px;overflow:hidden auto;max-height:212px;background:rgba(8,12,22,.5)}"
         ".net{display:flex;width:100%;align-items:center;justify-content:space-between;gap:10px;padding:11px 13px;"
         "background:transparent;border:0;border-bottom:1px solid var(--line);color:var(--ink);"
         "font:13px/1 var(--mono);cursor:pointer;text-align:left;transition:background .12s}"
         ".net:last-child{border-bottom:0}.net:hover{background:rgba(46,230,196,.06)}"
         ".net.sel{background:rgba(46,230,196,.10);box-shadow:inset 2px 0 0 var(--accent)}"
         ".nm{display:flex;align-items:center;gap:8px;min-width:0}"
         ".nm .t{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
         ".lk{color:var(--dim);flex:0 0 auto}"
         ".rt{display:flex;align-items:center;gap:10px;flex:0 0 auto}"
         ".rt em{font-style:normal;color:var(--dim);font-size:11px;min-width:56px;text-align:right}"
         ".sig{display:inline-flex;align-items:flex-end;gap:2px;height:14px}"
         ".sig i{width:3px;border-radius:1px;background:var(--dim)}"
         ".sig i.on{background:var(--accent);box-shadow:0 0 6px rgba(46,230,196,.5)}"
         ".scan,.empty{padding:16px 13px;color:var(--dim);font:13px var(--mono);display:flex;align-items:center;gap:10px}"
         ".sp{width:13px;height:13px;border:2px solid var(--line);border-top-color:var(--accent);border-radius:50%;animation:spin .8s linear infinite;flex:0 0 auto}"
         "@keyframes spin{to{transform:rotate(360deg)}}"
         ".f{width:100%;padding:11px 12px;border-radius:10px;border:1px solid var(--line);background:rgba(8,12,22,.7);"
         "color:var(--ink);font:14px var(--mono);outline:none;transition:border-color .15s,box-shadow .15s}"
         ".f:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(46,230,196,.16)}"
         ".pw{position:relative}.pw .f{padding-right:64px}"
         ".pw button{position:absolute;right:5px;top:50%;transform:translateY(-50%);background:transparent;border:0;"
         "color:var(--dim);font:600 11px var(--mono);letter-spacing:.5px;cursor:pointer;padding:7px 9px}"
         ".pw button:hover{color:var(--accent)}"
         "details{margin-top:16px;border-top:1px solid var(--line);padding-top:12px}"
         "summary{cursor:pointer;color:var(--accent);font:600 11px var(--mono);letter-spacing:1.2px;text-transform:uppercase;list-style:none}"
         "summary::-webkit-details-marker{display:none}summary::before{content:'+ ';color:var(--dim)}"
         "details[open] summary::before{content:'– '}"
         ".go{width:100%;margin-top:18px;padding:13px 16px;border:0;border-radius:11px;cursor:pointer;"
         "font:700 15px/1 system-ui;letter-spacing:.3px;color:#04140f;background:linear-gradient(180deg,#3df0d0,#19c7a8);"
         "box-shadow:0 10px 26px -10px rgba(46,230,196,.65),inset 0 1px 0 rgba(255,255,255,.4);transition:transform .15s,box-shadow .15s}"
         ".go:hover{transform:translateY(-1px);box-shadow:0 14px 32px -10px rgba(46,230,196,.75)}.go:active{transform:none}"
         ".hint{color:var(--dim);font:11px var(--mono);letter-spacing:.3px;text-align:center;margin:10px 0 0}"
         "@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}"
         "</style></head><body><main class='card'>"
         "<div class='hd'><span class='dot'></span><span class='wm'>netio<b>.probe</b></span></div>"
         "<p class='tag'>SNMP PROBE · WiFi SETUP</p>"
         "<form method='POST' action='/save'>"
         "<div class='sec'><span class='lbl'>Ağlar</span>"
         "<button type='button' class='resc' onclick='scan()'>↻ tekrar tara</button></div>"
         "<div class='nets' id='nets'></div>"
         "<noscript><p class='hint'>JavaScript kapalı — SSID'yi elle yazın.</p></noscript>"
         "<label class='lbl'>WiFi adı (SSID)</label>"
         "<input class='f' name='ssid' id='ssid' autocomplete='off' autocapitalize='off' required>"
         "<label class='lbl'>Şifre</label><div class='pw'>"
         "<input class='f' id='pass' name='pass' type='password' autocomplete='off'>"
         "<button type='button' onclick='pw(this)'>göster</button></div>"
         "<details><summary>Gelişmiş · SNMP</summary>");
  p += "<label class='lbl'>Read community</label><input class='f' name='ro' value='" + htmlEscape(cfg.roComm) + "'>";
  p += "<label class='lbl'>sysName</label><input class='f' name='sn' value='" + htmlEscape(cfg.sysName) + "'>";
  p += "<label class='lbl'>sysLocation</label><input class='f' name='sl' value='" + htmlEscape(cfg.sysLocation) + "'>";
  p += "<label class='lbl'>sysContact</label><input class='f' name='sc' value='" + htmlEscape(cfg.sysContact) + "'>";
  p += "<label class='lbl'>OTA şifresi</label><input class='f' name='ota' value='" + htmlEscape(cfg.otaPass) + "'>";
  p += "<label class='lbl'>Yönetici kullanıcı</label><input class='f' name='au' value='" + htmlEscape(cfg.adminUser) + "'>";
  p += F("<label class='lbl'>Yönetici şifresi (boş = değişmez)</label>"
         "<input class='f' name='ap' type='password' autocomplete='off'>");
  p += F("</details>"
         "<button class='go' type='submit'>Bağlan →</button>"
         "<p class='hint'>Kaydedince cihaz yeniden başlar ve ağa bağlanır.</p>"
         "</form></main><script>"
         "var nets=document.getElementById('nets'),ssid=document.getElementById('ssid');"
         "var LK=`<svg class='lk' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round'><rect x='5' y='11' width='14' height='9' rx='2'/><path d='M8 11V8a4 4 0 0 1 8 0v3'/></svg>`;"
         "function bars(q){var h=[5,8,11,14],s='';for(var i=0;i<4;i++){s+=`<i class='${i<q?'on':''}' style='height:${h[i]}px'></i>`;}return `<span class='sig'>${s}</span>`;}"
         "function render(l){nets.innerHTML='';if(!l.length){nets.innerHTML=`<div class='empty'>Ağ bulunamadı — tekrar tara</div>`;return;}"
         "l.sort(function(a,b){return b.r-a.r;});"
         "l.forEach(function(w){var nm=w.s||'(gizli ağ)';var b=document.createElement('button');b.type='button';b.className='net';"
         "b.innerHTML=`<span class='nm'>${w.o?'':LK}<span class='t'></span></span><span class='rt'>${bars(w.q)}<em></em></span>`;"
         "b.querySelector('.t').textContent=nm;b.querySelector('em').textContent=w.r+' dBm';"
         "b.onclick=function(){ssid.value=nm;Array.prototype.forEach.call(nets.children,function(x){x.classList&&x.classList.remove('sel');});b.classList.add('sel');};"
         "nets.appendChild(b);});}"
         "var busy=false;function scan(){if(busy)return;busy=true;"
         "nets.innerHTML=`<div class='scan'><span class='sp'></span>Ağlar taranıyor…</div>`;"
         "fetch('/scan').then(function(r){return r.json();}).then(render).catch(function(){nets.innerHTML=`<div class='empty'>Tarama hatası — tekrar dene</div>`;}).then(function(){busy=false;});}"
         "function pw(b){var i=document.getElementById('pass');var s=i.type==='password';i.type=s?'text':'password';b.textContent=s?'gizle':'göster';}"
         "scan();"
         "</script></body></html>");
  return p;
}

// /scan -> bulunan WiFi ağlarını kompakt JSON olarak döndürür (sayfa açıkken async çekilir)
static void handleScan() {
  int n = WiFi.scanNetworks();
  String j; j.reserve(64 + (n > 0 ? n : 0) * 48);
  j = "[";
  int added = 0;
  for (int i = 0; i < n && added < 24; i++) {
    int r = WiFi.RSSI(i);
    int q = (r >= -55) ? 4 : (r >= -67) ? 3 : (r >= -78) ? 2 : 1;
    bool open = (WiFi.encryptionType(i) == ENC_TYPE_NONE);
    if (added) j += ',';
    j += "{\"s\":\"";
    j += jsonEscape(WiFi.SSID(i));
    j += "\",\"r\":";  j += String(r);
    j += ",\"q\":";    j += String(q);
    j += ",\"o\":";    j += (open ? "1" : "0");
    j += "}";
    added++;
  }
  j += "]";
  WiFi.scanDelete();
  server.send(200, "application/json", j);
}

static void handleRoot() { server.send(200, "text/html", buildPortalPage()); }

static void handleSave() {
  if (server.hasArg("ssid")) copyStr(cfg.ssid, server.arg("ssid").c_str(), sizeof(cfg.ssid));
  if (server.hasArg("pass")) copyStr(cfg.pass, server.arg("pass").c_str(), sizeof(cfg.pass));
  if (server.hasArg("ro") && server.arg("ro").length()) copyStr(cfg.roComm, server.arg("ro").c_str(), sizeof(cfg.roComm));
  if (server.hasArg("sn") && server.arg("sn").length()) copyStr(cfg.sysName, server.arg("sn").c_str(), sizeof(cfg.sysName));
  if (server.hasArg("sl")) copyStr(cfg.sysLocation, server.arg("sl").c_str(), sizeof(cfg.sysLocation));
  if (server.hasArg("sc")) copyStr(cfg.sysContact, server.arg("sc").c_str(), sizeof(cfg.sysContact));
  if (server.hasArg("ota") && server.arg("ota").length()) copyStr(cfg.otaPass, server.arg("ota").c_str(), sizeof(cfg.otaPass));
  if (server.hasArg("au") && server.arg("au").length()) copyStr(cfg.adminUser, server.arg("au").c_str(), sizeof(cfg.adminUser));
  if (server.hasArg("ap") && server.arg("ap").length()) setAdminPassword(server.arg("ap").c_str());

  cfg.configured = (strlen(cfg.ssid) > 0) ? 1 : 0;
  saveConfig();

  String body = F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'>"
                  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>netio.probe</title><style>"
                  ":root{--accent:#2ee6c4;--dim:#7f8ea8;--mono:ui-monospace,'SF Mono',Menlo,Consolas,monospace}"
                  "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
                  "font-family:system-ui,-apple-system,'Segoe UI',sans-serif;color:#e6edf6;text-align:center;padding:24px;"
                  "background:#06080f radial-gradient(720px 420px at 50% 0%,rgba(46,230,196,.12),transparent 60%) no-repeat}"
                  ".w{max-width:380px}"
                  ".ring{width:64px;height:64px;margin:0 auto 22px;border-radius:50%;"
                  "border:3px solid rgba(127,142,168,.18);border-top-color:var(--accent);"
                  "animation:s 1s linear infinite;box-shadow:0 0 26px -4px rgba(46,230,196,.5)}"
                  "@keyframes s{to{transform:rotate(360deg)}}"
                  "h2{font:700 18px var(--mono);letter-spacing:.3px;margin:0 0 10px}h2 b{color:var(--accent)}"
                  "p{color:var(--dim);font:13px/1.5 var(--mono);margin:6px 0}"
                  ".host{display:inline-block;margin-top:12px;padding:8px 13px;border:1px solid rgba(127,142,168,.18);"
                  "border-radius:9px;color:var(--accent);font:13px var(--mono)}"
                  "@media(prefers-reduced-motion:reduce){.ring{animation:none}}"
                  "</style></head><body><div class='w'><div class='ring'></div>"
                  "<h2>netio<b>.probe</b> kaydedildi</h2>"
                  "<p>Cihaz yeniden başlatılıyor ve ağa bağlanıyor…</p>"
                  "<p>Bağlandıktan sonra erişim adresi:</p><span class='host'>");
  body += hostName(); body += F(".local</span></div></body></html>");
  server.send(200, "text/html", body);

  delay(1500);
  ESP.restart();
}

static void handleNotFound() {  // captive portal: her isteği köke yönlendir
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

static void startPortal() {
  g_mode = MODE_PORTAL;
  Serial.println(F("\n=== netio.probe - WiFi Yapilandirma Portali (AP) ==="));
  WiFi.mode(WIFI_AP_STA);                   // STA: tarama yapabilmek için
  String ap = apSsid();
  WiFi.softAP(ap.c_str(), AP_PASSWORD);
  delay(200);
  IPAddress ip = WiFi.softAPIP();
  Serial.println("AP SSID : " + ap);
  Serial.println("AP Sifre: " + String(AP_PASSWORD));
  Serial.println("Portal  : http://" + ip.toString() + "/");

  dnsServer.start(53, "*", ip);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();

  pinMode(LED_BUILTIN, OUTPUT);
}

/* =============================== STA / OTA / mDNS ================================ */

static bool connectSTA(uint32_t timeoutMs) {
  Serial.print("[WiFi] Baglaniliyor: "); Serial.println(cfg.ssid);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostName());
  WiFi.begin(cfg.ssid, cfg.pass);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(250); Serial.print('.'); yield();
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("[WiFi] Bagli. IP : " + WiFi.localIP().toString());
    Serial.println("[WiFi] mDNS    : " + hostName() + ".local");
    Serial.println("[WiFi] RSSI    : " + String(WiFi.RSSI()) + " dBm");
    return true;
  }
  Serial.println(F("\n[WiFi] Baglanti basarisiz."));
  return false;
}

static void setupOTA() {
  ArduinoOTA.setHostname(hostName().c_str());
  ArduinoOTA.setPassword(cfg.otaPass);
  ArduinoOTA.onStart([]() { Serial.println(F("[OTA] Basliyor...")); });
  ArduinoOTA.onEnd([]()   { Serial.println(F("\n[OTA] Tamamlandi.")); });
  ArduinoOTA.onProgress([](unsigned int pr, unsigned int t) {
    Serial.printf("[OTA] %u%%\r", (t ? (pr * 100 / t) : 0));
  });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("[OTA] Hata[%u]\n", e); });
  ArduinoOTA.begin();                        // ESP8266'da mDNS'i de baslatir
  MDNS.addService("snmp", "udp", SNMP_PORT);
}

static void startRunServices();

/* ============================ ÇALIŞMA MODU WEB YÖNETİMİ ========================== */
/*
 * STA moduna geçtikten sonra http://<ip>/ adresinde HTTP Basic Auth korumalı yönetim
 * arayüzü çalışır: SNMP community / sysName / sysLocation / sysContact / OTA ve yönetici
 * bilgileri değiştirilebilir; .bin yüklenerek firmware güncellenebilir (web OTA);
 * fabrika ayarına dönülebilir veya cihaz yeniden başlatılabilir.
 *   NOT: HTTP Basic Auth düz metindir (HTTPS değil) -> güvenilir ağda kullanın.
 */

// Tüm yönetim sayfalarının paylaştığı tema (flash'ta saklanır)
static const char STYLE_BASE[] PROGMEM =
  ":root{--ink:#e6edf6;--dim:#7f8ea8;--accent:#2ee6c4;--danger:#f0716a;--line:rgba(127,142,168,.16);"
  "--mono:ui-monospace,'SF Mono',SFMono-Regular,Menlo,Consolas,monospace}"
  "*{box-sizing:border-box}html,body{margin:0;min-height:100%}"
  "body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;color:var(--ink);"
  "display:flex;align-items:flex-start;justify-content:center;padding:20px;position:relative;overflow-x:hidden;"
  "background:#06080f radial-gradient(820px 460px at 50% -8%,rgba(46,230,196,.12),transparent 62%) no-repeat}"
  "body::before{content:'';position:fixed;inset:0;pointer-events:none;z-index:0;"
  "background:linear-gradient(rgba(127,142,168,.05) 1px,transparent 1px) 0 0/100% 30px,"
  "linear-gradient(90deg,rgba(127,142,168,.05) 1px,transparent 1px) 0 0/30px 100%;"
  "-webkit-mask:radial-gradient(680px 420px at 50% 8%,#000,transparent 78%);mask:radial-gradient(680px 420px at 50% 8%,#000,transparent 78%)}"
  ".card{position:relative;z-index:1;width:100%;max-width:460px;background:rgba(15,21,35,.72);"
  "-webkit-backdrop-filter:blur(14px);backdrop-filter:blur(14px);border:1px solid var(--line);"
  "border-radius:18px;padding:24px 22px 22px;box-shadow:0 30px 80px -30px rgba(0,0,0,.85),inset 0 0 0 1px rgba(46,230,196,.04)}"
  ".hd{display:flex;align-items:center;gap:10px;margin:0 0 4px}"
  ".dot{width:9px;height:9px;border-radius:50%;background:var(--accent);animation:pulse 2.2s infinite}"
  "@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(46,230,196,.55)}70%{box-shadow:0 0 0 9px rgba(46,230,196,0)}100%{box-shadow:0 0 0 0 rgba(46,230,196,0)}}"
  ".wm{font:700 19px/1 var(--mono);letter-spacing:.3px}.wm b{color:var(--accent)}"
  ".tag{font:600 10px/1 var(--mono);letter-spacing:2.4px;text-transform:uppercase;color:var(--dim);margin:0 0 18px}"
  ".msg{color:var(--ink);font:14px/1.5 var(--mono);margin:4px 0}"
  ".lbl{display:block;font:600 10.5px/1 var(--mono);letter-spacing:1.4px;text-transform:uppercase;color:var(--dim);margin:14px 0 6px}"
  ".f{width:100%;padding:11px 12px;border-radius:10px;border:1px solid var(--line);background:rgba(8,12,22,.7);"
  "color:var(--ink);font:14px var(--mono);outline:none;transition:border-color .15s,box-shadow .15s}"
  ".f:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(46,230,196,.16)}"
  ".pw{position:relative}.pw .f{padding-right:64px}"
  ".pw button{position:absolute;right:5px;top:50%;transform:translateY(-50%);background:transparent;border:0;"
  "color:var(--dim);font:600 11px var(--mono);letter-spacing:.5px;cursor:pointer;padding:7px 9px}.pw button:hover{color:var(--accent)}"
  "details{margin-top:16px;border-top:1px solid var(--line);padding-top:12px}"
  "summary{cursor:pointer;color:var(--accent);font:600 11px var(--mono);letter-spacing:1.2px;text-transform:uppercase;list-style:none}"
  "summary::-webkit-details-marker{display:none}summary::before{content:'+ ';color:var(--dim)}details[open] summary::before{content:'– '}"
  ".go{width:100%;margin-top:18px;padding:13px 16px;border:0;border-radius:11px;cursor:pointer;font:700 15px/1 system-ui;letter-spacing:.3px;"
  "color:#04140f;background:linear-gradient(180deg,#3df0d0,#19c7a8);box-shadow:0 10px 26px -10px rgba(46,230,196,.65),inset 0 1px 0 rgba(255,255,255,.4);"
  "transition:transform .15s}.go:hover{transform:translateY(-1px)}.go:active{transform:none}"
  ".hint{color:var(--dim);font:11px var(--mono);letter-spacing:.3px;text-align:center;margin:10px 0 0}"
  "a{color:var(--accent);text-decoration:none}"
  "@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}";

// HTTP Basic Auth doğrulaması; başarısızsa 401 döndürüp false döner
// Authorization: Basic ... başlığını çözüp kullanıcı+hash ile doğrula
static bool authClientOK() {
  if (!server.hasHeader("Authorization")) return false;
  String h = server.header("Authorization");
  if (!h.startsWith("Basic ")) return false;
  String creds = b64Decode(h.substring(6));     // "kullanıcı:parola"
  int c = creds.indexOf(':');
  if (c < 0) return false;
  String u = creds.substring(0, c);
  String p = creds.substring(c + 1);
  return u == String(cfg.adminUser) && checkAdminPassword(p.c_str());
}

static bool requireAuth() {
  IPAddress ip = server.client().remoteIP();
  if (!ipAllowed(ip)) {
    syslogf(SLOG_WARNING, "web ACL deny src=%s", ip.toString().c_str());
    server.send(403, "text/plain", "403 - kaynak IP izinli degil (ACL)");
    return false;
  }
  if (authLockedOut(ip)) {
    server.send(429, "text/plain", "429 - cok fazla hatali giris, biraz bekleyin");
    return false;
  }
  if (!authClientOK()) {
    authNoteFail(ip);
    syslogf(SLOG_WARNING, "web auth fail src=%s user=%s", ip.toString().c_str(), cfg.adminUser);
    server.requestAuthentication();
    return false;
  }
  authNoteOK(ip);
  return true;
}

static String buildAdminPage() {
  String p; p.reserve(8000);
  p += F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>netio.probe · yönetim</title><style>");
  p += FPSTR(STYLE_BASE);
  p += F(".pill{margin-left:auto;font:600 10px var(--mono);letter-spacing:1.5px;text-transform:uppercase;color:var(--accent);"
         "border:1px solid rgba(46,230,196,.35);border-radius:999px;padding:4px 10px}"
         ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:4px 0 6px}"
         ".m{background:rgba(8,12,22,.55);border:1px solid var(--line);border-radius:11px;padding:11px 12px}"
         ".m label{display:block;font:600 9.5px var(--mono);letter-spacing:1.2px;text-transform:uppercase;color:var(--dim)}"
         ".m b{display:block;font:600 19px/1.2 var(--mono);margin-top:5px}"
         ".act{display:flex;gap:10px;margin-top:16px}.act form{flex:1;margin:0}"
         ".btn{width:100%;padding:11px;border-radius:10px;cursor:pointer;font:600 13px var(--mono);letter-spacing:.4px;"
         "background:transparent;border:1px solid var(--line);color:var(--ink);transition:.15s}"
         ".btn:hover{border-color:var(--accent);color:var(--accent)}"
         ".btn.dng:hover{border-color:var(--danger);color:var(--danger)}"
         ".upl{display:block;text-align:center;margin-top:12px;padding:11px;border-radius:10px;border:1px dashed var(--line);color:var(--accent)}"
         "</style></head><body><main class='card'>"
         "<div class='hd'><span class='dot'></span><span class='wm'>netio<b>.probe</b></span><span class='pill'>online</span></div>"
         "<p class='tag'>SNMP PROBE · YÖNETİM</p>"
         "<div class='grid'>"
         "<div class='m'><label>Sıcaklık</label><b id='temp'>—</b></div>"
         "<div class='m'><label>RSSI</label><b id='rssi'>—</b></div>"
         "<div class='m'><label>Uptime</label><b id='up'>—</b></div>"
         "<div class='m'><label>Boş Heap</label><b id='heap'>—</b></div>"
         "<div class='m'><label>SNMP istek</label><b id='sn'>—</b></div>"
         "<div class='m'><label>Okuma / hata</label><b id='rd'>—</b></div>"
         "</div>"
         "<form method='POST' action='/save'>"
         "<label class='lbl'>SNMP read community</label><input class='f' name='ro' value='");
  p += htmlEscape(cfg.roComm);
  p += F("'><label class='lbl'>sysName</label><input class='f' name='sn' value='");
  p += htmlEscape(cfg.sysName);
  p += F("'><label class='lbl'>sysLocation</label><input class='f' name='sl' value='");
  p += htmlEscape(cfg.sysLocation);
  p += F("'><label class='lbl'>sysContact</label><input class='f' name='sc' value='");
  p += htmlEscape(cfg.sysContact);
  p += F("'><details><summary>Güvenlik & WiFi</summary>"
         "<label class='lbl'>Yönetici kullanıcı</label><input class='f' name='au' value='");
  p += htmlEscape(cfg.adminUser);
  p += F("'><label class='lbl'>Yönetici şifresi (boş = değişmez)</label><div class='pw'>"
         "<input class='f' id='ap' name='ap' type='password' autocomplete='off'>"
         "<button type='button' onclick='pw(this,\"ap\")'>göster</button></div>"
         "<label class='lbl'>OTA (espota) şifresi</label><input class='f' name='ota' value='");
  p += htmlEscape(cfg.otaPass);
  p += F("'><label class='lbl'>WiFi SSID (değişirse cihaz yeniden başlar)</label><input class='f' name='ssid' value='");
  p += htmlEscape(cfg.ssid);
  p += F("'><label class='lbl'>WiFi şifresi (boş = değişmez)</label><div class='pw'>"
         "<input class='f' id='wp' name='pass' type='password' autocomplete='off'>"
         "<button type='button' onclick='pw(this,\"wp\")'>göster</button></div>"
         "<label class='lbl'>Erişim listesi (CIDR · 0.0.0.0/0 = herkes)</label><input class='f' name='acl' value='");
  p += htmlEscape(cidrStr());
  p += F("'><label class='lbl'>Syslog sunucu IP (boş = kapalı)</label><input class='f' name='slog' value='");
  p += htmlEscape(String(cfg.syslogIp));
  p += F("'><label class='lbl'>Syslog UDP port</label><input class='f' name='slogp' value='");
  p += String(cfg.syslogPort);
  p += F("'></details>"
         "<button class='go' type='submit'>Ayarları kaydet</button></form>"
         "<a class='upl' href='/update'>⤓ Firmware güncelle (.bin yükle)</a>"
         "<div class='act'>"
         "<form method='POST' action='/reboot' onsubmit='return confirm(\"Cihaz yeniden başlatılsın mı?\")'>"
         "<button class='btn' type='submit'>Yeniden başlat</button></form>"
         "<form method='POST' action='/reset' onsubmit='return confirm(\"TÜM ayarlar silinip fabrika ayarına dönülecek. Emin misiniz?\")'>"
         "<button class='btn dng' type='submit'>Fabrika ayarları</button></form>"
         "</div>"
         "<p class='hint'>http basic auth · snmp v2c düz metindir, güvenilir ağda kullanın</p>"
         "</main><script>"
         "function pw(b,id){var i=document.getElementById(id);var s=i.type==='password';i.type=s?'text':'password';b.textContent=s?'gizle':'göster';}"
         "function fmtUp(s){var d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'g ':'')+(h?h+'s ':'')+m+'d';}"
         "function tick(){fetch('/status').then(function(r){return r.json();}).then(function(j){"
         "document.getElementById('temp').textContent=(j.oper===1?(j.tempd/10).toFixed(1)+' °C':'yok');"
         "document.getElementById('rssi').textContent=j.rssi+' dBm';"
         "document.getElementById('up').textContent=fmtUp(j.up);"
         "document.getElementById('heap').textContent=(j.heap/1024).toFixed(1)+' KB';"
         "document.getElementById('sn').textContent=j.sn;"
         "document.getElementById('rd').textContent=j.rd+' / '+j.er;"
         "}).catch(function(){});}"
         "tick();setInterval(tick,4000);"
         "</script></body></html>");
  return p;
}

static String buildUpdatePage() {
  String p; p.reserve(5000);
  p += F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>netio.probe · firmware</title><style>");
  p += FPSTR(STYLE_BASE);
  p += F(".drop{margin-top:6px;border:1px dashed var(--line);border-radius:12px;padding:18px;text-align:center;"
         "background:rgba(8,12,22,.4);color:var(--dim);font:13px var(--mono)}"
         ".drop b{color:var(--ink)}.drop input{display:block;margin:12px auto 0;color:var(--dim);font:12px var(--mono)}"
         ".bar{height:8px;border-radius:6px;background:rgba(127,142,168,.18);overflow:hidden;margin-top:16px;display:none}"
         ".bar i{display:block;height:100%;width:0;background:var(--accent);transition:width .2s}"
         ".st{font:12px var(--mono);color:var(--dim);text-align:center;margin-top:10px;min-height:16px}"
         ".back{display:inline-block;margin-top:16px;font:12px var(--mono)}"
         "</style></head><body><main class='card'>"
         "<div class='hd'><span class='dot'></span><span class='wm'>netio<b>.probe</b></span></div>"
         "<p class='tag'>FIRMWARE · WEB OTA</p>"
         "<form id='f' method='POST' action='/update' enctype='multipart/form-data'>"
         "<div class='drop'>Derlenmiş <b>.bin</b> dosyasını seçin"
         "<input id='file' type='file' name='firmware' accept='.bin' required>"
         "<input id='md5' type='text' name='md5' autocomplete='off' placeholder='MD5 (opsiyonel · 32 hex)' style='width:100%;margin-top:12px;box-sizing:border-box'></div>"
         "<div class='bar' id='bar'><i id='barf'></i></div>"
         "<div class='st' id='st'></div>"
         "<button class='go' type='submit'>Yükle ve güncelle</button>"
         "</form>"
         "<a class='back' href='/'>← yönetime dön</a>"
         "<p class='hint'>Yükleme sırasında cihazın gücünü kesmeyin. Bittiğinde otomatik yeniden başlar.</p>"
         "</main><script>"
         "var f=document.getElementById('f'),file=document.getElementById('file'),bar=document.getElementById('bar'),barf=document.getElementById('barf'),st=document.getElementById('st');"
         "f.addEventListener('submit',function(e){e.preventDefault();if(!file.files.length){st.textContent='Önce bir .bin seçin.';return;}"
         "var fd=new FormData();fd.append('firmware',file.files[0]);"
         "var m=document.getElementById('md5').value.trim();var url='/update'+(m.length===32?('?md5='+encodeURIComponent(m)):'');"
         "var x=new XMLHttpRequest();x.open('POST',url);"
         "bar.style.display='block';st.textContent='Yükleniyor…';"
         "x.upload.onprogress=function(ev){if(ev.lengthComputable){var pc=Math.round(ev.loaded/ev.total*100);barf.style.width=pc+'%';st.textContent='Yükleniyor… '+pc+'%';}};"
         "x.onload=function(){if(x.status===200){barf.style.width='100%';st.textContent='Tamamlandı. Cihaz yeniden başlatılıyor…';setTimeout(function(){location.href='/';},6000);}else if(x.status===401){st.textContent='Yetki hatası (401).';}else{st.textContent='Hata: '+x.status+' '+x.responseText;}};"
         "x.onerror=function(){st.textContent='Bağlantı koptu (cihaz yeniden başlamış olabilir).';};"
         "x.send(fd);});"
         "</script></body></html>");
  return p;
}

static void handleAdminRoot() {
  if (!requireAuth()) return;
  server.send(200, "text/html", buildAdminPage());
}

static void handleStatus() {
  if (!requireAuth()) return;
  char b[260];
  snprintf(b, sizeof(b),
    "{\"tempd\":%ld,\"oper\":%ld,\"rssi\":%ld,\"up\":%lu,\"heap\":%u,\"rd\":%lu,\"er\":%lu,\"sn\":%lu}",
    (long)g_tempDeci, (long)g_sensorOper, (long)WiFi.RSSI(),
    (unsigned long)uptimeSec(), (unsigned)ESP.getFreeHeap(),
    (unsigned long)g_readCount, (unsigned long)g_errCount, (unsigned long)g_snmpCount);
  server.send(200, "application/json", b);
}

static void handleAdminSave() {
  if (!requireAuth()) return;
  bool wifiChanged = false;
  if (server.hasArg("ssid")) {
    String s = server.arg("ssid");
    if (s.length() && s != String(cfg.ssid)) { copyStr(cfg.ssid, s.c_str(), sizeof(cfg.ssid)); cfg.configured = 1; wifiChanged = true; }
  }
  if (server.hasArg("pass") && server.arg("pass").length()) { copyStr(cfg.pass, server.arg("pass").c_str(), sizeof(cfg.pass)); wifiChanged = true; }
  if (server.hasArg("ro") && server.arg("ro").length())  copyStr(cfg.roComm, server.arg("ro").c_str(), sizeof(cfg.roComm));
  if (server.hasArg("sn") && server.arg("sn").length())  copyStr(cfg.sysName, server.arg("sn").c_str(), sizeof(cfg.sysName));
  if (server.hasArg("sl")) copyStr(cfg.sysLocation, server.arg("sl").c_str(), sizeof(cfg.sysLocation));
  if (server.hasArg("sc")) copyStr(cfg.sysContact, server.arg("sc").c_str(), sizeof(cfg.sysContact));
  if (server.hasArg("ota") && server.arg("ota").length()) copyStr(cfg.otaPass, server.arg("ota").c_str(), sizeof(cfg.otaPass));
  if (server.hasArg("au") && server.arg("au").length())  copyStr(cfg.adminUser, server.arg("au").c_str(), sizeof(cfg.adminUser));
  if (server.hasArg("ap") && server.arg("ap").length())  setAdminPassword(server.arg("ap").c_str());
  if (server.hasArg("acl")) {
    String a = server.arg("acl"); a.trim();
    if (a.length() == 0 || a == "0.0.0.0/0") { cfg.aclNet = 0; cfg.aclBits = 0; }
    else { uint32_t net; uint8_t bits; if (parseCidr(a, net, bits)) { cfg.aclNet = net; cfg.aclBits = bits; } }
  }
  if (server.hasArg("slog")) { String s = server.arg("slog"); s.trim(); copyStr(cfg.syslogIp, s.c_str(), sizeof(cfg.syslogIp)); }
  if (server.hasArg("slogp") && server.arg("slogp").length()) cfg.syslogPort = (uint16_t)server.arg("slogp").toInt();
  saveConfig();

  String p; p.reserve(2200);
  p += F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<meta http-equiv='refresh' content='");
  p += wifiChanged ? "8;url=/" : "2;url=/";
  p += F("'><title>netio.probe</title><style>");
  p += FPSTR(STYLE_BASE);
  p += F("</style></head><body><main class='card'>"
         "<div class='hd'><span class='dot'></span><span class='wm'>netio<b>.probe</b></span></div>"
         "<p class='tag'>AYARLAR</p><p class='msg'>");
  p += wifiChanged ? F("Kaydedildi. WiFi değiştiği için cihaz yeniden başlatılıyor…")
                   : F("Kaydedildi ve uygulandı. Yönetime dönülüyor…");
  p += F("</p><p class='hint'>Yönetici şifresini değiştirdiyseniz tarayıcı yeniden giriş isteyebilir.</p>"
         "</main></body></html>");
  server.send(200, "text/html", p);

  if (wifiChanged) { delay(900); ESP.restart(); }
}

static void handleReset() {
  if (!requireAuth()) return;
  String p; p.reserve(1800);
  p += F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'><style>");
  p += FPSTR(STYLE_BASE);
  p += F("</style></head><body><main class='card'>"
         "<div class='hd'><span class='dot'></span><span class='wm'>netio<b>.probe</b></span></div>"
         "<p class='tag'>FABRİKA AYARLARI</p>"
         "<p class='msg'>Tüm ayarlar silindi. Cihaz yeniden başlatılıyor ve kurulum portalına (AP) dönüyor…</p>"
         "</main></body></html>");
  server.send(200, "text/html", p);
  delay(900);
  resetConfig();
  delay(150);
  ESP.restart();
}

static void handleReboot() {
  if (!requireAuth()) return;
  String p; p.reserve(1500);
  p += F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<meta http-equiv='refresh' content='6;url=/'><style>");
  p += FPSTR(STYLE_BASE);
  p += F("</style></head><body><main class='card'>"
         "<div class='hd'><span class='dot'></span><span class='wm'>netio<b>.probe</b></span></div>"
         "<p class='tag'>YENİDEN BAŞLATMA</p>"
         "<p class='msg'>Cihaz yeniden başlatılıyor…</p></main></body></html>");
  server.send(200, "text/html", p);
  delay(700);
  ESP.restart();
}

// Prometheus text-exposition. ACL korumalı, login GEREKMEZ (scrape için).
static void handleMetrics() {
  IPAddress ip = server.client().remoteIP();
  if (!ipAllowed(ip)) { server.send(403, "text/plain", "403"); return; }
  String m; m.reserve(1100);
  m += F("# HELP netio_temp_celsius DS18B20 sicakligi (C)\n# TYPE netio_temp_celsius gauge\n");
  if (g_sensorOper == 1) { m += F("netio_temp_celsius "); m += String(g_tempDeci / 10.0, 2); m += '\n'; }
  m += F("# HELP netio_sensor_up Sensor calisiyor (1=ok)\n# TYPE netio_sensor_up gauge\nnetio_sensor_up ");
  m += String(g_sensorOper == 1 ? 1 : 0); m += '\n';
  m += F("# TYPE netio_rssi_dbm gauge\nnetio_rssi_dbm "); m += String((long)WiFi.RSSI()); m += '\n';
  m += F("# TYPE netio_free_heap_bytes gauge\nnetio_free_heap_bytes "); m += String((unsigned)ESP.getFreeHeap()); m += '\n';
  m += F("# TYPE netio_uptime_seconds counter\nnetio_uptime_seconds "); m += String((unsigned long)uptimeSec()); m += '\n';
  m += F("# TYPE netio_snmp_requests_total counter\nnetio_snmp_requests_total "); m += String((unsigned long)g_snmpCount); m += '\n';
  m += F("# TYPE netio_sensor_reads_total counter\nnetio_sensor_reads_total "); m += String((unsigned long)g_readCount); m += '\n';
  m += F("# TYPE netio_sensor_errors_total counter\nnetio_sensor_errors_total "); m += String((unsigned long)g_errCount); m += '\n';
  server.send(200, "text/plain; version=0.0.4; charset=utf-8", m);
}

static void startRunServices() {
  g_mode = MODE_RUN;
  udp.begin(SNMP_PORT);
  setupOTA();

  // --- HTTP Basic Auth korumalı web yönetim arayüzü ---
  server.on("/",       HTTP_GET,  handleAdminRoot);
  server.on("/status", HTTP_GET,  handleStatus);
  server.on("/save",   HTTP_POST, handleAdminSave);
  server.on("/reset",  HTTP_POST, handleReset);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.on("/metrics", HTTP_GET, handleMetrics);   // Prometheus (ACL korumalı, login yok)
  server.on("/update", HTTP_GET, []() {
    if (!requireAuth()) return;
    server.send(200, "text/html", buildUpdatePage());
  });

  // --- Web tabanlı OTA: tarayıcıdan .bin yükle ---
  server.on("/update", HTTP_POST,
    []() {                                   // yükleme bittikten sonra çağrılan sonuç handler'ı
      if (!requireAuth()) return;
      bool ok = !Update.hasError() && !g_otaAuthFail;
      server.sendHeader("Connection", "close");
      String r; r.reserve(1600);
      r += F("<!DOCTYPE html><html lang='tr'><head><meta charset='utf-8'><style>");
      r += FPSTR(STYLE_BASE);
      r += F("</style></head><body><main class='card'><div class='hd'><span class='dot'></span>"
             "<span class='wm'>netio<b>.probe</b></span></div><p class='tag'>WEB OTA</p><p class='msg'>");
      r += ok ? F("Firmware güncellendi. Cihaz yeniden başlatılıyor…")
              : F("Güncelleme başarısız. Eski firmware korunuyor.");
      r += F("</p></main></body></html>");
      server.send(ok ? 200 : 500, "text/html", r);
      delay(800);
      if (ok) ESP.restart();
    },
    []() {                                   // dosya parça parça gelirken çağrılan upload handler'ı
      HTTPUpload &up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        g_otaAuthFail = !authClientOK();
        if (g_otaAuthFail) { syslogf(SLOG_WARNING, "OTA reddedildi (yetki) src=%s", server.client().remoteIP().toString().c_str()); return; }
        syslogf(SLOG_NOTICE, "OTA basladi: %s", up.filename.c_str());
        uint32_t maxSketch = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        String md5 = server.arg("md5"); md5.trim();
        if (md5.length() == 32) Update.setMD5(md5.c_str());   // opsiyonel bütünlük doğrulaması
        if (!Update.begin(maxSketch)) Update.printError(Serial);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (g_otaAuthFail) return;
        if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
      } else if (up.status == UPLOAD_FILE_END) {
        if (g_otaAuthFail) return;
        if (Update.end(true)) syslogf(SLOG_NOTICE, "OTA tamam: %u byte", (unsigned)up.totalSize);
        else { Update.printError(Serial); syslogf(SLOG_ERR, "OTA yazma/dogrulama hatasi"); }
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.end();
        Serial.println(F("[OTA-WEB] Yukleme iptal edildi."));
      }
      yield();
    });

  server.onNotFound([]() { server.send(404, "text/plain", "404 Not Found"); });
  server.collectHeaders("Authorization");        // Basic Auth başlığını oku (variadic imza)
  server.begin();
  MDNS.addService("http", "tcp", WEB_PORT);
  syslogf(SLOG_INFO, "boot up ip=%s host=%s.local", WiFi.localIP().toString().c_str(), hostName().c_str());

  Serial.println("[SNMP] Agent hazir. UDP/" + String(SNMP_PORT) +
                 "  community='" + String(cfg.roComm) + "'");
  Serial.println("[WEB ] http://" + WiFi.localIP().toString() + "/  (kullanici: " + String(cfg.adminUser) + ")");
  Serial.println("  snmpwalk -v2c -c " + String(cfg.roComm) + " " +
                 WiFi.localIP().toString() + " .1.3.6.1.2.1.1");
  Serial.println("  snmpget  -v2c -c " + String(cfg.roComm) + " " +
                 WiFi.localIP().toString() + " .1.3.6.1.2.1.99.1.1.1.4.1");
}

/* ================================ SENSÖR DURUMU ================================== */

enum SensorState { S_IDLE, S_CONVERTING };
static SensorState sensorState = S_IDLE;
static unsigned long convStart = 0, lastConv = 0;

// Durum değişiminde syslog'a yaz (her okumada değil, yalnızca geçişte)
static void setSensorOper(int32_t v, const char *why) {
  if (v != g_sensorOper) {
    if (v == 2)      syslogf(SLOG_WARNING, "sensor fault: %s", why);
    else if (v == 1) syslogf(SLOG_NOTICE, "sensor recovered");
  }
  g_sensorOper = v;
}

static void serviceSensor() {
  if (sensorState == S_IDLE) {
    if (millis() - lastConv >= SENSOR_INTERVAL) {
      if (ds18b20.startConversion()) { sensorState = S_CONVERTING; convStart = millis(); }
      else { setSensorOper(2, "presence yok"); g_errCount++; lastConv = millis(); }
    }
  } else { // S_CONVERTING (bloklamadan 750ms bekle)
    if (millis() - convStart >= DS18B20_CONV_MS) {
      float t = ds18b20.readTemperature();
      if (!isnan(t)) {
        g_tempDeci  = (int32_t)lroundf(t * 10.0f);
        g_tempMilli = (int32_t)lroundf(t * 1000.0f);
        setSensorOper(1, ""); g_readCount++;
        Serial.printf("[DS18B20] %.2f C  (read=%lu err=%lu heap=%u)\n",
                      t, g_readCount, g_errCount, ESP.getFreeHeap());
      } else {
        setSensorOper(2, "CRC/aralik/hat"); g_errCount++;
        Serial.println(F("[DS18B20] Okuma hatasi (CRC/aralik)."));
      }
      sensorState = S_IDLE; lastConv = millis();
    }
  }
}

/* ================================== SETUP / LOOP ================================= */

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(F("\n\n=== netio.probe v1.2 (ESP8266 DS18B20 SNMP) ==="));
  Serial.println("Chip ID : 0x" + String(ESP.getChipId(), HEX));
  Serial.println("Host    : " + hostName());
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " B");

  bootMillis = millis();

  pinMode(0, INPUT_PULLUP);                       // FLASH butonu (ayar sıfırlama)
  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);       // GPIO14 (D5) <-> GND köprüsü = fabrika reset
  EEPROM.begin(sizeof(Config) + 16);
  loadConfig();

  // Açılışta köprü zaten kapalıysa (D5-GND) hemen fabrika ayarına dön
  if (digitalRead(FACTORY_RESET_PIN) == LOW) {
    delay(50);
    if (digitalRead(FACTORY_RESET_PIN) == LOW) {
      Serial.println(F("[CFG] Acilista D5<->GND kopru algilandi -> fabrika ayarlari."));
      resetConfig();
    }
  }

  ds18b20.begin();

  if (cfg.configured && strlen(cfg.ssid) > 0 && connectSTA(STA_CONNECT_TIMEOUT)) {
    startRunServices();
  } else {
    startPortal();
  }
}

static void checkResetTriggers() {
  // GPIO0 (FLASH butonu) 3 sn VEYA GPIO14<->GND köprüsü (D5) ~2 sn -> fabrika reset
  static unsigned long btnStart = 0, jmpStart = 0;

  if (digitalRead(0) == LOW) {
    if (btnStart == 0) btnStart = millis();
    else if (millis() - btnStart > 3000) {
      Serial.println(F("\n[CFG] FLASH 3sn basili -> fabrika ayarlari."));
      resetConfig(); delay(300); ESP.restart();
    }
  } else btnStart = 0;

  if (digitalRead(FACTORY_RESET_PIN) == LOW) {
    if (jmpStart == 0) jmpStart = millis();
    else if (millis() - jmpStart > FACTORY_RESET_HOLD_MS) {
      Serial.println(F("\n[CFG] D5<->GND kopru -> fabrika ayarlari."));
      resetConfig(); delay(300); ESP.restart();
    }
  } else jmpStart = 0;
}

void loop() {
  yield();
  ESP.wdtFeed();             // yazılım watchdog'u besle
  checkResetTriggers();

  if (g_mode == MODE_PORTAL) {
    dnsServer.processNextRequest();
    server.handleClient();
    // portal LED yanıp sönmesi
    static unsigned long b = 0;
    if (millis() - b > 300) { digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); b = millis(); }
    return;
  }

  // ---- MODE_RUN ----
  ArduinoOTA.handle();
  MDNS.update();
  server.handleClient();     // şifreli web yönetim arayüzü + web OTA
  snmpHandle();
  serviceSensor();

  // Heap taban koruması: boş heap uzun süre eşik altındaysa kontrollü reboot
  static unsigned long lowHeapSince = 0;
  if (ESP.getFreeHeap() < HEAP_FLOOR) {
    if (lowHeapSince == 0) lowHeapSince = millis();
    else if (millis() - lowHeapSince > HEAP_FLOOR_MS) {
      syslogf(SLOG_ERR, "dusuk heap %u -> reboot", (unsigned)ESP.getFreeHeap());
      delay(50); ESP.restart();
    }
  } else lowHeapSince = 0;

  // periyodik durum raporu
  static unsigned long lastReport = 0;
  if (millis() - lastReport >= 60000UL) {
    Serial.printf("[STAT] up=%lus heap=%u rssi=%ld read=%lu err=%lu snmp=%lu\n",
                  uptimeSec(), ESP.getFreeHeap(), (long)WiFi.RSSI(),
                  g_readCount, g_errCount, g_snmpCount);
    lastReport = millis();
  }

  // WiFi kopması -> reconnect, uzun sürerse portala dön
  static unsigned long wifiDownSince = 0, lastRetry = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDownSince == 0) { wifiDownSince = millis(); Serial.println(F("[WiFi] Baglanti koptu.")); }
    if (millis() - wifiDownSince > WIFI_LOST_PORTAL_MS) {
      Serial.println(F("[WiFi] Uzun suredir yok -> yeniden baslat (portala dusecek)."));
      delay(200);
      ESP.restart();
    }
    if (millis() - lastRetry > 10000UL) { WiFi.reconnect(); lastRetry = millis(); }
  } else if (wifiDownSince != 0) {
    wifiDownSince = 0;
    Serial.println(F("[WiFi] Tekrar bagli."));
  }

  delay(2);
}

/* =====================================================================================
 *  SNMP OID HARİTASI (özet)
 *  -------------------------------------------------------------------------------------
 *  .1.3.6.1.2.1.1.1.0           sysDescr          (OCTET STRING)
 *  .1.3.6.1.2.1.1.2.0           sysObjectID       (OID -> .1.3.6.1.4.1.63333.1)
 *  .1.3.6.1.2.1.1.3.0           sysUpTime         (TimeTicks, centisaniye)
 *  .1.3.6.1.2.1.1.4.0           sysContact        (OCTET STRING)
 *  .1.3.6.1.2.1.1.5.0           sysName           (OCTET STRING)
 *  .1.3.6.1.2.1.1.6.0           sysLocation       (OCTET STRING)
 *  .1.3.6.1.2.1.1.7.0           sysServices       (INTEGER)
 *
 *  .1.3.6.1.2.1.99.1.1.1.1.1    entPhySensorType        (8 = celsius)
 *  .1.3.6.1.2.1.99.1.1.1.2.1    entPhySensorScale       (9 = units, 10^0)
 *  .1.3.6.1.2.1.99.1.1.1.3.1    entPhySensorPrecision   (1)
 *  .1.3.6.1.2.1.99.1.1.1.4.1    entPhySensorValue       (sıcaklık x10 -> /10 = °C)
 *  .1.3.6.1.2.1.99.1.1.1.5.1    entPhySensorOperStatus  (1 = ok, 2 = unavailable)
 *
 *  .1.3.6.1.4.1.63333.10.1.0    sıcaklık x1000 (m°C)    (Integer32)
 *  .1.3.6.1.4.1.63333.10.2.0    boş heap (byte)         (Gauge32)
 *  .1.3.6.1.4.1.63333.10.3.0    WiFi RSSI (dBm)         (Integer32)
 *  .1.3.6.1.4.1.63333.10.4.0    uptime (saniye)         (Gauge32)
 *  .1.3.6.1.4.1.63333.10.5.0    ölçüm sayısı            (Counter32)
 *  .1.3.6.1.4.1.63333.10.6.0    hata sayısı             (Counter32)
 *  .1.3.6.1.4.1.63333.10.7.0    SNMP istek sayısı       (Counter32)
 *
 *  GÜVENLİK NOTLARI
 *  - SNMP v2c community düz metindir; güvenli ağda kullanın, community'leri değiştirin.
 *  - OTA şifresini güçlü seçin. Gerçek dağıtımda PEN'i (63333) IANA'dan alın.
 *  - Web yönetim arayüzü HTTP Basic Auth ile korunur ama HTTPS DEĞİLDİR (şifre düz metin
 *    base64 olarak gider). VARSAYILAN admin/netioprobe bilgisini İLK GİRİŞTE değiştirin.
 *  - Web OTA (/update) yalnızca kimlik doğrulanmış istemciye .bin yazar; yine de yükleme
 *    yetkisini ağ seviyesinde de kısıtlayın (VLAN/erişim listesi).
 *  - Fabrika reset: web UI düğmesi VEYA GPIO14(D5)<->GND ~2 sn köprü VEYA GPIO0 3 sn.
 *  - Production için SNMPv3 (auth+priv) + HTTPS/ters proxy önerilir; bu agent v2c kapsamındadır.
 * ===================================================================================== */
