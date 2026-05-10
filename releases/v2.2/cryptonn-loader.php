<?php
/**
 * CryptONN Loader v2.2 — Free, Keyless, License-on-Encoder
 *
 * Güvenlik: Bu dosya hiçbir şifre anahtarı içermez.
 * Master key, encoded dosyanın header'ındaki license_id ile API'den alınır.
 * Kurulum için lisans gerekmez — loader tamamen ücretsizdir.
 *
 * Kurulum (php.ini / user.ini / .htaccess):
 *   auto_prepend_file = /opt/cryptonn/cryptonn-loader.php
 *
 * Gereksinimler: PHP 7.2+, ext-sodium (bundled), ext-openssl
 *
 * API URL override (opsiyonel):
 *   CRYPTONN_API_URL ortam değişkeni ile varsayılanı değiştirin.
 */

if (PHP_MAJOR_VERSION < 7 || (PHP_MAJOR_VERSION === 7 && PHP_MINOR_VERSION < 2)) {
    trigger_error('CryptONN Loader requires PHP 7.2+', E_USER_ERROR);
}
if (!extension_loaded('sodium')) {
    trigger_error('CryptONN Loader requires ext-sodium (bundled with PHP 7.2+)', E_USER_ERROR);
}
if (!extension_loaded('openssl')) {
    trigger_error('CryptONN Loader requires ext-openssl', E_USER_ERROR);
}

define('_CNN_API_URL',  rtrim(getenv('CRYPTONN_API_URL') ?: 'https://api.laicos.com.tr', '/'));
define('_CNN_MAGIC',    "CRYPTNN\x01\x00");  // 9 bytes
define('_CNN_HDR_LEN',  85);
define('_CNN_OFF_LID',  21);   // license_id: 32 bytes
define('_CNN_OFF_IV',   53);   // nonce: 12 bytes
define('_CNN_OFF_TAG',  65);   // tag: 16 bytes
define('_CNN_OFF_PLEN', 81);   // payload_size: 4 bytes LE

// ── Server fingerprint: SHA-256(hostname|server_ip) ──────────────────────────
function _cnn_fingerprint(): string {
    static $fp = null;
    if ($fp !== null) return $fp;
    $parts = [
        php_uname('n'),
        function_exists('gethostname') ? gethostname() : '',
        $_SERVER['SERVER_ADDR'] ?? '',
    ];
    $fp = hash('sha256', implode('|', $parts));
    return $fp;
}

// ── Decrypt an API key bundle using the delivery key ─────────────────────────
// delivery_key = SHA-256(license_id.toUpperCase() + "|" + server_fingerprint)
// Mirrors computeDeliveryKey() in api/src/utils/crypto.ts exactly.
function _cnn_decrypt_bundle(?array $bundle, string $delivery_key): ?string {
    if (!is_array($bundle)) return null;
    $enc = base64_decode($bundle['enc'] ?? '', true);
    $iv  = base64_decode($bundle['iv']  ?? '', true);
    $tag = base64_decode($bundle['tag'] ?? '', true);
    if ($enc === false || $iv === false || $tag === false) return null;
    if (strlen($iv) !== 12 || strlen($tag) !== 16) return null;

    $mk = openssl_decrypt($enc, 'aes-256-gcm', $delivery_key, OPENSSL_RAW_DATA, $iv, $tag);
    if ($mk === false || strlen($mk) !== 32) return null;
    return $mk;
}

// ── Fetch master key from API for a given license_id (3-layer cache) ──────────
//
// Monetizasyon encoder tarafında — loader ücretsiz, her sunucuya kurulur.
// Loader, encoded dosyanın header'ındaki license_id ile API'ye gider.
//
// Layer 1 — APCu (in-process, 1 saat):    hot path'de sıfır I/O
// Layer 2 — File cache (24 saat):         PHP process yeniden başlamasına dayanır
// Layer 3 — API çağrısı (POST /key/fetch): license geçerliliği doğrular
// Grace   — Stale file cache (72 saat):   API geçici olarak ulaşılamaz olursa
//
function _cnn_get_master_key(string $license_id): ?string {
    static $cache = [];
    if (isset($cache[$license_id])) return $cache[$license_id];

    $fp           = _cnn_fingerprint();
    $lid_upper    = strtoupper($license_id);
    $delivery_key = hash('sha256', $lid_upper . '|' . $fp, true);
    $lid_hash     = substr(hash('sha256', $lid_upper), 0, 16);
    $apcu_key     = '_cnn_mk_' . $lid_hash;
    $cache_file   = sys_get_temp_dir() . DIRECTORY_SEPARATOR . '_cnn_mk_' . $lid_hash . '.json';

    // ── Layer 1: APCu ────────────────────────────────────────────────────────
    if (function_exists('apcu_fetch')) {
        $cached = apcu_fetch($apcu_key, $hit);
        if ($hit && is_string($cached) && strlen($cached) === 32) {
            $cache[$license_id] = $cached;
            return $cached;
        }
    }

    // ── Layer 2: File cache (taze, ≤ 24 saat) ───────────────────────────────
    if (file_exists($cache_file)) {
        $stat = @stat($cache_file);
        if ($stat && (time() - $stat['mtime']) < 86400) {
            $raw = @file_get_contents($cache_file);
            if ($raw !== false) {
                $candidate = _cnn_decrypt_bundle(json_decode($raw, true), $delivery_key);
                if ($candidate !== null) {
                    $cache[$license_id] = $candidate;
                    if (function_exists('apcu_store')) apcu_store($apcu_key, $candidate, 3600);
                    return $candidate;
                }
            }
        }
    }

    // ── Layer 3: API çağrısı ─────────────────────────────────────────────────
    $post_body = json_encode([
        'license_id'         => $lid_upper,
        'server_fingerprint' => $fp,
        'loader_version'     => '2.2',
    ]);

    $ctx = stream_context_create([
        'http' => [
            'method'        => 'POST',
            'header'        => "Content-Type: application/json\r\n"
                             . "User-Agent: CryptONN-Loader/2.2 PHP/" . PHP_VERSION . "\r\n",
            'content'       => $post_body,
            'timeout'       => 8,
            'ignore_errors' => true,
        ],
        'ssl' => ['verify_peer' => true],
    ]);

    $resp = @file_get_contents(_CNN_API_URL . '/v1/key/fetch', false, $ctx);

    if ($resp !== false) {
        $data   = json_decode($resp, true);
        $bundle = is_array($data) && ($data['ok'] ?? false) === true
            ? ($data['data']['customer_key'] ?? null)
            : null;

        if (is_array($bundle)) {
            $candidate = _cnn_decrypt_bundle($bundle, $delivery_key);
            if ($candidate !== null) {
                @file_put_contents($cache_file, json_encode($bundle), LOCK_EX);
                if (function_exists('apcu_store')) apcu_store($apcu_key, $candidate, 3600);
                $cache[$license_id] = $candidate;
                return $candidate;
            }
        }
        // API yanıt verdi ama lisans geçersiz — stale cache'e düşme
        return null;
    }

    // ── Grace: API ulaşılamıyor, stale cache ≤ 72 saat ──────────────────────
    if (file_exists($cache_file)) {
        $stat = @stat($cache_file);
        if ($stat && (time() - $stat['mtime']) < 259200) {
            $raw = @file_get_contents($cache_file);
            if ($raw !== false) {
                $candidate = _cnn_decrypt_bundle(json_decode($raw, true), $delivery_key);
                if ($candidate !== null) {
                    $cache[$license_id] = $candidate;
                    if (function_exists('apcu_store')) apcu_store($apcu_key, $candidate, 3600);
                    return $candidate;
                }
            }
        }
    }

    return null;
}

