# CryptONN PHP Kod Koruma ve Lisanslama Sistemi

**Diller:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **CryptONN Loader tamamen ücretsizdir ve herhangi bir lisans anahtarı gerektirmez.** Lisanslama süreci kodlama aşamasında gerçekleşir; Loader aşamasında herhangi bir ücret veya kısıtlama yoktur. Sunucuya bir kez kurulur, tüm korumalı uygulamaları otomatik olarak yönetir.

---

## CryptONN Nedir?

CryptONN; PHP uygulamalarını ticari olarak dağıtan bağımsız yazılım geliştiricileri (ISV) ve geliştirme ekipleri için tasarlanmış profesyonel bir PHP kaynak kodu koruma ve yazılım lisanslama platformudur. PHP kaynak dosyalarını; tersine mühendislik, decompile ve yetkisiz kopyalamaya karşı yapısal olarak dayanıklı, şifreli bir ikili formata dönüştürür. Tüm bu koruma, standart PHP altyapısıyla tam uyumluluk ve sıfır performans kaybıyla sağlanır.

Sistem iki bileşenden oluşur: PHP dosyalarını korumak için geliştiricinin kullandığı **CryptONN Encoder** (masaüstü uygulaması) ve son müşterinin sunucusuna kurulan, korumalı dosyaların şeffaf biçimde çalıştırılmasını sağlayan **CryptONN Loader** (bu depo).

---

## Çözüm Üretilen Sorunlar

| Sorun | CryptONN'un Yaklaşımı |
|---|---|
| **Kaynak kod hırsızlığı** | PHP mantığı şifreli bir ikili yüke dönüştürülür. Dosya sistemine tam erişim sağlansa dahi orijinal kaynak kod geri elde edilemez. |
| **Yetkisiz dağıtım** | Her korumalı dosya, sunucu tarafında doğrulanan gömülü bir lisans tanımlayıcısı taşır. Lisanssız sunuculara kopyalanan dosyalar çalışmayı reddeder. |
| **Lisans süresi yönetimi** | Deneme süreleri ve son kullanma tarihleri lisanslama sunucusunda uygulanır. İstemci tarafında atlatılabilecek herhangi bir denetim mekanizması yoktur. |
| **Çok müşterili dağıtım** | Aynı kod tabanı; her biri kendine özgü koşullara, kullanım limitine ve domain kısıtlamasına sahip birden fazla müşteriye lisanslanabilir. |
| **Yetkisiz yeniden dağıtım** | Korumalı dosyalar belirli lisans tanımlayıcılarına bağlıdır; geçerli ve aktif bir lisans olmaksızın çalışmaz. |

---

## Nasıl Çalışır?

**Kodlama aşamasında** (geliştirici makinesi):
Bir PHP dosyası CryptONN Encoder tarafından işlenir. Çıktı, şifreli bir yük ve gömülü lisans tanımlayıcısı içeren `.cryptonn` ikili dosyasıdır. Orijinal PHP kaynak kodu çıktı dosyasında hiçbir biçimde yer almaz.

**Çalışma zamanında** (müşteri sunucusu):
1. PHP bir `.cryptonn` dosyasını çalıştırmayı dener
2. Loader, `auto_prepend_file` mekanizması aracılığıyla çalışmayı yakalar
3. Loader, dosya başlığındaki gömülü lisans tanımlayıcısını okur
4. Loader, CryptONN lisanslama API'sine; lisans tanımlayıcısını ve sunucunun ağ kimliğinden türetilen benzersiz parmak izini sunarak bağlanır
5. API lisansı doğrular ve bu sunucuya özgü olarak şifrelenmiş bir şifre çözme anahtarı döner
6. Loader, PHP yükünü tamamen bellek içinde çözer
7. Çözülen PHP kodu doğrudan çalışır; diskte kaynak kod içeren geçici dosya bırakılmaz

Bu akış son kullanıcıya tamamen şeffaftır ve uygulamanın kod yapısında herhangi bir değişiklik gerektirmez.

---

## Güvenlik Modeli

