# CryptONN PHP-Code-Schutz und Lizenzsystem

**Sprachen:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **Der CryptONN Loader ist kostenlos und erfordert keinen Lizenzschlüssel.** Die Lizenzierung findet auf der Encoder-Seite statt. Der Loader wird einmalig pro Server installiert und verwaltet automatisch alle geschützten Anwendungen.

---

## Was ist CryptONN?

CryptONN ist eine professionelle PHP-Quellcode-Schutz- und Software-Lizenzierungsplattform für unabhängige Softwareanbieter (ISVs) und Entwicklungsteams, die PHP-Anwendungen kommerziell vertreiben. Die Plattform transformiert PHP-Quelldateien in ein undurchsichtiges, verschlüsseltes Binärformat, das grundlegend resistent gegen Reverse Engineering, Dekompilierung und unbefugte Weitergabe ist – bei voller Laufzeit-Kompatibilität mit standardmäßiger PHP-Infrastruktur und ohne Leistungseinbußen.

Das System besteht aus zwei Komponenten: dem **CryptONN Encoder** (Desktop-Anwendung, die der Entwickler zum Schutz von PHP-Dateien verwendet) und dem **CryptONN Loader** (dieses Repository – eine einzelne PHP-Datei, die auf dem Server des Endkunden installiert wird).

---

## Gelöste Probleme

| Problem | Lösung durch CryptONN |
|---|---|
| **Quellcode-Diebstahl** | Die PHP-Logik wird in eine verschlüsselte Binärdatei umgewandelt. Selbst bei vollem Dateisystemzugriff kann der Originalquellcode nicht rekonstruiert werden. |
| **Unbefugte Bereitstellung** | Jede geschützte Datei enthält eine eingebettete Lizenzkennung, die serverseitig validiert wird. Auf nicht lizenzierten Servern kopierte Dateien verweigern die Ausführung. |
| **Lizenzbedingungen** | Testzeiträume und Ablaufdaten werden auf dem Lizenzierungsserver durchgesetzt. Clientseitige Umgehungen sind nicht möglich. |
| **Mehrmandanten-Vertrieb** | Dieselbe Codebasis kann an mehrere Kunden lizenziert werden, jeweils mit eigenen Bedingungen, Nutzungslimits und Domain-Einschränkungen. |

---

## PHP-Kompatibilität

| PHP-Version | Status |
|---|---|
| PHP 7.2 | ✅ Vollständig unterstützt |
| PHP 7.3 | ✅ Vollständig unterstützt |
| PHP 7.4 | ✅ Vollständig unterstützt |
| PHP 8.0 | ✅ Vollständig unterstützt |
| PHP 8.1 | ✅ Vollständig unterstützt |
| PHP 8.2 | ✅ Vollständig unterstützt |
| PHP 8.3 | ✅ Vollständig unterstützt |
| PHP 8.4 | ✅ Vollständig unterstützt |
| PHP 8.5 | ✅ Vollständig unterstützt |
| PHP 5.x · 7.0 · 7.1 | ❌ Nicht unterstützt |

---

## Systemanforderungen

| Komponente | Anforderung | Hinweise |
|---|---|---|
| PHP | 7.2 – 8.5 | Alle Nebenversionen unterstützt |
| ext-sodium | Beliebige Version | Mit PHP 7.2+ gebündelt |
| ext-openssl | Beliebige Version | Standardmäßig in nahezu allen Hosting-Umgebungen verfügbar |
| Ausgehender HTTPS | Port 443 | Erforderlich für Lizenzvalidierungs-API-Aufrufe |
| APCu (optional) | Beliebige Version | Aktiviert In-Process-Caching; reduziert Cold-Start-Latenz deutlich |

---

## Installation

### Schritt 1 — Loader herunterladen

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-extension/main/cryptonn-loader.php \
     -o /opt/cryptonn/cryptonn-loader.php
sudo chmod 644 /opt/cryptonn/cryptonn-loader.php
```

### Schritt 2 — PHP konfigurieren

**cPanel / EasyApache 4**
```bash
echo "auto_prepend_file = /opt/cryptonn/cryptonn-loader.php" \
  >> /opt/cpanel/ea-phpXX/root/etc/php.ini
