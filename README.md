# CryptONN PHP Loader

Free, keyless PHP loader for CryptONN-encoded files.

## Quick Install

```bash
curl -fsSL https://raw.githubusercontent.com/LAICOS-LTD/cryptonn-loader/main/install.sh | bash
```

## Manual Install

```bash
bash install.sh [--dir /opt/cryptonn] [--php /usr/bin/php] [--vhost /home/user/public_html]
```

## Requirements

- PHP 7.2+
- ext-sodium (bundled with PHP 7.2+)
- ext-openssl
- curl, bash

## Supported Panels

- cPanel (EasyApache 4)
- Plesk
- DirectAdmin
- Bare Linux server

## No License Key Required

The loader is free and keyless. License validation happens on the encoder side.
Each `.php` file encoded with CryptONN contains the encoder's license ID in its header.
The loader uses this ID to fetch the decryption key from the CryptONN API.

## Loader Location After Install

```
/opt/cryptonn/cryptonn-loader.php
```

Auto-configured via `auto_prepend_file` in `php.ini` or `.user.ini`.

## How It Works

1. Encoded `.php` files contain a binary header with the license ID
2. The loader intercepts `include`/`require` calls via `auto_prepend_file`
3. For each encoded file, the loader fetches the decryption key from `api.laicos.com.tr`
4. Keys are cached locally (APCu → file cache → 72-hour grace period)
5. Files are decrypted in-memory and executed — no plaintext ever written to disk

## Security

- No master key stored on the server
- Keys are delivered encrypted, bound to the server fingerprint (hostname + IP)
- AES-256-GCM encryption with per-file key derivation (BLAKE2b-256)