| Özellik | Detay |
|---|---|
| **Sunucuda depolanan anahtar** | Hiçbiri. Dosya sisteminde veya ortamda hiçbir şifreleme anahtarı saklanmaz. |
| **Sunucu bağlama** | Her sunucunun ağ kimliğinden türetilen benzersiz bir parmak izi vardır. Bir sunucu için geçerli olan şifre çözme paketi başka bir sunucuda kriptografik olarak işe yaramaz. |
| **API iletişimi** | Tüm anahtar iletimi şifreli HTTPS kanalları üzerinden gerçekleşir. Anahtarlar iletimden önce sunucuya özgü sarmalama anahtarıyla ek olarak şifrelenir. |
| **Çevrimdışı tolerans** | Üç katmanlı önbellekleme (bellek içi → dosya tabanlı 24 saat → tolerans süresi 72 saat), geçici API kesintilerinde kesintisiz çalışmayı sağlar. |
| **Deneme lisansı uygulaması** | Deneme lisansları sunucu tarafında kesin son kullanma tarihini uygular. Deneme lisansları için çevrimdışı tolerans süresi uygulanmaz. |
| **Değişiklik tespiti** | Kesilen, değiştirilen veya bozulan `.cryptonn` dosyaları herhangi bir şifre çözme girişiminden önce tespit edilerek reddedilir. |
| **Diskte düz metin yok** | Şifresi çözülen PHP kodu hiçbir zaman kalıcı bir konuma yazılmaz. Geçici çalıştırma dosyaları kullanımın hemen ardından silinir. |

---

## PHP Uyumluluğu

| PHP Sürümü | Durum |
|---|---|
| PHP 7.2 | ✅ Tam desteklenir |
| PHP 7.3 | ✅ Tam desteklenir |
| PHP 7.4 | ✅ Tam desteklenir |
| PHP 8.0 | ✅ Tam desteklenir |
| PHP 8.1 | ✅ Tam desteklenir |
| PHP 8.2 | ✅ Tam desteklenir |
| PHP 8.3 | ✅ Tam desteklenir |
| PHP 8.4 | ✅ Tam desteklenir |
| PHP 8.5 | ✅ Tam desteklenir |
| PHP 5.x · 7.0 · 7.1 | ❌ Desteklenmez |

---

## Sistem Gereksinimleri

| Bileşen | Gereksinim | Notlar |
|---|---|---|
| PHP | 7.2 – 8.5 | Tüm minör sürümler desteklenir |
| ext-sodium | Herhangi bir sürüm | PHP 7.2 ile birlikte gelir; modern sistemlerde ayrıca kurulumu gerekmez |
| ext-openssl | Herhangi bir sürüm | Neredeyse tüm hosting ortamlarında varsayılan olarak mevcuttur |
| Giden HTTPS | Port 443 | Lisans doğrulama API çağrıları için gereklidir |
| Disk (önbellekleme) | Aktif lisans başına ~10 KB | Sistem geçici dizininde oluşturulan dosyalar |
| APCu (opsiyonel) | Herhangi bir sürüm | Bellek içi önbelleklemeyi etkinleştirir; soğuk başlatma gecikmesini önemli ölçüde azaltır |

---

## Kurulum

### Adım 1 — Loader'ı İndirin

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-extension/main/cryptonn-loader.php \
     -o /opt/cryptonn/cryptonn-loader.php
sudo chmod 644 /opt/cryptonn/cryptonn-loader.php
sudo chown root:root /opt/cryptonn/cryptonn-loader.php
```

### Adım 2 — PHP'yi Yapılandırın (ortamınıza göre seçin)

**cPanel / EasyApache 4**
```bash
# XX yerine PHP sürümünüzü yazın (ör. ea-php82)
echo "auto_prepend_file = /opt/cryptonn/cryptonn-loader.php" \
  >> /opt/cpanel/ea-phpXX/root/etc/php.ini