/scripts/restartsrv_apache && /scripts/restartsrv_php_fpm
```

**Plesk / DirectAdmin — `.user.ini`**
```ini
auto_prepend_file = /opt/cryptonn/cryptonn-loader.php
```

**Bare Metal — PHP-FPM Pool**
```ini
php_admin_value[auto_prepend_file] = /opt/cryptonn/cryptonn-loader.php
```
```bash
systemctl restart php8.2-fpm
```

**Apache — `.htaccess`**
```apache
php_value auto_prepend_file /opt/cryptonn/cryptonn-loader.php
```

### Schritt 3 — Installation überprüfen

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader: Aktiv\n" : "❌ CryptONN Loader: Nicht geladen\n";
echo "PHP-Version : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ FEHLT") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ FEHLT") . "\n";
```

---

## Fehlerbehebung

### `CryptONN Loader requires ext-sodium`
Die Erweiterung `sodium` ist für die aktive PHP-Version nicht aktiviert.
```bash
# AlmaLinux / RHEL / CentOS
dnf install php-sodium
# Ubuntu / Debian
apt-get install php8.2-sodium
# cPanel
/scripts/install_ea_metapackage ea-php82-php-sodium
```

### `CryptONN Loader requires ext-openssl`
Aktivieren Sie `extension=openssl` in der `php.ini` oder installieren Sie das Paket `php-openssl`.

### `Master key could not be retrieved`
Der Loader kann die CryptONN-Lizenzierungs-API nicht erreichen.
```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```
Stellen Sie sicher, dass ausgehender TCP-Port 443 vom Server aus erlaubt ist.

### `Invalid magic bytes`
Die Datei ist keine gültige CryptONN-kodierte Datei oder wurde während der Übertragung beschädigt. Übertragen Sie die `.cryptonn`-Datei erneut im Binärmodus.

### `Incomplete header`
Die `.cryptonn`-Datei ist unvollständig. Übertragen Sie die Datei erneut und prüfen Sie den verfügbaren Speicherplatz.

### `Decryption failed`
Der von der API zurückgegebene Schlüssel stimmt nicht mit den Verschlüsselungsparametern der Datei überein. Wenden Sie sich an den Softwareanbieter.

### `Temporary file could not be written`
Der PHP-Prozess hat keine Schreibberechtigung für das temporäre Systemverzeichnis. Prüfen Sie SELinux- oder AppArmor-Richtlinien.

---

## Leistung

| Cache-Schicht | Typische Latenz | Dauer |
|---|---|---|
| In-Process (APCu) | < 0,1 ms | 1 Stunde |
| Datei-Cache | < 0,5 ms | 24 Stunden |
| API-Aufruf (kalt) | 50 – 200 ms | Bei Cache-Miss |
| Toleranzzeit | < 0,5 ms | Bis zu 72 Stunden (nicht Trial) |

---

## Häufig gestellte Fragen

**F: Ist der Loader kostenlos?**  
A: Ja. Der CryptONN Loader ist kostenlos und Open-Source. Es gibt keinen Lizenzschlüssel oder Abonnement.

**F: Funktioniert er mit PHP OPcache?**  
A: Ja. OPcache arbeitet auf dem PHP-Bytecode, nachdem der Loader den Code entschlüsselt und ausgeführt hat.

**F: Was passiert bei einem API-Ausfall?**  
A: Nicht-Trial-Lizenzen arbeiten bis zu 72 Stunden aus dem Datei-Cache. Trial-Lizenzen erfordern eine erfolgreiche API-Antwort bei jedem Cache-Miss.

---

## Deinstallation

```bash
# 1. auto_prepend_file-Direktive entfernen und PHP neu starten
# 2. Loader-Verzeichnis löschen
rm -rf /opt/cryptonn
```

---

## Support

| Kanal | Link |
|---|---|
| Dokumentation | [laicos.com.tr](https://laicos.com.tr) |
| Issue-Tracker | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-extension/issues) |

---

*© 2026 LAICOS Technology. CryptONN ist ein Produkt von LAICOS Technology.*
