# CryptONN PHP Code Protection & Licensing System

**Languages:** [English](README.md) · [Türkçe](README.tr.md) · [Deutsch](README.de.md) · [Français](README.fr.md) · [Español](README.es.md) · [Português](README.pt.md) · [Русский](README.ru.md) · [العربية](README.ar.md) · [Polski](README.pl.md) · [Nederlands](README.nl.md)

---

> **The CryptONN Loader is free and requires no license key.** Licensing is enforced at the encoding stage, not the loader stage. Install once per server and it handles all protected applications automatically.

---

## What is CryptONN?

CryptONN is a professional PHP source code protection and software licensing platform built for independent software vendors (ISVs) and development teams that distribute PHP applications commercially. It transforms PHP source files into an opaque, AES-256-GCM encrypted binary format that is fundamentally resistant to reverse engineering, decompilation, and unauthorized redistribution — while preserving full runtime performance and compatibility with standard PHP infrastructure.

The system consists of two components: the **CryptONN Encoder** (a desktop application used by the developer to protect PHP files) and the **CryptONN Extension** (this repository — a native PHP extension installed on the end customer's server via a single installer script, with no manual configuration required).

---

## Quick Install

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh)
```

The installer automatically detects all installed PHP versions (Plesk, cPanel/EasyApache 4, DirectAdmin, bare Linux), downloads the correct `.so` binary for each version, installs it into the PHP extension directory, and adds `extension=cryptonn` to each `php.ini`. No manual configuration is required.

**Supported environments:** Debian · Ubuntu · AlmaLinux · RHEL · CentOS · Plesk · cPanel/EasyApache 4 · DirectAdmin

---

## Installer Commands

| Command | Description |
|---|---|
| `(none)` | Install or update to the latest version (default) |
| `--install` | Force reinstall even if already up to date |
| `--update` | Upgrade if a newer version is available |
| `--uninstall` | Remove the extension from all PHP versions |
| `--status` | Show installation status and version for each PHP |
| `--help` | Show usage help |

```bash
# Check status and installed versions
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --status

# Uninstall from all PHP versions
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --uninstall
```

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
A PHP file is processed by the CryptONN Encoder. The output is a `.cryptonn` file — a valid PHP file containing a minimal stub header followed by an AES-256-GCM encrypted binary payload. The stub calls `__cnn_load()` (provided by the extension) with the file path and payload offset. The original PHP source code is not present in any readable form.

**At runtime** (customer's server):
1. PHP executes a `.cryptonn` file normally (`include`, `require`, or direct invocation)
2. The stub header runs: `__cnn_load(__FILE__, __COMPILER_HALT_OFFSET__)`
3. The extension reads the embedded license identifier from the binary payload header
4. The extension contacts the CryptONN licensing API, presenting the license identifier and a unique fingerprint derived from the server's network identity
5. The API validates the license and returns a decryption key encrypted specifically for this server
6. The extension decrypts the PHP payload in memory, executes it, and immediately removes the temporary execution file

No `auto_prepend_file` configuration is required. The extension is loaded automatically by PHP at startup once `extension=cryptonn` is present in `php.ini`.

---

## Security Model

| Property | Detail |
|---|---|
| **Keys stored on server** | None. No cryptographic keys are stored in the filesystem or environment. |
| **Server binding** | Each server has a unique fingerprint derived from its network identity. A decryption bundle valid for one server is cryptographically useless on any other. |
| **API communication** | All key delivery occurs over encrypted HTTPS channels. Keys are additionally encrypted with a server-specific wrapping key before transmission. |
| **Offline tolerance** | A three-layer cache (in-process → file-based 24 h → grace period 72 h) ensures operation during temporary API outages. |
| **Trial enforcement** | Trial licenses enforce hard server-side expiry. No offline grace period applies — the API must confirm validity for trial licenses on every cache miss. |
| **Tamper detection** | Truncated, modified, or corrupt `.cryptonn` files are detected and rejected before any decryption attempt. |
| **No plaintext on disk** | Decrypted PHP code is executed via a temporary file that is deleted immediately after use. |

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
| PHP 8.4 | 🔜 Planned |
| PHP 5.x · 7.0 · 7.1 | ❌ Not supported |

---

## System Requirements

| Component | Requirement | Notes |
|---|---|---|
| PHP | 7.2 – 8.3 | All minor versions supported |
| Architecture | x86_64 · aarch64 | Pre-built binaries for both |
| bash | 4.0+ | Required by the installer script |
| curl | Any | Used by installer to download binaries |
| sha256sum | Any | Binary integrity verification |
| Outbound HTTPS | Port 443 | Required for license validation API calls |
| APCu (optional) | Any | Enables in-process caching; reduces cold-start latency |

---

## Verify Installation

```bash
# Visual status table with version info
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --status