/scripts/restartsrv_apache
/scripts/restartsrv_php_fpm
```

**Plesk / DirectAdmin — site bazında `.user.ini`**
```ini
auto_prepend_file = /opt/cryptonn/cryptonn-loader.php
```

**Bare Metal — nginx + PHP-FPM pool konfigürasyonu**
```ini
; /etc/php/8.x/fpm/pool.d/www.conf
php_admin_value[auto_prepend_file] = /opt/cryptonn/cryptonn-loader.php
```
```bash
systemctl restart php8.2-fpm
```

**Apache — `.htaccess` (dizin bazında)**
```apache
php_value auto_prepend_file /opt/cryptonn/cryptonn-loader.php
```

### Adım 3 — Kurulumu Doğrulayın

Aşağıdaki içeriği `/tmp/cnn-dogrula.php` olarak kaydedin ve çalıştırın:

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader: Aktif\n" : "❌ CryptONN Loader: Yüklenmemiş\n";
echo "PHP Sürümü  : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ EKSİK") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ EKSİK") . "\n";
echo "APCu        : " . (function_exists('apcu_store') ? "✅ Mevcut" : "— Mevcut değil (opsiyonel)") . "\n";
```

```bash
php /tmp/cnn-dogrula.php
```

Doğru yapılandırılmış bir sunucuda beklenen çıktı:
```
✅ CryptONN Loader: Aktif
PHP Sürümü  : 8.2.x
ext-sodium  : ✅
ext-openssl : ✅
APCu        : ✅ Mevcut
```

---

## Sorun Giderme

### `CryptONN Loader requires ext-sodium`
**Neden:** `sodium` PHP eklentisi etkin PHP sürümü için etkinleştirilmemiş.

```bash
# cPanel / EasyApache 4
/scripts/install_ea_metapackage ea-php82-php-sodium

# AlmaLinux / RHEL / CentOS 8+
dnf install php-sodium

# Ubuntu / Debian
apt-get install php8.2-sodium

# Doğrulama
php -m | grep sodium
```

---

### `CryptONN Loader requires ext-openssl`
**Neden:** `openssl` eklentisi etkin değil.

**Çözüm:** İlgili `php.ini` dosyasında `extension=openssl` satırını etkinleştirin veya paket yöneticiniz aracılığıyla `php-openssl` paketini kurun.

---

### `Master key alınamadı`
**Neden:** Loader, CryptONN lisanslama API'sine ulaşamıyor. Bu durum; giden HTTPS bağlantısını engelleyen bir güvenlik duvarı kuralından, DNS çözümleme hatasından veya geçici bir ağ sorundan kaynaklanıyor olabilir.

```bash
# Bağlantıyı tanılayın
curl -sv --max-time 10 https://api.laicos.com.tr/health
```

**Çözümler:**
- Sunucudan giden TCP 443 portuna izin verildiğinden emin olun
- Sunucunun harici DNS adlarını çözümleyebildiğini doğrulayın
- Giden proxy arkasındaysa `CRYPTONN_API_URL` ortam değişkenini yapılandırın

---

### `Geçersiz magic bytes`
**Neden:** Dosya geçerli bir CryptONN kodlu dosya değil ya da aktarım sırasında bozulmuş (örneğin metin modu yerine ikili modda aktarılmış).

**Çözüm:** `.cryptonn` dosyasını ikili modda yeniden aktarın. Dosyayı bir metin düzenleyiciyle açmayın veya düzenlemeyin. Bozulma şüphesi varsa yazılım satıcısından yeni bir kopya isteyin.

---

### `Eksik header`
**Neden:** `.cryptonn` dosyası kesik; tam olarak aktarılamamış.

**Çözüm:** Dosyayı yeniden aktarın. Kaynak ve hedef disklerde yeterli alan olduğunu kontrol edin. Web sunucusu veya PHP yapılandırmanızdaki yükleme boyutu sınırlarını inceleyin.

---

### `Şifre çözme başarısız`
**Neden:** API'den dönen şifre çözme anahtarı, dosyanın şifreleme parametreleriyle eşleşmiyor. Bu genellikle dosyanın bu sunucuda aktif olan lisanstan farklı bir lisansla kodlandığını gösterir.

**Çözüm:** Dosyayı kodlarken doğru lisans tanımlayıcısının kullanıldığını yazılım satıcısıyla doğrulayın.

---

### `Geçici dosya yazılamadı`
**Neden:** PHP işleminin sistem geçici dizinine veya uygulamanın dizinine yazma yetkisi yok.

