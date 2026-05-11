# CryptONN PHP Kod Koruma ve Lisanslama Sistemi

**Diller:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **CryptONN Loader tamamen ücretsizdir ve lisans anahtarı gerektirmez.** Lisanslama, yükleme aşamasında değil şifreleme aşamasında uygulanır. Sunucuya bir kez kurulduktan sonra tüm korumalı uygulamaları otomatik olarak yönetir.

---

## CryptONN Nedir?

CryptONN, PHP uygulamalarını ticari olarak dağıtan bağımsız yazılım satıcıları (ISV) ve geliştirme ekipleri için tasarlanmış profesyonel bir PHP kaynak kodu koruma ve yazılım lisanslama platformudur. PHP kaynak dosyalarını, tersine mühendisliğe, decompile işlemlerine ve yetkisiz dağıtıma karşı temelden dirençli, AES-256-GCM şifreli bir ikili formata dönüştürür; standart PHP altyapısıyla tam çalışma zamanı performansını ve uyumluluğu korur.

Sistem iki bileşenden oluşur: **CryptONN Encoder** (geliştirici tarafından PHP dosyalarını korumak için kullanılan masaüstü uygulaması) ve **CryptONN Extension** (bu depo — tek bir kurulum scripti aracılığıyla müşteri sunucusuna kurulan, herhangi bir manuel yapılandırma gerektirmeyen yerel PHP eklentisi).

---

## Hızlı Kurulum

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh)
```

Kurulum scripti; kurulu tüm PHP versiyonlarını otomatik olarak tespit eder (Plesk, cPanel/EasyApache 4, DirectAdmin, bare Linux), her versiyon için doğru `.so` ikili dosyasını indirir, PHP eklenti dizinine kurar ve her `php.ini` dosyasına `extension=cryptonn` ekler. Manuel yapılandırma gerekmez.

**Desteklenen ortamlar:** Debian · Ubuntu · AlmaLinux · RHEL · CentOS · Plesk · cPanel/EasyApache 4 · DirectAdmin

---

## Script Komutları

| Komut | Açıklama |
|---|---|
| `(yok)` | En son versiyona kur veya güncelle (varsayılan) |
| `--install` | Güncel olsa bile yeniden kur |
| `--update` | Yeni versiyon varsa yükselt |
| `--uninstall` | Tüm PHP versiyonlarından kaldır |
| `--status` | Her PHP için kurulum durumunu ve versiyonu göster |
| `--help` | Kullanım yardımını göster |

```bash
# Durum ve kurulu versiyonları kontrol et
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --status

