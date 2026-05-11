# CryptONN PHP Code Protection & Licensing System

**Languages:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **The CryptONN Loader is free and requires no license key.** Licensing operates at the encoding stage, not at the loader stage. Install once per server and it handles all protected applications automatically.

---

## What is CryptONN?

CryptONN is a professional PHP source code protection and software licensing platform built for independent software vendors (ISVs) and development teams that distribute PHP applications commercially. It transforms PHP source files into an opaque, encrypted binary format that is fundamentally resistant to reverse engineering, decompilation, and unauthorized redistribution — while preserving full runtime performance and compatibility with standard PHP infrastructure.

The system consists of two components: the **CryptONN Encoder** (a desktop application used by the developer to protect PHP files) and the **CryptONN Loader** (this repository — a single PHP file installed on the end customer's server to transparently execute protected files).

---

## Problems Solved

| Problem | How CryptONN Addresses It |
|---|---|
| **Source code theft** | PHP logic is transformed into an encrypted binary payload. Even with full filesystem access, the original source cannot be reconstructed. |
| **Unauthorized deployment** | Each protected file carries an embedded license identifier validated server-side. Files copied to unlicensed servers refuse to execute. |
| **License term enforcement** | Trial periods and expiry dates are enforced on the licensing server. There are no client-side checks that can be bypassed. |
| **Multi-tenant distribution** | The same codebase can be licensed to multiple customers, each with unique terms, usage limits, and domain restrictions. |
| **Unauthorized redistribution** | Protected files are bound to specific license identifiers — they are useless without an active, valid license. |

---

## How It Works

**At encoding time** (developer's machine):
A PHP file is processed by the CryptONN Encoder. The output is a `.cryptonn` binary file containing an encrypted payload and an embedded license identifier. The original PHP source code is not present in the output in any form.

**At runtime** (customer's server):
1. PHP attempts to execute a `.cryptonn` file
2. The Loader intercepts the execution via the PHP `auto_prepend_file` mechanism
3. The Loader reads the embedded license identifier from the file header
4. The Loader contacts the CryptONN licensing API, presenting the license identifier and a unique fingerprint derived from the server's network identity
5. The API validates the license and returns a decryption key, encrypted specifically for this server
6. The Loader decrypts the PHP payload entirely in memory
7. The decrypted PHP code executes natively — no temporary files containing source code are retained on disk

This flow is transparent to the end user and requires no modifications to the application's code structure.

---

## Security Model

| Property | Detail |
|---|---|
| **Keys stored on server** | None. No cryptographic keys are stored in the filesystem or environment. |
| **Server binding** | Each server has a unique fingerprint derived from its network identity. A decryption bundle valid for one server is cryptographically useless on any other. |
| **API communication** | All key delivery occurs over encrypted HTTPS channels. Keys are additionally encrypted with a server-specific wrapping key before transmission. |
| **Offline tolerance** | A three-layer cache (in-process → file-based 24h → grace period 72h) ensures operation during temporary API outages. |
| **Trial enforcement** | Trial licenses enforce hard server-side expiry. No offline grace period applies — the API must confirm validity for trial licenses on every cache miss. |
| **Tamper detection** | Truncated, modified, or corrupt `.cryptonn` files are detected and rejected before any decryption attempt. |
| **No plaintext on disk** | Decrypted PHP code is never written to a persistent location. Temporary execution files are deleted immediately after use. |

---

## PHP Compatibility

| PHP Version | Status |
|---|---|
| PHP 7.2 | ✅ Fully supported |
| PHP 7.3 | ✅ Fully supported |
| PHP 7.4 | ✅ Fully supported |
| PHP 8.0 | ✅ Fully supported |
| PHP 8.1 | ✅ Fully supported |
| PHP 8.2 | ✅ Fully supported |
| PHP 8.3 | ✅ Fully supported |
| PHP 8.4 | ✅ Fully supported |
| PHP 8.5 | ✅ Fully supported |
| PHP 5.x · 7.0 · 7.1 | ❌ Not supported |

---

## System Requirements

| Component | Requirement | Notes |
|---|---|---|
| PHP | 7.2 – 8.5 | All minor versions supported |
| ext-sodium | Any version | Bundled with PHP 7.2+ — no separate installation required on modern systems |
| ext-openssl | Any version | Available by default on virtually all hosting environments |
| Outbound HTTPS | Port 443 | Required for license validation API calls |
| Disk (cache) | ~10 KB per active license | Temporary files in system temp directory |
| APCu (optional) | Any version | Enables in-process caching; significantly reduces cold-start latency |

---

## Installation

### Step 1 — Download the Loader

```bash
sudo mkdir -p /opt/cryptonn
sudo curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/cryptonn-loader.php \
     -o /opt/cryptonn/cryptonn-loader.php
sudo chmod 644 /opt/cryptonn/cryptonn-loader.php
sudo chown root:root /opt/cryptonn/cryptonn-loader.php
```

### Step 2 — Configure PHP (choose your environment)

**cPanel / EasyApache 4**
```bash
# Replace XX with your PHP version (e.g., ea-php82)
echo "auto_prepend_file = /opt/cryptonn/cryptonn-loader.php" \
  >> /opt/cpanel/ea-phpXX/root/etc/php.ini
/scripts/restartsrv_apache
/scripts/restartsrv_php_fpm
```

**Plesk / DirectAdmin — per-site `.user.ini`**
```ini
auto_prepend_file = /opt/cryptonn/cryptonn-loader.php
```

**Bare Metal — nginx + PHP-FPM pool config**
```ini
; /etc/php/8.x/fpm/pool.d/www.conf
php_admin_value[auto_prepend_file] = /opt/cryptonn/cryptonn-loader.php
```
```bash
systemctl restart php8.2-fpm
```

**Apache — `.htaccess` (per-directory)**
```apache
php_value auto_prepend_file /opt/cryptonn/cryptonn-loader.php
```

### Step 3 — Verify the Installation

Save the following as `/tmp/cnn-verify.php` and run it:

```php
<?php
echo defined('_CNN_MAGIC') ? "✅ CryptONN Loader: Active\n" : "❌ CryptONN Loader: Not loaded\n";
echo "PHP Version : " . PHP_VERSION . "\n";
echo "ext-sodium  : " . (extension_loaded('sodium')  ? "✅" : "❌ MISSING") . "\n";
echo "ext-openssl : " . (extension_loaded('openssl') ? "✅" : "❌ MISSING") . "\n";
echo "APCu        : " . (function_exists('apcu_store') ? "✅ Available" : "— Not available (optional)") . "\n";
```

```bash
php /tmp/cnn-verify.php
```

Expected output on a correctly configured server:
```
✅ CryptONN Loader: Active
PHP Version : 8.2.x
ext-sodium  : ✅
ext-openssl : ✅
APCu        : ✅ Available
```

---

## Troubleshooting

### `CryptONN Loader requires ext-sodium`
**Cause:** The `sodium` PHP extension is not enabled for the active PHP version.

```bash
# cPanel / EasyApache 4
/scripts/install_ea_metapackage ea-php82-php-sodium

# AlmaLinux / RHEL / CentOS 8+
dnf install php-sodium

# Ubuntu / Debian
apt-get install php8.2-sodium

# Verify
php -m | grep sodium
```

---

### `CryptONN Loader requires ext-openssl`
**Cause:** The `openssl` extension is not enabled.

Enable `extension=openssl` in the relevant `php.ini`, or install the `php-openssl` package via your package manager.

---

### `Master key could not be retrieved`
**Cause:** The Loader cannot reach the CryptONN licensing API. This may be caused by a firewall rule blocking outbound HTTPS, a DNS resolution failure, or a temporary network issue.

```bash
# Diagnose connectivity
curl -sv --max-time 10 https://api.laicos.com.tr/health
```

**Solutions:**
- Ensure outbound TCP port 443 is permitted from the server
- Verify that the server can resolve external DNS names
- If behind an egress proxy, configure the `CRYPTONN_API_URL` environment variable

---

### `Invalid magic bytes`
**Cause:** The file is not a valid CryptONN-encoded file, or the file was corrupted during transfer (e.g., transferred in text mode instead of binary mode).

**Solution:** Re-transfer the `.cryptonn` file in binary mode. Do not open or edit the file with a text editor. Request a fresh copy from the software vendor if corruption is suspected.

---

### `Incomplete header`
**Cause:** The `.cryptonn` file is truncated — it was not transferred completely.

**Solution:** Re-transfer the file. Verify available disk space on both the source and destination. Check for upload size limits in your web server or PHP configuration.

---

### `Decryption failed`
**Cause:** The decryption key returned by the API does not match the file's encryption parameters. This typically indicates that the file was encoded with a different license than the one active on this server.

**Solution:** Confirm with the software vendor that the correct license identifier was used when encoding the file.

---

### `Temporary file could not be written`
**Cause:** The PHP process does not have write permission to the system temporary directory or to the application's directory.

**Solution:** Ensure the web server user has write access to `sys_get_temp_dir()` (typically `/tmp`). Check SELinux or AppArmor policies if running on a hardened system.

---

### Loader is active but `.cryptonn` files are not executing
**Possible causes:**
- `auto_prepend_file` is not applying to the correct PHP SAPI (CLI vs. FPM)
- A `.user.ini` file is being served from cache (`user_ini.cache_ttl` defaults to 300 seconds — wait and retry)
- The application is invoking PHP files via `include`/`require` with a path that bypasses the prepend

**Diagnosis:**
```bash
php -r "echo ini_get('auto_prepend_file');"
```

---

## Performance

The Loader is designed for negligible overhead on warm requests:

| Cache Layer | Typical Latency | Duration |
|---|---|---|
| In-process (APCu) | < 0.1 ms | 1 hour |
| File cache | < 0.5 ms | 24 hours |
| API call (cold) | 50 – 200 ms | On cache miss |
| Grace period | < 0.5 ms | Up to 72 hours (non-trial) |

APCu is used automatically when available. No configuration is required.

---

## Frequently Asked Questions

**Q: Is the Loader free to use?**  
A: Yes. The CryptONN Loader is free and open-source. There is no license key, subscription, or fee associated with installing it on any number of servers.

**Q: Does it work with PHP OPcache?**  
A: Yes. OPcache operates on the PHP bytecode after the Loader has decrypted and executed the code. The interaction is fully transparent and correct.

**Q: Can one Loader installation serve multiple applications?**  
A: Yes. A single Loader installation handles all `.cryptonn` files on the server, across all applications and PHP versions that reference it via `auto_prepend_file`.

**Q: What happens during an API outage?**  
A: Non-trial licenses continue to operate normally for up to 72 hours using the file-based cache. Trial licenses require a successful API response on every cache miss — they do not benefit from the grace period.

**Q: Is cached data a security risk?**  
A: No. The cached bundle is encrypted with a key derived from the server's unique fingerprint. It cannot be decrypted on any other machine, and it does not contain the raw decryption key in any usable form.

**Q: Can the Loader be used on shared hosting?**  
A: Yes, provided the hosting environment permits `auto_prepend_file` to be set via `.user.ini` or `.htaccess`, and outbound HTTPS connections are allowed.

**Q: Does CryptONN support wildcard or multi-domain licenses?**  
A: License terms, domain restrictions, and deployment limits are managed through the CryptONN platform by the software vendor. Contact your software vendor for the terms applicable to your license.

---

## Removing the Loader

1. Remove the `auto_prepend_file` directive from `php.ini`, `.user.ini`, or `.htaccess`
2. Restart PHP-FPM or Apache
3. Delete the loader directory:
```bash
rm -rf /opt/cryptonn
```

This does not affect any `.cryptonn` files — they simply revert to being unexecutable until a Loader is reinstalled.

---

## Support

| Channel | Link |
|---|---|
| Documentation | [docs.laicos.com.tr](https://laicos.com.tr) |
| Commercial Licensing | [laicos.com.tr](https://laicos.com.tr) |
| Issue Tracker | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-loader/issues) |

---

*© 2026 LAICOS Technology. CryptONN is a product of LAICOS Technology.*