**Çözüm:** Web sunucusu kullanıcısının `sys_get_temp_dir()` (genellikle `/tmp`) dizinine yazma erişimi olduğundan emin olun. Güçlendirilmiş bir sistemde çalışıyorsanız SELinux veya AppArmor politikalarını kontrol edin.

---

### Loader aktif ama `.cryptonn` dosyaları çalışmıyor
**Olası nedenler:**
- `auto_prepend_file` doğru PHP SAPI'ye (CLI ve FPM) uygulanmıyor
- `.user.ini` dosyası önbellekten sunuluyor (`user_ini.cache_ttl` varsayılan olarak 300 saniyedir — bekleyip tekrar deneyin)
- Uygulama, PHP dosyalarını `include`/`require` ile prepend'i atlayan bir yol üzerinden çağırıyor

**Tanılama:**
```bash
php -r "echo ini_get('auto_prepend_file');"
```

---

## Performans

Loader, sıcak isteklerde ihmal edilebilir ek yük için tasarlanmıştır:

| Önbellek Katmanı | Tipik Gecikme | Süre |
|---|---|---|
| Bellek içi (APCu) | < 0,1 ms | 1 saat |
| Dosya önbelleği | < 0,5 ms | 24 saat |
| API çağrısı (soğuk) | 50 – 200 ms | Önbellek ıskalandığında |
| Tolerans süresi | < 0,5 ms | 72 saate kadar (deneme dışı) |

APCu mevcut olduğunda otomatik olarak kullanılır. Herhangi bir yapılandırma gerekmez.

---

## Sık Sorulan Sorular

**S: Loader kullanımı ücretli mi?**  
C: Hayır. CryptONN Loader tamamen ücretsiz ve açık kaynaklıdır. Herhangi bir sayıda sunucuya kurulmasıyla ilişkili lisans anahtarı, abonelik veya ücret bulunmamaktadır.

**S: PHP OPcache ile birlikte çalışıyor mu?**  
C: Evet. OPcache, Loader kodun şifresini çözüp çalıştırdıktan sonra PHP bytecode üzerinde işlem yapar. Bu etkileşim tamamen şeffaf ve doğrudur.

**S: Tek bir Loader kurulumu birden fazla uygulamaya hizmet verebilir mi?**  
C: Evet. Tek bir Loader kurulumu, sunucudaki tüm `.cryptonn` dosyalarını tüm uygulamalar ve PHP sürümleri genelinde yönetir.

**S: API kesintisinde ne olur?**  
C: Deneme dışı lisanslar, son başarılı API çağrısından itibaren 72 saate kadar dosya tabanlı önbellekten normal şekilde çalışmaya devam eder. Deneme lisansları her önbellek ıskalanmasında başarılı bir API yanıtı gerektirir.

**S: Önbelleklenen veriler güvenlik riski oluşturur mu?**  
C: Hayır. Önbelleğe alınan paket, sunucunun benzersiz parmak izinden türetilen bir anahtarla şifrelenir. Başka bir makinede şifresi çözülemez ve kullanılabilir biçimde ham şifre çözme anahtarı içermez.

**S: CryptONN paylaşımlı hostingde kullanılabilir mi?**  
C: Evet; hosting ortamı `.user.ini` veya `.htaccess` aracılığıyla `auto_prepend_file` ayarlanmasına izin veriyorsa ve giden HTTPS bağlantılarına izin veriliyorsa kullanılabilir.

---

## Loader'ı Kaldırma

1. `auto_prepend_file` direktifini `php.ini`, `.user.ini` veya `.htaccess` dosyasından kaldırın
2. PHP-FPM veya Apache'yi yeniden başlatın
3. Loader dizinini silin:
```bash
rm -rf /opt/cryptonn
```

Bu işlem hiçbir `.cryptonn` dosyasını etkilemez; geçerli bir Loader yeniden kurulana kadar bu dosyalar çalışmayacaktır.

---

## Destek

| Kanal | Bağlantı |
|---|---|
| Dokümantasyon | [laicos.com.tr](https://laicos.com.tr) |
| Ticari Lisanslama | [laicos.com.tr](https://laicos.com.tr) |
| Sorun Takibi | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-extension/issues) |

---

*© 2026 LAICOS Technology. CryptONN, LAICOS Technology'nin bir ürünüdür.*