# Tüm PHP versiyonlarından kaldır
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --uninstall
```

---

## Çözülen Problemler

| Problem | CryptONN Nasıl Çözer |
|---|---|
| **Kaynak kod hırsızlığı** | PHP mantığı şifreli ikili yüke dönüştürülür. Tam dosya sistemi erişimiyle bile orijinal kaynak kodu yeniden oluşturulamaz. |
| **Yetkisiz dağıtım** | Her korumalı dosya, sunucu tarafında doğrulanan gömülü bir lisans tanımlayıcı taşır. Lisanssız sunuculara kopyalanan dosyalar çalıştırılmayı reddeder. |
| **Lisans süresi uygulaması** | Deneme süreleri ve son kullanma tarihleri lisanslama sunucusunda uygulanır. Atlatılabilecek istemci taraflı kontrol yoktur. |
| **Çok kiracılı dağıtım** | Aynı kod tabanı, her biri benzersiz koşullara, kullanım limitine ve alan adı kısıtlamalarına sahip birden fazla müşteriye lisanslanabilir. |
| **Yetkisiz yeniden dağıtım** | Korumalı dosyalar belirli lisans tanımlayıcılarına bağlıdır — aktif ve geçerli bir lisans olmadan işe yaramazlar. |

---

## Nasıl Çalışır

**Şifreleme sırasında** (geliştiricinin bilgisayarı):
CryptONN Encoder bir PHP dosyasını işler ve çıktı olarak bir `.cryptonn` dosyası üretir — AES-256-GCM şifreli ikili yükü izleyen minimal bir stub başlığı içeren geçerli bir PHP dosyası. Stub, `__cnn_load()` fonksiyonunu (eklenti tarafından sağlanır) dosya yolu ve yük ofseti ile çağırır. Orijinal PHP kaynak kodu, dosyada hiçbir okunabilir formda bulunmaz.

**Çalışma zamanında** (müşteri sunucusu):
1. PHP, `.cryptonn` dosyasını normal şekilde çalıştırır (`include`, `require` veya doğrudan çağrı)
2. Stub başlığı çalışır: `__cnn_load(__FILE__, __COMPILER_HALT_OFFSET__)`
3. Eklenti, ikili yük başlığından gömülü lisans tanımlayıcısını okur
4. Eklenti, lisans tanımlayıcısını ve sunucunun ağ kimliğinden türetilen benzersiz bir parmak izini sunarak CryptONN lisanslama API'sine başvurur
5. API, lisansı doğrular ve bu sunucu için özel olarak şifrelenmiş bir şifre çözme anahtarı döner
6. Eklenti, PHP yükünü bellekte çözer, çalıştırır ve geçici yürütme dosyasını hemen siler

`auto_prepend_file` yapılandırması gerekmez. `extension=cryptonn` php.ini dosyasında mevcut olduğunda PHP başlangıçta eklentiyi otomatik olarak yükler.

---

## Güvenlik Modeli

| Özellik | Detay |
|---|---|
| **Sunucuda saklanan anahtarlar** | Yok. Dosya sisteminde veya ortamda kriptografik anahtar saklanmaz. |
| **Sunucu bağlama** | Her sunucu, ağ kimliğinden türetilen benzersiz bir parmak izine sahiptir. Bir sunucu için geçerli olan şifre çözme paketi, başka bir sunucuda kriptografik olarak işe yaramaz. |
| **API iletişimi** | Tüm anahtar dağıtımı şifreli HTTPS kanalları üzerinden gerçekleşir. Anahtarlar iletilmeden önce sunucuya özgü bir sarmalama anahtarıyla ek olarak şifrelenir. |
| **Çevrimdışı toleransı** | Üç katmanlı önbellek (işlem içi → dosya tabanlı 24 sa → yetkisiz kullanım süresi 72 sa), geçici API kesintileri sırasında çalışmayı sağlar. |
| **Deneme uygulaması** | Deneme lisansları katı sunucu taraflı son kullanma tarihini uygular. Deneme lisansları için yetkisiz kullanım süresi geçerli değildir — her önbellek kaçırmasında API başarılı yanıt vermelidir. |
| **Kurcalama tespiti** | Kesilmiş, değiştirilmiş veya bozuk `.cryptonn` dosyaları herhangi bir şifre çözme girişiminden önce tespit edilir ve reddedilir. |
| **Diskte düz metin yok** | Şifresi çözülen PHP kodu, kullanımdan hemen sonra silinen geçici bir dosya aracılığıyla yürütülür. |

---

## PHP Uyumluluğu

| PHP Versiyonu | Durum |
|---|---|
| PHP 7.2 | ✅ Tam destekleniyor |
| PHP 7.3 | ✅ Tam destekleniyor |
| PHP 7.4 | ✅ Tam destekleniyor |
| PHP 8.0 | ✅ Tam destekleniyor |
| PHP 8.1 | ✅ Tam destekleniyor |
| PHP 8.2 | ✅ Tam destekleniyor |
| PHP 8.3 | ✅ Tam destekleniyor |
| PHP 8.4 | 🔜 Planlanıyor |
| PHP 5.x · 7.0 · 7.1 | ❌ Desteklenmiyor |

---

## Sistem Gereksinimleri

| Bileşen | Gereksinim | Notlar |
|---|---|---|
| PHP | 7.2 – 8.3 | Tüm alt versiyonlar destekleniyor |
| Mimari | x86_64 · aarch64 | Her ikisi için önceden derlenmiş ikili dosyalar |
| bash | 4.0+ | Kurulum scripti için gerekli |
| curl | Herhangi | Scriptın ikili dosyaları indirmesi için |
| sha256sum | Herhangi | İkili bütünlük doğrulaması |
| Giden HTTPS | Port 443 | Lisans doğrulama API çağrıları için gerekli |
| APCu (opsiyonel) | Herhangi | İşlem içi önbellekleme; soğuk başlatma gecikmesini azaltır |

---

## Kurulumu Doğrulama

```bash
# Görsel durum tablosu
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --status

# Hızlı CLI kontrolleri
php -m | grep cryptonn
php -r "echo phpversion('cryptonn');"
```

Beklenen çıktı:
```
cryptonn
1.0.0
```

---

## Sorun Giderme

### Kurulum sonrası eklenti yüklenmiyor

```bash
# Eklentinin modül listesinde görünüp görünmediğini kontrol et
php -m | grep cryptonn

# PHP-FPM'in kullandığı php.ini yolunu bul
php-fpm8.2 -i | grep "Loaded Configuration"