// ── Main decode entry point (called from .cryptonn PHP stub) ─────────────────
function __cnn_load(string $file, int $offset): void {
    $fh = @fopen($file, 'rb');
    if ($fh === false) {
        trigger_error("CryptONN: Dosya açılamadı — {$file}", E_USER_ERROR);
        return;
    }

    if (fseek($fh, $offset) !== 0) {
        fclose($fh);
        trigger_error("CryptONN: Seek hatası — {$file}", E_USER_ERROR);
        return;
    }

    // __COMPILER_HALT_OFFSET__ points right after the ; in __halt_compiler();
    // The encoder writes a \n there before the binary header — skip it.
    $peek = fread($fh, 1);
    if ($peek === false) {
        fclose($fh);
        trigger_error("CryptONN: Eksik header — {$file}", E_USER_ERROR);
        return;
    }
    if ($peek !== "\n") {
        fseek($fh, $offset); // not a newline, seek back
    }

    $header = fread($fh, _CNN_HDR_LEN);
    if ($header === false || strlen($header) < _CNN_HDR_LEN) {
        fclose($fh);
        trigger_error("CryptONN: Eksik header — {$file}", E_USER_ERROR);
        return;
    }

    if (substr($header, 0, 9) !== _CNN_MAGIC) {
        fclose($fh);
        trigger_error("CryptONN: Geçersiz magic bytes — {$file}", E_USER_ERROR);
        return;
    }

    $license_id   = rtrim(substr($header, _CNN_OFF_LID,  32), "\x00");
    $iv           = substr($header, _CNN_OFF_IV,   12);
    $tag          = substr($header, _CNN_OFF_TAG,  16);
    $payload_size = unpack('V', substr($header, _CNN_OFF_PLEN, 4))[1];

    $encrypted = fread($fh, $payload_size);
    fclose($fh);

    if ($encrypted === false || strlen($encrypted) < $payload_size) {
        trigger_error("CryptONN: Eksik payload — {$file}", E_USER_ERROR);
        return;
    }

    $mk = _cnn_get_master_key($license_id);
    if ($mk === null) {
        trigger_error("CryptONN: Master key alınamadı (lisans: {$license_id}) — {$file}", E_USER_ERROR);
        return;
    }

    // Key derivation: BLAKE2b-256 MAC — Rust encoder ile birebir aynı
    // info = "cryptonn:v1:{license_id}:{basename}"
    $info     = "cryptonn:v1:{$license_id}:" . basename($file);
    $file_key = sodium_crypto_generichash($info, $mk, 32);

    $decrypted = openssl_decrypt($encrypted, 'aes-256-gcm', $file_key, OPENSSL_RAW_DATA, $iv, $tag);
    if ($decrypted === false) {
        trigger_error("CryptONN: Şifre çözme başarısız — {$file}", E_USER_ERROR);
        return;
    }

    // Temp dosyayı orijinal dosyayla aynı dizine yaz: __DIR__ ve __FILE__
    // sabitleri decode edilmiş kodda doğru çalışsın.
    $tmp_name = '_cnn_' . hash('sha256', $file . $offset) . '.php';
    $orig_dir = dirname($file);
    $tmp = (is_writable($orig_dir) ? $orig_dir : sys_get_temp_dir())
         . DIRECTORY_SEPARATOR . $tmp_name;

    if (@file_put_contents($tmp, $decrypted, LOCK_EX) === false) {
        trigger_error("CryptONN: Geçici dosya yazılamadı", E_USER_ERROR);
        return;
    }

    try {
        include $tmp;
    } finally {
        @unlink($tmp);
    }
}