# Quick CLI checks
php -m | grep cryptonn
php -r "echo phpversion('cryptonn');"
```

Expected output:
```
cryptonn
1.0.0
```

---

## Troubleshooting

### Extension not loading after install

```bash
# Check that the extension appears in the module list
php -m | grep cryptonn

# Find the php.ini that PHP-FPM uses
php-fpm8.2 -i | grep "Loaded Configuration"

# Verify the directive is present
grep cryptonn /etc/php/8.2/fpm/php.ini
```

If the directive is present but the extension does not load, restart PHP-FPM:

```bash
systemctl restart plesk-php82-fpm   # Plesk
/scripts/restartsrv_php_fpm         # cPanel
systemctl restart php8.2-fpm        # bare Linux
```

---

### Cannot reach the licensing API

```bash
curl -sv --max-time 10 https://api.laicos.com.tr/health
```

Ensure outbound TCP port 443 is permitted from the server and that external DNS names can be resolved. If an egress proxy is required, set the `CRYPTONN_API_URL` environment variable to point to the proxy.

---

### `Invalid magic bytes`

The `.cryptonn` file is not a valid CryptONN-encoded file, or it was corrupted during transfer (transferred in text mode instead of binary, or opened with a text editor). Re-transfer the file in binary mode and request a fresh copy from the software vendor if corruption is suspected.

---

### `Incomplete header`

The `.cryptonn` file is truncated — it was not transferred completely. Re-transfer the file and verify available disk space on both source and destination.

---

### `Decryption failed`

The API returned a key that does not match the file's encryption parameters. This typically means the file was encoded with a different license than the one currently active on this server. Contact the software vendor with the license identifier shown in the error.

---

## Performance

The extension is designed for negligible overhead on warm requests:

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
A: Yes. The CryptONN Extension is free and open-source. There is no license key, subscription, or fee associated with installing it on any number of servers.

**Q: Does it work with PHP OPcache?**
A: Yes. OPcache operates on the PHP bytecode after the extension has decrypted and executed the code. The interaction is fully transparent and correct.

**Q: Can one installation serve multiple applications?**
A: Yes. A single extension installation handles all `.cryptonn` files on the server, across all applications and PHP versions where `extension=cryptonn` is active.

**Q: What happens during an API outage?**
A: Non-trial licenses continue to operate normally for up to 72 hours using the file-based grace cache. Trial licenses require a successful API response on every cache miss — they do not benefit from the grace period.

**Q: Is cached data a security risk?**
A: No. The cached bundle is encrypted with a key derived from the server's unique fingerprint. It cannot be decrypted on any other machine.

**Q: Can the extension be used on shared hosting?**
A: It requires the ability to install a PHP extension (`.so` file) and write to `php.ini`. This is typically available on VPS and dedicated servers, Plesk, and cPanel environments. Standard shared hosting without extension installation rights is not supported.

---

## Uninstall

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh) --uninstall
```

Removes `cryptonn.so` from all PHP extension directories, strips `extension=cryptonn` from each `php.ini`, and restarts PHP-FPM services automatically on Plesk. Existing `.cryptonn` files are not affected — they simply revert to being unexecutable until the extension is reinstalled.

---

## Support

| Channel | Link |
|---|---|
| Documentation | [docs.laicos.com.tr](https://laicos.com.tr) |
| Commercial Licensing | [laicos.com.tr](https://laicos.com.tr) |
| Issue Tracker | [GitHub Issues](https://github.com/LAICOS-LTD/cryptonn-loader/issues) |

---

*© 2026 LAICOS Technology. CryptONN is a product of LAICOS Technology.*