# Direktifin mevcut olduğunu doğrula
grep cryptonn /etc/php/8.2/fpm/php.ini
```

Direktif mevcutsa ancak eklenti yüklenmiyorsa PHP-FPM'i yeniden başlatın:

```bash
systemctl restart plesk-php82-fpm   # Plesk
/scripts/restartsrv_php_fpm         # cPanel
systemctl restart php8.2-fpm        # bare Linux
```

---

### Lisanslama API'sine erişilemiyor

```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```

Sunucudan giden TCP 443 portuna izin verildiğini ve sunucunun harici DNS adlarını çözebildiğini doğrulayın. Çıkış proxy'si gerekiyorsa `CRYPTONN_API_URL` ortam değişkenini ayarlayın.

---

### `Geçersiz magic bytes`

`.cryptonn` dosyası geçerli bir CryptONN kodlu dosya değil veya aktarım sırasında bozulmuş (metin modunda aktarılmış ya da bir metin düzenleyiciyle açılmış). Dosyayı ikili modda yeniden aktarın; bozulma şüpheleniliyorsa yazılım satıcısından yeni bir kopya isteyin.

---

### `Eksik header`

`.cryptonn` dosyası kesilmiş — tamamen aktarılmamış. Dosyayı yeniden aktarın ve kaynak ile hedefteki disk alanını doğrulayın.

---

### `Şifre çözme başarısız`

API, dosyanın şifreleme parametreleriyle eşleşmeyen bir anahtar döndürdü. Dosyanın bu sunucuda aktif olan lisanstan farklı bir lisansla kodlandığı anlamına gelir. Hatada gösterilen lisans tanımlayıcısıyla yazılım satıcısına başvurun.

---

## Performans

Eklenti, sıcak isteklerde ihmal edilebilir ek yük için tasarlanmıştır:

| Önbellek Katmanı | Tipik Gecikme | Süre |
|---|---|---|
| İşlem içi (APCu) | < 0.1 ms | 1 saat |
| Dosya önbelleği | < 0.5 ms | 24 saat |
| API çağrısı (soğuk) | 50 – 200 ms | Önbellek kaçırmasında |
| Yetkisiz kullanım süresi | < 0.5 ms | 72 saate kadar (deneme dışı) |

APCu mevcut olduğunda otomatik olarak kullanılır. Yapılandırma gerekmez.

---

## Sıkça Sorulan Sorular

**S: Loader ücretsiz mi?**
A: Evet. CryptONN Extension ücretsiz ve açık kaynaklıdır. Herhangi bir sayıda sunucuya kurulmasıyla ilgili lisans anahtarı, abonelik veya ücret yoktur.

**S: PHP OPcache ile çalışıyor mu?**
A: Evet. OPcache, eklenti kodu çözüp çalıştırdıktan sonra PHP bayt kodu üzerinde çalışır. Etkileşim tamamen şeffaf ve doğrudur.

**S: Tek bir kurulum birden fazla uygulamaya hizmet verebilir mi?**
A: Evet. Tek bir eklenti kurulumu, `extension=cryptonn` aktif olan tüm PHP versiyonlarında, tüm uygulamalar genelinde sunucudaki tüm `.cryptonn` dosyalarını yönetir.

**S: API kesintisinde ne olur?**
A: Deneme dışı lisanslar, dosya tabanlı yetkisiz kullanım önbelleğini kullanarak 72 saate kadar normal şekilde çalışmaya devam eder. Deneme lisansları her önbellek kaçırmasında başarılı bir API yanıtı gerektirir — yetkisiz kullanım süresinden yararlanamazlar.

**S: Önbelleğe alınan veri bir güvenlik riski mi?**
A: Hayır. Önbelleğe alınan paket, sunucunun benzersiz parmak izinden türetilen bir anahtarla şifrelenir. Başka bir makinede çözümlenemez.

**S: Paylaşımlı hostingde kullanılabilir mi?**
A: PHP eklentisi (`.so` dosyası) kurma ve `php.ini` yazma yeteneği gerektirir. Bu genellikle VPS, dedicated sunucu, Plesk ve cPanel ortamlarında mevcuttur. Eklenti kurma yetkisi olmayan standart paylaşımlı hosting desteklenmez.

---

## Kaldırma

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --uninstall
```

`cryptonn.so` dosyasını tüm PHP eklenti dizinlerinden kaldırır, her `php.ini` dosyasından `extension=cryptonn` satırını siler ve Plesk üzerinde PHP-FPM servislerini otomatik olarak yeniden başlatır. Mevcut `.cryptonn` dosyaları etkilenmez — eklenti yeniden kurulana kadar yürütülemez duruma gelirler.

---

## Destek

| Kanal | Bağlantı |
|---|---|
| Dokümantasyon | [docs.laicos.com.tr](https://laicos.com.tr) |
| Ticari Lisanslama | [laicos.com.tr](https://laicos.com.tr) |
| Sorun Takipçisi | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-loader/issues) |

---

*© 2026 LAICOS Technology. CryptONN, LAICOS Technology'nin bir ürünüdür.*