/*
 * CryptONN PHP Extension v1.2.0
 *
 * Replaces the PHP loader (cryptonn-loader.php) with a compiled binary extension.
 * Hooks zend_compile_file to intercept .cryptonn encoded files before PHP parses them.
 *
 * Supports: PHP 7.2 – 8.5
 * Arch:     x86_64, aarch64
 * Crypto:   AES-256-GCM (OpenSSL) + BLAKE2b-256 MAC (libsodium)
 * Network:  libcurl (POST /v1/key/fetch)
 * Cache:    static per-process + file (/tmp) + APCu (optional)
 *
 * Build:    phpize && ./configure --enable-cryptonn && make
 * Install:  extension = cryptonn.so  (php.ini)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_compile.h"
#include "zend_stream.h"
#include "SAPI.h"
#include "php_cryptonn.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sodium.h>
#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

/* ── Portable u32 LE read ────────────────────────────────────────────────── */
static uint32_t read_u32le(const unsigned char *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* ── SHA-256 via EVP — compatible with OpenSSL 1.1 and 3.x ─────────────── */
static void cnn_sha256(const void *data, size_t len, unsigned char out[32]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx) {
        unsigned int outlen = 32;
        EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(ctx, data, len);
        EVP_DigestFinal_ex(ctx, out, &outlen);
        EVP_MD_CTX_free(ctx);
    }
}

/* ── PHP INI entries ────────────────────────────────────────────────────── */
PHP_INI_BEGIN()
    PHP_INI_ENTRY(CNN_MACHINE_KEY_INI, "", PHP_INI_SYSTEM, NULL)
PHP_INI_END()

/* ── Machine secret: SHA-256 cache layer key ─────────────────────────────── */
/* Reads cryptonn.machine_key (64 hex chars) from php.ini, or falls back to  */
/* /etc/cryptonn/machine.key file. If neither exists, all-zero key is used   */
/* (degrades to old behaviour — run installer to generate machine secret).   */
static void cnn_machine_secret(unsigned char out[32]) {
    memset(out, 0, 32);

    /* Try php.ini first */
    const char *ini_val = INI_STR(CNN_MACHINE_KEY_INI);
    if (ini_val && strlen(ini_val) == 64) {
        int ok = 1;
        for (int i = 0; i < 32; i++) {
            char hi = ini_val[i*2], lo = ini_val[i*2+1];
            int h = (hi>='0'&&hi<='9') ? hi-'0' : (hi>='a'&&hi<='f') ? hi-'a'+10 : (hi>='A'&&hi<='F') ? hi-'A'+10 : -1;
            int l = (lo>='0'&&lo<='9') ? lo-'0' : (lo>='a'&&lo<='f') ? lo-'a'+10 : (lo>='A'&&lo<='F') ? lo-'A'+10 : -1;
            if (h < 0 || l < 0) { ok = 0; break; }
            out[i] = (unsigned char)((h << 4) | l);
        }
        if (ok) return;
    }

    /* Fall back to file */
    FILE *f = fopen(CNN_MACHINE_KEY_FILE, "r");
    if (!f) return;
    char hex[65] = {0};
    size_t n = fread(hex, 1, 64, f);
    fclose(f);
    if (n != 64) return;
    for (int i = 0; i < 32; i++) {
        char hi = hex[i*2], lo = hex[i*2+1];
        int h = (hi>='0'&&hi<='9') ? hi-'0' : (hi>='a'&&hi<='f') ? hi-'a'+10 : (hi>='A'&&hi<='F') ? hi-'A'+10 : -1;
        int l = (lo>='0'&&lo<='9') ? lo-'0' : (lo>='a'&&lo<='f') ? lo-'a'+10 : (lo>='A'&&lo<='F') ? lo-'A'+10 : -1;
        if (h < 0 || l < 0) { memset(out, 0, 32); return; }
        out[i] = (unsigned char)((h << 4) | l);
    }
}

/* ── Cache protection key: SHA-256(machine_secret | lid | fp) ─────────────── */
/* Used only for local /tmp storage — API protocol is unchanged.              */
static void cnn_cache_protect_key(
    const unsigned char *machine_secret,
    const char          *lid_upper,
    const char          *fp_hex,
    unsigned char        out[32]
) {
    unsigned char buf[32 + 33 + 64 + 2];
    int pos = 0;
    memcpy(buf + pos, machine_secret, 32);  pos += 32;
    buf[pos++] = '|';
    int llen = (int)strlen(lid_upper);
    if (llen > 32) llen = 32;
    memcpy(buf + pos, lid_upper, llen);     pos += llen;
    buf[pos++] = '|';
    memcpy(buf + pos, fp_hex, 64);          pos += 64;
    cnn_sha256(buf, pos, out);
}

/* ── In-process key cache (per PHP worker, cleared on restart) ───────────── */
#define CNN_PROC_CACHE_SIZE 64
typedef struct {
    char    license_id[33];   /* NUL-terminated */
    char    master_key[32];
    int     valid;
} cnn_proc_entry_t;

static cnn_proc_entry_t s_proc_cache[CNN_PROC_CACHE_SIZE];
static int              s_cache_init = 0;

static void proc_cache_init(void) {
    if (!s_cache_init) {
        memset(s_proc_cache, 0, sizeof(s_proc_cache));
        s_cache_init = 1;
    }
}

static const char *proc_cache_get(const char *lid) {
    proc_cache_init();
    for (int i = 0; i < CNN_PROC_CACHE_SIZE; i++) {
        if (s_proc_cache[i].valid && strcmp(s_proc_cache[i].license_id, lid) == 0)
            return s_proc_cache[i].master_key;
    }
    return NULL;
}

static void proc_cache_set(const char *lid, const char *mk) {
    proc_cache_init();
    /* find empty or oldest slot (simple round-robin on index) */
    static int s_next = 0;
    strncpy(s_proc_cache[s_next].license_id, lid, 32);
    s_proc_cache[s_next].license_id[32] = '\0';
    memcpy(s_proc_cache[s_next].master_key, mk, 32);
    s_proc_cache[s_next].valid = 1;
    s_next = (s_next + 1) % CNN_PROC_CACHE_SIZE;
}

/* ── Server fingerprint: SHA-256(hostname|server_addr) ───────────────────── */
static void cnn_fingerprint(char out_hex[65]) {
    char buf[512] = {0};
    char hostname[256] = {0};
    gethostname(hostname, sizeof(hostname) - 1);

    const char *server_addr = getenv("SERVER_ADDR");
    if (!server_addr) server_addr = "";

    snprintf(buf, sizeof(buf), "%s|%s", hostname, server_addr);

    unsigned char digest[32];
    cnn_sha256(buf, strlen(buf), digest);

    for (int i = 0; i < 32; i++)
        sprintf(out_hex + i * 2, "%02x", digest[i]);
    out_hex[64] = '\0';
}

/* ── Delivery key: SHA-256(license_id_upper + "|" + fingerprint) ─────────── */
static void cnn_delivery_key(const char *lid_upper, const char *fp, unsigned char out[32]) {
    char buf[512] = {0};
    snprintf(buf, sizeof(buf), "%s|%s", lid_upper, fp);
    cnn_sha256(buf, strlen(buf), out);
}

/* ── File-cache path: /tmp/_cnn_mk_<first16hex(sha256(lid))>.json ────────── */
static void cnn_cache_path(const char *lid_upper, char out[256]) {
    unsigned char digest[32];
    cnn_sha256(lid_upper, strlen(lid_upper), digest);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i*2, "%02x", digest[i]);
    hex[64] = '\0';
    snprintf(out, 256, "%s/_cnn_mk_%.16s.json", P_tmpdir ? P_tmpdir : "/tmp", hex);
}

/* ── AES-256-GCM decrypt bundle (delivery-key encrypted) ────────────────── */
/* bundle_enc/iv/tag are base64-encoded strings from the JSON response.       */
/* aad: additional authenticated data — equals trial_ends_at as decimal str,  */
/*      or "" for non-trial licenses.                                          */
static int cnn_decrypt_bundle(
    const unsigned char *enc, int enc_len,
    const unsigned char *iv,  int iv_len,
    const unsigned char *tag, int tag_len,
    const unsigned char *delivery_key,
    const char          *aad,
    unsigned char        out[32]
) {
    if (iv_len != 12 || tag_len != 16) return 0;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return 0;

    int ok = 0, outl = 0, outl2 = 0;
    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) goto done;
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv_len, NULL)) goto done;
    if (!EVP_DecryptInit_ex(ctx, NULL, NULL, delivery_key, iv)) goto done;

    /* AAD covers trial_ends_at — tamper-evident */
    if (aad && strlen(aad) > 0) {
        if (!EVP_DecryptUpdate(ctx, NULL, &outl, (unsigned char *)aad, (int)strlen(aad)))
            goto done;
    }

    if (!EVP_DecryptUpdate(ctx, out, &outl, enc, enc_len)) goto done;
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag)) goto done;
    if (EVP_DecryptFinal_ex(ctx, out + outl, &outl2) <= 0) goto done;

    ok = (outl + outl2 == 32);
done:
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

/* ── Minimal base64 decode ────────────────────────────────────────────────── */
static const int B64T[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static int b64_decode(const char *src, unsigned char *dst, int dst_max) {
    int len = 0;
    unsigned int acc = 0, bits = 0;
    for (; *src; src++) {
        int v = B64T[(unsigned char)*src];
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (len >= dst_max) return -1;
            dst[len++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    return len;
}

/* ── Minimal JSON field extractor ─────────────────────────────────────────── */
/* Extracts a quoted string value for a given key from flat JSON.             */
static int json_str(const char *json, const char *key, char *out, int out_max) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < out_max - 1)
        out[n++] = *p++;
    out[n] = '\0';
    return n > 0;
}

/* Extracts a numeric value (integer or null) for a given key. Returns 0 if null/missing. */
static long json_long(const char *json, const char *key, int *found) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (found) *found = 0;
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (strncmp(p, "null", 4) == 0) return 0;
    if (*p < '0' || *p > '9') return 0;
    if (found) *found = 1;
    return atol(p);
}

/* Extracts the "code" field from {"error":{"code":"VALUE"}} */
static int json_error_code(const char *json, char *out, int out_max) {
    const char *p = strstr(json, "\"error\"");
    if (!p) return 0;
    return json_str(p, "code", out, out_max);
}

/* ── libcurl write callback ───────────────────────────────────────────────── */
typedef struct { char *buf; size_t len; size_t cap; } cnn_buf_t;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    cnn_buf_t *b = (cnn_buf_t *)userdata;
    size_t add = size * nmemb;
    if (b->len + add + 1 > b->cap) {
        size_t new_cap = b->cap + add + 4096;
        char *tmp = realloc(b->buf, new_cap);
        if (!tmp) return 0;
        b->buf = tmp;
        b->cap = new_cap;
    }
    memcpy(b->buf + b->len, ptr, add);
    b->len += add;
    b->buf[b->len] = '\0';
    return add;
}

/* ── Parse a key bundle from JSON and decrypt with delivery key + AAD ─────── */
/* Returns 1 on success, 0 on failure. Writes 32 bytes to master_key_out.    */
/* aad: "" for non-trial, decimal trial_ends_at string for trial.            */
static int cnn_parse_and_decrypt_bundle(
    const char    *json_bundle, /* JSON object containing enc/iv/tag */
    const unsigned char *delivery_key,
    const char    *aad,
    unsigned char  master_key_out[32]
) {
    char enc_b64[512] = {0}, iv_b64[64] = {0}, tag_b64[64] = {0};
    if (!json_str(json_bundle, "enc", enc_b64, sizeof(enc_b64))) return 0;
    if (!json_str(json_bundle, "iv",  iv_b64,  sizeof(iv_b64)))  return 0;
    if (!json_str(json_bundle, "tag", tag_b64, sizeof(tag_b64))) return 0;

    unsigned char enc[512], iv[16], tag[20];
    int enc_len = b64_decode(enc_b64, enc, sizeof(enc));
    int iv_len  = b64_decode(iv_b64,  iv,  sizeof(iv));
    int tag_len = b64_decode(tag_b64, tag, sizeof(tag));
    if (enc_len <= 0 || iv_len != 12 || tag_len != 16) return 0;

    return cnn_decrypt_bundle(enc, enc_len, iv, iv_len, tag, tag_len,
                              delivery_key, aad, master_key_out);
}

/* ── File cache: read (machine-secret protected) ─────────────────────────── */
/* Outer layer: decrypt CNN2 envelope with cache_key (machine secret bound). */
/* Inner layer: decrypt customer_key bundle with delivery_key (API protocol).*/
static int cnn_file_cache_read(
    const char          *cache_path,
    const unsigned char *delivery_key,
    const unsigned char *cache_key,
    unsigned char        master_key_out[32],
    int                 *trial_expired,
    int                  grace
) {
    *trial_expired = 0;
    struct stat st;
    if (stat(cache_path, &st) != 0) return 0;

    time_t age = time(NULL) - st.st_mtime;
    long max_age = grace ? CNN_CACHE_TTL_GRACE : CNN_CACHE_TTL_FRESH;
    if (age > max_age) return 0;

    FILE *f = fopen(cache_path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz < 4) { fclose(f); return 0; }

    unsigned char magic[4];
    fread(magic, 1, 4, f);

    char *json = NULL;

    if (magic[0]=='C' && magic[1]=='N' && magic[2]=='N' && magic[3]=='2') {
        /* New format: CNN2 + iv(12) + tag(16) + enc_len(4) + enc */
        if (sz < 4 + 12 + 16 + 4) { fclose(f); return 0; }
        unsigned char iv[12], tag[16];
        uint32_t enc_len;
        fread(iv,  1, 12, f);
        fread(tag, 1, 16, f);
        fread(&enc_len, 1, 4, f);
        if (enc_len == 0 || enc_len > 16384) { fclose(f); return 0; }
        unsigned char *enc = malloc(enc_len);
        unsigned char *plain = malloc(enc_len + 1);
        if (!enc || !plain) { free(enc); free(plain); fclose(f); return 0; }
        fread(enc, 1, enc_len, f);
        fclose(f);

        /* Decrypt outer layer with cache_key */
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        int outl = 0, outl2 = 0, ok = 0;
        if (ctx) {
            if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) &&
                EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) &&
                EVP_DecryptInit_ex(ctx, NULL, NULL, cache_key, iv) &&
                EVP_DecryptUpdate(ctx, plain, &outl, enc, enc_len) &&
                EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag) &&
                EVP_DecryptFinal_ex(ctx, plain + outl, &outl2) > 0) {
                plain[outl + outl2] = '\0';
                ok = 1;
            }
            EVP_CIPHER_CTX_free(ctx);
        }
        free(enc);
        if (!ok) { free(plain); return 0; }
        json = (char *)plain;
    } else {
        /* Legacy format (plain JSON, no machine secret) — still readable */
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 16384) {
            json = malloc(sz + 1);
            if (json) { fread(json, 1, sz, f); json[sz] = '\0'; }
        }
        fclose(f);
        if (!json) return 0;
    }

    /* Trial expiry check */
    int ts_found = 0;
    long trial_ends_at = json_long(json, "trial_ends_at", &ts_found);
    if (ts_found && trial_ends_at > 0 && time(NULL) > trial_ends_at) {
        free(json);
        *trial_expired = 1;
        return 0;
    }

    char aad[32] = {0};
    if (ts_found && trial_ends_at > 0)
        snprintf(aad, sizeof(aad), "%ld", trial_ends_at);

    int ok = cnn_parse_and_decrypt_bundle(json, delivery_key, aad, master_key_out);
    free(json);
    return ok;
}

/* ── File cache: write (machine-secret protected) ───────────────────────── */
/* The API bundle is re-encrypted with cnn_cache_protect_key before disk     */
/* storage, so /tmp files are unusable without the machine secret.            */
static void cnn_file_cache_write(
    const char          *cache_path,
    const char          *bundle_json,
    long                 trial_ends_at,
    const unsigned char *cache_key      /* 32 bytes from cnn_cache_protect_key */
) {
    /* Build full JSON with trial_ends_at */
    char full[8192];
    size_t blen = strlen(bundle_json);
    if (blen >= sizeof(full) - 64) return;
    if (blen > 0 && bundle_json[blen - 1] == '}') {
        memcpy(full, bundle_json, blen - 1);
        if (trial_ends_at > 0)
            snprintf(full + blen - 1, sizeof(full) - blen + 1, ",\"trial_ends_at\":%ld}", trial_ends_at);
        else
            snprintf(full + blen - 1, sizeof(full) - blen + 1, ",\"trial_ends_at\":null}");
    } else {
        memcpy(full, bundle_json, blen);
        full[blen] = '\0';
    }

    size_t json_len = strlen(full);

    /* AES-256-GCM encrypt with cache_key */
    unsigned char iv[12], tag[16];
    unsigned char *enc = malloc(json_len);
    if (!enc) return;

    unsigned char rnd[12];
    /* Use OpenSSL RAND_bytes for IV */
    if (RAND_bytes(rnd, 12) != 1) { free(enc); return; }
    memcpy(iv, rnd, 12);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { free(enc); return; }
    int outl = 0, outl2 = 0, ok = 0;
    if (!EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) goto wdone;
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL)) goto wdone;
    if (!EVP_EncryptInit_ex(ctx, NULL, NULL, cache_key, iv)) goto wdone;
    if (!EVP_EncryptUpdate(ctx, enc, &outl, (unsigned char *)full, (int)json_len)) goto wdone;
    if (!EVP_EncryptFinal_ex(ctx, enc + outl, &outl2)) goto wdone;
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag)) goto wdone;
    ok = 1;
wdone:
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { free(enc); return; }

    /* Write: "CNN2" magic (4) + iv (12) + tag (16) + enc_len (4 LE) + enc */
    FILE *f = fopen(cache_path, "wb");
    if (!f) { free(enc); return; }
    uint32_t enc_len_le = (uint32_t)(outl + outl2);
    unsigned char hdr[4] = {'C','N','N','2'};
    fwrite(hdr, 1, 4, f);
    fwrite(iv,  1, 12, f);
    fwrite(tag, 1, 16, f);
    fwrite(&enc_len_le, 1, 4, f);
    fwrite(enc, 1, enc_len_le, f);
    fclose(f);
    free(enc);
}

/* ── API call: POST /v1/key/fetch ─────────────────────────────────────────── */
/* Returns allocated JSON response string (caller must free), or NULL.       */
static char *cnn_api_fetch(const char *lid_upper, const char *fingerprint) {
    const char *api_url_env = getenv("CRYPTONN_API_URL");
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/key/fetch",
             api_url_env ? api_url_env : CNN_API_DEFAULT);

    char post[512];
    snprintf(post, sizeof(post),
        "{\"license_id\":\"%s\",\"server_fingerprint\":\"%s\",\"loader_version\":\"%s\"}",
        lid_upper, fingerprint, CNN_LOADER_VER);

    cnn_buf_t resp = {NULL, 0, 0};

    CURL *ch = curl_easy_init();
    if (!ch) return NULL;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    char ua[128];
    snprintf(ua, sizeof(ua), "CryptONN-Ext/%s PHP/%s", PHP_CRYPTONN_VERSION, PHP_VERSION);

    curl_easy_setopt(ch, CURLOPT_URL,            url);
    curl_easy_setopt(ch, CURLOPT_POSTFIELDS,     post);
    curl_easy_setopt(ch, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(ch, CURLOPT_USERAGENT,      ua);
    curl_easy_setopt(ch, CURLOPT_TIMEOUT,        CNN_API_TIMEOUT);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA,      &resp);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode rc = curl_easy_perform(ch);
    curl_slist_free_all(headers);
    curl_easy_cleanup(ch);

    if (rc != CURLE_OK) {
        if (resp.buf) free(resp.buf);
        return NULL;
    }
    return resp.buf; /* caller frees */
}

/* ── Vendor-license in-process cache ─────────────────────────────────────── */
/* Stores CNN_VL_OK (0) for valid, or CNN_VL_* error type for invalid.        */
#define CNN_VL_PROC_SIZE 16

typedef struct {
    char   key_prod[256];
    int    err_type;    /* CNN_VL_OK or CNN_VL_SUSPENDED / EXPIRED / ... */
    time_t cached_at;
    int    set;
} cnn_vl_entry_t;

static cnn_vl_entry_t s_vl_cache[CNN_VL_PROC_SIZE];
static int            s_vl_init = 0;

static void vl_proc_init(void) {
    if (!s_vl_init) { memset(s_vl_cache, 0, sizeof(s_vl_cache)); s_vl_init = 1; }
}

static int vl_proc_get(const char *kp, int *err_out) {
    vl_proc_init();
    time_t now = time(NULL);
    for (int i = 0; i < CNN_VL_PROC_SIZE; i++) {
        if (!s_vl_cache[i].set || strcmp(s_vl_cache[i].key_prod, kp) != 0) continue;
        long ttl = (s_vl_cache[i].err_type == CNN_VL_OK) ? CNN_VL_TTL_VALID : CNN_VL_TTL_INVALID;
        if ((now - s_vl_cache[i].cached_at) <= ttl) { *err_out = s_vl_cache[i].err_type; return 1; }
        return 0;
    }
    return 0;
}

static void vl_proc_set(const char *kp, int err_type) {
    vl_proc_init();
    static int s_vl_next = 0;
    strncpy(s_vl_cache[s_vl_next].key_prod, kp, sizeof(s_vl_cache[s_vl_next].key_prod) - 1);
    s_vl_cache[s_vl_next].key_prod[sizeof(s_vl_cache[s_vl_next].key_prod) - 1] = '\0';
    s_vl_cache[s_vl_next].err_type  = err_type;
    s_vl_cache[s_vl_next].cached_at = time(NULL);
    s_vl_cache[s_vl_next].set       = 1;
    s_vl_next = (s_vl_next + 1) % CNN_VL_PROC_SIZE;
}

/* ── Vendor-license file cache ────────────────────────────────────────────── */
/* File stores one ASCII digit: '0'=OK, '1'=SUSPENDED, '2'=EXPIRED,          */
/* '3'=REVOKED, '4'=DOMAIN, '5'=GENERAL.  Old '0'/'1' format auto-migrates.  */
static void vl_file_path(const char *kp, char out[256]) {
    unsigned char d[32]; char hex[65];
    cnn_sha256(kp, strlen(kp), d);
    for (int i = 0; i < 32; i++) sprintf(hex + i*2, "%02x", d[i]);
    snprintf(out, 256, "%s/_cnn_vl_%.16s.cache", P_tmpdir ? P_tmpdir : "/tmp", hex);
}

static int vl_file_read(const char *path, int *err_out, int grace) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    time_t age = time(NULL) - st.st_mtime;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int c = fgetc(f);
    fclose(f);
    if (c == EOF) return 0;

    int err_type;
    if (c >= '0' && c <= '5') {
        err_type = c - '0';                   /* new format: digit = CNN_VL_* */
    } else if (c == '1') {
        err_type = CNN_VL_OK;                 /* old format: '1' = valid */
    } else {
        err_type = CNN_VL_GENERAL;            /* old format: '0' = invalid */
    }

    long ttl;
    if (grace) {
        if (err_type != CNN_VL_OK) return 0;  /* grace only for valid */
        ttl = CNN_VL_TTL_GRACE;
    } else {
        ttl = (err_type == CNN_VL_OK) ? CNN_VL_TTL_VALID : CNN_VL_TTL_INVALID;
    }
    if (age > ttl) return 0;
    *err_out = err_type;
    return 1;
}

static void vl_file_write(const char *path, int err_type) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputc('0' + err_type, f);
    fclose(f);
}

/* ── Get domain from request environment ─────────────────────────────────── */
/* Tries HTTP_HOST → SERVER_NAME → gethostname(). Strips port suffix.        */
/* Calls sapi_module.getenv directly to avoid the libcurl macro conflict     */
/* (libcurl may #define sapi_getenv curl_getenv). The returned pointer is    */
/* owned by the SAPI and must NOT be freed — we only read & copy it.         */
static void cnn_request_domain(char *out, int out_max) {
    out[0] = '\0';
    char *host = NULL;
    if (sapi_module.getenv) {
        host = sapi_module.getenv("HTTP_HOST", sizeof("HTTP_HOST") - 1);
        if (!host || !host[0])
            host = sapi_module.getenv("SERVER_NAME", sizeof("SERVER_NAME") - 1);
    }
    if (!host || !host[0]) {
        /* Fallback for CLI or SAPIs without getenv hook */
        host = getenv("HTTP_HOST");
        if (!host || !host[0]) host = getenv("SERVER_NAME");
    }
    if (host && host[0]) {
        strncpy(out, host, out_max - 1);
        out[out_max - 1] = '\0';
        /* Strip port */
        char *colon = strrchr(out, ':');
        if (colon) {
            int digits = 1;
            for (char *p = colon + 1; *p; p++)
                if (*p < '0' || *p > '9') { digits = 0; break; }
            if (digits) *colon = '\0';
        }
        /* Lowercase */
        for (char *p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
        return;
    }
    gethostname(out, out_max - 1);
    out[out_max - 1] = '\0';
}

/* ── Vendor-license API call ──────────────────────────────────────────────── */
/* Returns: CNN_VL_OK=valid, CNN_VL_* error type, or -1 on network error.     */
static int cnn_vl_api_verify(const char *vl_key, const char *product_id,
                              const char *domain)
{
    const char *base = getenv("CRYPTONN_API_URL");
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/vendor/licenses/verify",
             base ? base : CNN_API_DEFAULT);

    char post[512];
    snprintf(post, sizeof(post),
        "{\"license_key\":\"%s\",\"product_id\":\"%s\",\"domain\":\"%s\"}",
        vl_key, product_id, domain);

    cnn_buf_t resp = {NULL, 0, 0};
    CURL *ch = curl_easy_init();
    if (!ch) return -1;

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

    char ua[128];
    snprintf(ua, sizeof(ua), "CryptONN-Ext/%s PHP/%s", PHP_CRYPTONN_VERSION, PHP_VERSION);

    curl_easy_setopt(ch, CURLOPT_URL,            url);
    curl_easy_setopt(ch, CURLOPT_POSTFIELDS,     post);
    curl_easy_setopt(ch, CURLOPT_HTTPHEADER,     hdrs);
    curl_easy_setopt(ch, CURLOPT_USERAGENT,      ua);
    curl_easy_setopt(ch, CURLOPT_TIMEOUT,        CNN_API_TIMEOUT);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA,      &resp);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode rc = curl_easy_perform(ch);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(ch);

    if (rc != CURLE_OK) { if (resp.buf) free(resp.buf); return -1; }

    int result = CNN_VL_GENERAL;
    if (resp.buf) {
        if (strstr(resp.buf, "\"valid\":true") || strstr(resp.buf, "\"valid\": true")) {
            result = CNN_VL_OK;
        } else {
            /* Map API message to error type */
            if      (strstr(resp.buf, "suspended"))                result = CNN_VL_SUSPENDED;
            else if (strstr(resp.buf, "expired"))                  result = CNN_VL_EXPIRED;
            else if (strstr(resp.buf, "revoked"))                  result = CNN_VL_REVOKED;
            else if (strstr(resp.buf, "Domain not authorized") ||
                     strstr(resp.buf, "domain") ||
                     strstr(resp.buf, "IP"))                       result = CNN_VL_DOMAIN;
            else                                                   result = CNN_VL_GENERAL;
        }
        free(resp.buf);
    }
    return result;
}

/* ── Vendor-license verify: in-process → file → API → grace ─────────────── */
/* Returns CNN_VL_OK (0) if valid, or CNN_VL_* error type if invalid.         */
static int cnn_vendor_license_verify(const char *vl_key, const char *product_id) {
    char kp[256];
    snprintf(kp, sizeof(kp), "%.100s|%.100s", vl_key, product_id);

    /* Layer 1: in-process */
    int err = CNN_VL_GENERAL;
    if (vl_proc_get(kp, &err)) return err;

    char domain[256];
    cnn_request_domain(domain, sizeof(domain));

    char fpath[256];
    vl_file_path(kp, fpath);

    /* Layer 2: file cache (fresh TTL) */
    if (vl_file_read(fpath, &err, 0)) {
        vl_proc_set(kp, err);
        return err;
    }

    /* Layer 3: API */
    int api = cnn_vl_api_verify(vl_key, product_id, domain);
    if (api >= 0) {
        vl_file_write(fpath, api);
        vl_proc_set(kp, api);
        return api;
    }

    /* Layer 4: grace period — stale valid result up to 2h */
    if (vl_file_read(fpath, &err, 1)) {
        vl_proc_set(kp, err);
        return err;
    }

    return CNN_VL_GENERAL; /* fail closed */
}

/* ── Master key fetch (3-layer cache) ────────────────────────────────────── */
/*                                                                            */
/*  Layer 1: in-process static array  (per PHP worker process)               */
/*  Layer 2: file cache /tmp (24 h fresh, 72 h grace)                        */
/*  Layer 3: API call                                                         */
/*                                                                            */
/*  Trial-expired files are rejected at every layer.                         */
/*  Grace period is skipped for trial licenses.                               */
static int cnn_get_master_key(const char *license_id, unsigned char out[32]) {
    /* Normalise to upper-case */
    char lid[33] = {0};
    for (int i = 0; i < 32 && license_id[i]; i++)
        lid[i] = (char)toupper((unsigned char)license_id[i]);

    /* ── Layer 1: in-process ─────────────────────────────────────────────── */
    const char *cached = proc_cache_get(lid);
    if (cached) {
        memcpy(out, cached, 32);
        return 1;
    }

    char fp[65];
    cnn_fingerprint(fp);

    unsigned char delivery_key[32];
    cnn_delivery_key(lid, fp, delivery_key);

    /* Machine secret — protects /tmp cache from public-info decryption */
    unsigned char machine_secret[32];
    cnn_machine_secret(machine_secret);

    unsigned char cache_key[32];
    cnn_cache_protect_key(machine_secret, lid, fp, cache_key);

    char cache_path[256];
    cnn_cache_path(lid, cache_path);

    /* ── Layer 2: file cache (fresh ≤ 24 h) ─────────────────────────────── */
    int trial_expired = 0;
    if (cnn_file_cache_read(cache_path, delivery_key, cache_key, out, &trial_expired, 0)) {
        proc_cache_set(lid, (char *)out);
        return 1;
    }
    if (trial_expired) {
        remove(cache_path);
        return 0; /* trial ended, cache purged */
    }

    /* ── Layer 3: API call ────────────────────────────────────────────────── */
    char *resp = cnn_api_fetch(lid, fp);

    if (resp) {
        /* Check for explicit TRIAL_EXPIRED error */
        char err_code[64] = {0};
        if (json_error_code(resp, err_code, sizeof(err_code)) &&
            strcmp(err_code, "TRIAL_EXPIRED") == 0) {
            remove(cache_path);
            free(resp);
            return 0;
        }

        /* Check ok:true */
        if (strstr(resp, "\"ok\":true") || strstr(resp, "\"ok\": true")) {
            /* Extract customer_key bundle */
            const char *bk = strstr(resp, "\"customer_key\"");
            if (bk) {
                /* Find the opening { of the bundle object */
                const char *bstart = strchr(bk + strlen("\"customer_key\""), '{');
                if (bstart) {
                    /* Find matching closing } */
                    int depth = 0;
                    const char *bend = bstart;
                    while (*bend) {
                        if (*bend == '{') depth++;
                        else if (*bend == '}') { depth--; if (depth == 0) { bend++; break; } }
                        bend++;
                    }

                    int bundle_len = (int)(bend - bstart);
                    char *bundle_json = malloc(bundle_len + 1);
                    if (bundle_json) {
                        memcpy(bundle_json, bstart, bundle_len);
                        bundle_json[bundle_len] = '\0';

                        /* trial_ends_at from outer data object */
                        int ts_found = 0;
                        long trial_ends_at = json_long(resp, "trial_ends_at", &ts_found);

                        char aad[32] = {0};
                        if (ts_found && trial_ends_at > 0)
                            snprintf(aad, sizeof(aad), "%ld", trial_ends_at);

                        if (cnn_parse_and_decrypt_bundle(bundle_json, delivery_key, aad, out)) {
                            cnn_file_cache_write(cache_path, bundle_json, trial_ends_at, cache_key);
                            proc_cache_set(lid, (char *)out);
                            free(bundle_json);
                            free(resp);
                            return 1;
                        }
                        free(bundle_json);
                    }
                }
            }
        }
        /* API responded but key invalid — do NOT fall through to grace */
        free(resp);
        return 0;
    }

    /* ── Grace: API unreachable, stale cache ≤ 72 h ─────────────────────── */
    /* Trial licenses do NOT get grace period — expired is expired.          */
    if (cnn_file_cache_read(cache_path, delivery_key, cache_key, out, &trial_expired, 1)) {
        proc_cache_set(lid, (char *)out);
        return 1;
    }

    return 0;
}

/* ── File-key derivation: BLAKE2b-256 MAC ─────────────────────────────────── */
/* Mirrors Rust: crypto_generichash("cryptonn:v1:{lid}:{basename}", mk, 32)  */
static int cnn_derive_file_key(
    const char    *license_id,
    const char    *filename,   /* basename only */
    const unsigned char *master_key,
    unsigned char  out[32]
) {
    char info[512] = {0};
    snprintf(info, sizeof(info), "cryptonn:v1:%s:%s", license_id, filename);
    return crypto_generichash(out, 32,
                              (const unsigned char *)info, strlen(info),
                              master_key, 32) == 0;
}

/* ── AES-256-GCM decrypt payload ─────────────────────────────────────────── */
static unsigned char *cnn_decrypt_payload(
    const unsigned char *ciphertext, size_t ct_len,
    const unsigned char *file_key,
    const unsigned char *iv,
    const unsigned char *tag,
    size_t *plain_len_out
) {
    unsigned char *plain = malloc(ct_len + 1);
    if (!plain) return NULL;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { free(plain); return NULL; }

    int outl = 0, outl2 = 0, ok = 0;
    if (!EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) goto done;
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL)) goto done;
    if (!EVP_DecryptInit_ex(ctx, NULL, NULL, file_key, iv)) goto done;
    if (!EVP_DecryptUpdate(ctx, plain, &outl, ciphertext, (int)ct_len)) goto done;
    if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag)) goto done;
    if (EVP_DecryptFinal_ex(ctx, plain + outl, &outl2) <= 0) goto done;
    plain[outl + outl2] = '\0';
    *plain_len_out = (size_t)(outl + outl2);
    ok = 1;
done:
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) { free(plain); return NULL; }
    return plain;
}

/* ── Original zend_compile_file pointer ──────────────────────────────────── */
static zend_op_array *(*cnn_orig_compile_file)(zend_file_handle *fh, int type);

/* ── Vendor-license error page (HTTP 403, 5 types × 10 languages) ─────────── */
static void cnn_vl_send_error_page(int err_type)
{
    if (err_type < 1 || err_type > 5) err_type = 5;
    char digit[1];
    digit[0] = (char)('0' + err_type);

    static const char HEAD[] =
        "<!DOCTYPE html><html lang=\"en\"><head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>CryptONN PHP Encoder | License Error</title>"
        "<meta name=\"description\" content=\"This application is protected by CryptONN PHP Encoder, a professional PHP source code encryption and license management solution by Laicos.\">"
        "<meta name=\"robots\" content=\"noindex, follow\">"
        "<meta property=\"og:type\" content=\"website\">"
        "<meta property=\"og:title\" content=\"CryptONN PHP Encoder | License Protected Application\">"
        "<meta property=\"og:description\" content=\"This application is encrypted and license-protected by CryptONN PHP Encoder. Visit laicos.com.tr/cryptonn to learn more.\">"
        "<meta property=\"og:url\" content=\"https://laicos.com.tr/cryptonn\">"
        "<meta property=\"og:image\" content=\"https://api.laicos.com.tr/cdn/crypton_logo_c-BLzYjWhI.png\">"
        "<meta property=\"og:site_name\" content=\"CryptONN PHP Encoder\">"
        "<script type=\"application/ld+json\">"
        "{\"@context\":\"https://schema.org\","
        "\"@type\":\"SoftwareApplication\","
        "\"name\":\"CryptONN PHP Encoder\","
        "\"url\":\"https://laicos.com.tr/cryptonn\","
        "\"applicationCategory\":\"DeveloperApplication\","
        "\"operatingSystem\":\"PHP 7.3+\","
        "\"description\":\"CryptONN is a professional PHP source code encoder and license management system. Protects PHP applications with AES-256-GCM encryption and enforces vendor license control.\","
        "\"publisher\":{\"@type\":\"Organization\",\"name\":\"Laicos\",\"url\":\"https://laicos.com.tr\","
        "\"logo\":\"https://api.laicos.com.tr/cdn/laicos_logo-BzcTtjW0.png\"},"
        "\"offers\":{\"@type\":\"Offer\",\"url\":\"https://laicos.com.tr/cryptonn\"}}"
        "</script>"
        "<style>"
        "*,::before,::after{box-sizing:border-box;margin:0;padding:0}"
        "html,body{height:100%;background:#0a0a0a;display:flex;align-items:center;"
        "justify-content:center;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
        ".window{width:100%;max-width:560px;border-radius:12px;overflow:hidden;"
        "box-shadow:0 0 0 1px rgba(255,255,255,.08),0 32px 80px rgba(0,0,0,.7),0 8px 24px rgba(0,0,0,.4)}"
        ".titlebar{display:flex;align-items:center;padding:13px 16px;position:relative;"
        "background:#2d2d2f;border-bottom:1px solid rgba(255,255,255,.06)}"
        ".controls{display:flex;gap:8px}"
        ".dot{width:12px;height:12px;border-radius:50%}"
        ".close{background:#ff5f57}.minimize{background:#ffbd2e}.maximize{background:#28c840}"
        ".titlebar-label{position:absolute;left:50%;transform:translateX(-50%);"
        "font-size:12px;font-weight:500;color:rgba(255,255,255,.3);letter-spacing:.02em;pointer-events:none}"
        ".content{background:#1c1c1e;padding:36px 40px 20px;display:flex;flex-direction:column;align-items:center;text-align:center}"
        ".brand{display:flex;align-items:center;gap:12px;text-decoration:none;margin-bottom:24px}"
        ".icon{width:50px;height:50px;object-fit:contain}"
        ".brand-text{text-align:left}"
        ".brand-name{font-size:18px;font-weight:800;letter-spacing:-.02em;line-height:1}"
        ".brand-name .crypt{color:#fff}.brand-name .onn{color:#f05252}"
        ".brand-sub{font-size:10px;font-weight:500;letter-spacing:.14em;color:rgba(255,255,255,.22);margin-top:5px}"
        ".divider{width:100%;height:1px;background:rgba(255,255,255,.07);margin-bottom:24px}"
        ".badge{display:inline-flex;align-items:center;gap:7px;"
        "border:1px solid transparent;font-size:11px;font-weight:600;"
        "letter-spacing:.1em;padding:5px 12px;border-radius:5px;margin-bottom:18px}"
        ".bdot{width:6px;height:6px;border-radius:50%;animation:pulse 1.6s ease-in-out infinite}"
        ".bcode{opacity:.5;font-size:9px;letter-spacing:.05em}"
        "@keyframes pulse{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.4;transform:scale(.7)}}"
        "h1{font-size:24px;font-weight:700;color:#fff;line-height:1.25;letter-spacing:-.03em;margin-bottom:12px}"
        "p{font-size:14px;color:rgba(255,255,255,.38);line-height:1.65;max-width:380px;margin-bottom:24px}"
        ".flags{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-bottom:20px}"
        ".fb{background:none;border:2px solid transparent;border-radius:4px;padding:2px;"
        "cursor:pointer;opacity:.45;transition:opacity .15s,border-color .15s,transform .15s;line-height:0}"
        ".fb:hover{opacity:.8;transform:scale(1.1)}"
        ".fb.active{opacity:1;border-color:rgba(255,255,255,.35);transform:scale(1.12)}"
        ".fb img{display:block;width:28px;height:20px;object-fit:cover;border-radius:2px}"
        ".footer{padding:11px 40px 13px;border-top:1px solid rgba(255,255,255,.06);width:100%;"
        "display:flex;align-items:center;justify-content:center;gap:8px;direction:ltr;"
        "background:#000;border-radius:0 0 12px 12px}"
        "img.ll{height:18px;width:auto;display:block;mix-blend-mode:lighten;opacity:.5}"
        ".ft-lk{font-size:10px;color:rgba(255,255,255,.28);text-decoration:none;letter-spacing:.06em;font-weight:500}"
        ".ft-lk:hover{color:rgba(255,255,255,.55)}"
        ".ft-sep{color:rgba(255,255,255,.15);margin:0 6px;font-size:11px}"
        "</style></head><body>"
        "<div class=\"window\">"
        "<div class=\"titlebar\">"
        "<div class=\"controls\">"
        "<div class=\"dot close\"></div>"
        "<div class=\"dot minimize\"></div>"
        "<div class=\"dot maximize\"></div>"
        "</div>"
        "<span class=\"titlebar-label\">cryptonn | license check</span>"
        "</div>"
        "<div class=\"content\">"
        "<a class=\"brand\" href=\"https://laicos.com.tr/cryptonn\" target=\"_blank\" rel=\"noopener\">"
        "<img class=\"icon\" src=\"https://api.laicos.com.tr/cdn/crypton_logo_c-BLzYjWhI.png\" alt=\"CryptONN\">"
        "<div class=\"brand-text\">"
        "<div class=\"brand-name\"><span class=\"crypt\">CRYPT</span><span class=\"onn\">ONN</span></div>"
        "<div class=\"brand-sub\">PROTECT YOUR CODE</div>"
        "</div></a>"
        "<div class=\"divider\"></div>"
        "<div class=\"badge\" id=\"B\"><span class=\"bdot\" id=\"D\"></span>"
        "<span id=\"BT\"></span><span class=\"bcode\" id=\"BC\"></span></div>"
        "<h1 id=\"TL\"></h1><p id=\"DS\"></p>"
        "<div class=\"flags\" id=\"FR\"></div>"
        "</div>"
        "<div class=\"footer\">"
        "<a href=\"https://laicos.com.tr\" target=\"_blank\" rel=\"noopener\">"
        "<img class=\"ll\" src=\"https://api.laicos.com.tr/cdn/laicos_logo-BzcTtjW0.png\" alt=\"Laicos\">"
        "</a>"
        "<span class=\"ft-sep\">\xc2\xb7</span>"
        "<a class=\"ft-lk\" href=\"https://laicos.com.tr/cryptonn\" target=\"_blank\" rel=\"noopener\">CryptONN PHP Encoder</a>"
        "</div></div>"
        "<script>window.__CNN_ERR=";

    static const char TAIL[] =
        ";"
        "(function(){"
        "var E=window.__CNN_ERR||1;"
        "var ec={"
        "1:{b:'LICENSE SUSPENDED',c:'CR-403',col:'#f05252',rgb:'240,82,82'},"
        "2:{b:'LICENSE EXPIRED',c:'CR-402',col:'#fbbf24',rgb:'251,191,36'},"
        "3:{b:'LICENSE REVOKED',c:'CR-401',col:'#f97316',rgb:'249,115,22'},"
        "4:{b:'DOMAIN MISMATCH',c:'CR-403',col:'#818cf8',rgb:'129,140,248'},"
        "5:{b:'VERIFICATION ERROR',c:'CR-501',col:'#6b7280',rgb:'107,114,128'}"
        "};"
        "var lg=["
        "{g:'gb',code:'en',d:["
        "'License<br>Suspended','This software\\u2019s license has been suspended. Please contact your software provider.',"
        "'License<br>Expired','This software\\u2019s license has expired. Please renew your license to continue.',"
        "'License<br>Revoked','This software\\u2019s license has been permanently revoked. Please contact your software provider.',"
        "'Domain Not<br>Authorized','This software\\u2019s license is not valid for this domain. Please contact your software provider.',"
        "'Verification<br>Failed','License verification failed. Please try again later or contact your software provider.'"
        "]},"
        "{g:'tr',code:'tr',d:["
        "'Lisans<br>Ask\\u0131ya Al\\u0131nd\\u0131','Bu yaz\\u0131l\\u0131m\\u0131n lisans\\u0131 ask\\u0131ya al\\u0131nm\\u0131\\u015ft\\u0131r. L\\u00fctfen yaz\\u0131l\\u0131m sa\\u011flay\\u0131c\\u0131n\\u0131z ile ileti\\u015fime ge\\u00e7in.',"
        "'Lisans S\\u00fcresi<br>Doldu','Bu yaz\\u0131l\\u0131m\\u0131n lisans s\\u00fcresi dolmu\\u015ftur. Devam etmek i\\u00e7in lisans\\u0131n\\u0131z\\u0131 yenileyin.',"
        "'Lisans<br>\\u0130ptal Edildi','Bu yaz\\u0131l\\u0131m\\u0131n lisans\\u0131 kal\\u0131c\\u0131 olarak iptal edilmi\\u015ftir. Yaz\\u0131l\\u0131m sa\\u011flay\\u0131c\\u0131n\\u0131z ile ileti\\u015fime ge\\u00e7in.',"
        "'Alan Ad\\u0131<br>Yetkisiz','Bu yaz\\u0131l\\u0131m\\u0131n lisans\\u0131 bu alan ad\\u0131 i\\u00e7in ge\\u00e7erli de\\u011fildir. Yaz\\u0131l\\u0131m sa\\u011flay\\u0131c\\u0131n\\u0131z ile ileti\\u015fime ge\\u00e7in.',"
        "'Do\\u011frulama<br>Ba\\u015far\\u0131s\\u0131z','Lisans do\\u011frulamas\\u0131 ba\\u015far\\u0131s\\u0131z oldu. L\\u00fctfen daha sonra tekrar deneyin veya yaz\\u0131l\\u0131m sa\\u011flay\\u0131c\\u0131n\\u0131z ile ileti\\u015fime ge\\u00e7in.'"
        "]},"
        "{g:'de',code:'de',d:["
        "'Lizenz<br>Ausgesetzt','Die Lizenz dieser Software wurde ausgesetzt. Bitte kontaktieren Sie Ihren Softwareanbieter.',"
        "'Lizenz<br>Abgelaufen','Die Lizenz dieser Software ist abgelaufen. Bitte erneuern Sie Ihre Lizenz um fortzufahren.',"
        "'Lizenz<br>Widerrufen','Die Lizenz dieser Software wurde dauerhaft widerrufen. Bitte kontaktieren Sie Ihren Softwareanbieter.',"
        "'Dom\\u00e4ne Nicht<br>Autorisiert','Die Lizenz dieser Software gilt nicht f\\u00fcr diese Dom\\u00e4ne. Bitte kontaktieren Sie Ihren Anbieter.',"
        "'Verifizierung<br>Fehlgeschlagen','Die Lizenzverifizierung ist fehlgeschlagen. Bitte versuchen Sie es sp\\u00e4ter erneut.'"
        "]},"
        "{g:'fr',code:'fr',d:["
        "'Licence<br>Suspendue','La licence de ce logiciel a \\u00e9t\\u00e9 suspendue. Veuillez contacter votre fournisseur de logiciels.',"
        "'Licence<br>Expir\\u00e9e','La licence de ce logiciel a expir\\u00e9. Veuillez renouveler votre licence pour continuer.',"
        "'Licence<br>R\\u00e9voqu\\u00e9e','La licence de ce logiciel a \\u00e9t\\u00e9 d\\u00e9finitivement r\\u00e9voqu\\u00e9e. Veuillez contacter votre fournisseur.',"
        "'Domaine Non<br>Autoris\\u00e9','La licence de ce logiciel n\\u2019est pas valide pour ce domaine. Veuillez contacter votre fournisseur.',"
        "'V\\u00e9rification<br>\\u00c9chou\\u00e9e','La v\\u00e9rification de la licence a \\u00e9chou\\u00e9. Veuillez r\\u00e9essayer plus tard ou contacter votre fournisseur.'"
        "]},"
        "{g:'es',code:'es',d:["
        "'Licencia<br>Suspendida','La licencia de este software ha sido suspendida. Comun\\u00edquese con su proveedor de software.',"
        "'Licencia<br>Expirada','La licencia de este software ha expirado. Por favor renueve su licencia para continuar.',"
        "'Licencia<br>Revocada','La licencia de este software ha sido revocada permanentemente. Contacte a su proveedor.',"
        "'Dominio No<br>Autorizado','La licencia de este software no es v\\u00e1lida para este dominio. Contacte a su proveedor.',"
        "'Verificaci\\u00f3n<br>Fallida','La verificaci\\u00f3n de la licencia fall\\u00f3. Por favor intente m\\u00e1s tarde o contacte a su proveedor.'"
        "]},"
        "{g:'pt',code:'pt',d:["
        "'Licen\\u00e7a<br>Suspensa','A licen\\u00e7a deste software foi suspensa. Entre em contato com seu fornecedor de software.',"
        "'Licen\\u00e7a<br>Expirada','A licen\\u00e7a deste software expirou. Por favor renove sua licen\\u00e7a para continuar.',"
        "'Licen\\u00e7a<br>Revogada','A licen\\u00e7a deste software foi revogada permanentemente. Entre em contato com seu fornecedor.',"
        "'Dom\\u00ednio N\\u00e3o<br>Autorizado','A licen\\u00e7a deste software n\\u00e3o \\u00e9 v\\u00e1lida para este dom\\u00ednio. Contate seu fornecedor.',"
        "'Verifica\\u00e7\\u00e3o<br>Falhou','A verifica\\u00e7\\u00e3o da licen\\u00e7a falhou. Por favor tente novamente mais tarde ou contate seu fornecedor.'"
        "]},"
        "{g:'ru',code:'ru',d:["
        "'\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f<br>\\u041f\\u0440\\u0438\\u043e\\u0441\\u0442\\u0430\\u043d\\u043e\\u0432\\u043b\\u0435\\u043d\\u0430','\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f \\u044d\\u0442\\u043e\\u0433\\u043e \\u041f\\u041e \\u043f\\u0440\\u0438\\u043e\\u0441\\u0442\\u0430\\u043d\\u043e\\u0432\\u043b\\u0435\\u043d\\u0430. \\u041e\\u0431\\u0440\\u0430\\u0442\\u0438\\u0442\\u0435\\u0441\\u044c \\u043a \\u043f\\u043e\\u0441\\u0442\\u0430\\u0432\\u0449\\u0438\\u043a\\u0443.',"
        "'\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f<br>\\u0418\\u0441\\u0442\\u0435\\u043a\\u043b\\u0430','\\u0421\\u0440\\u043e\\u043a \\u0434\\u0435\\u0439\\u0441\\u0442\\u0432\\u0438\\u044f \\u043b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u0438 \\u044d\\u0442\\u043e\\u0433\\u043e \\u041f\\u041e \\u0438\\u0441\\u0442\\u0451\\u043a. \\u041f\\u043e\\u0436\\u0430\\u043b\\u0443\\u0439\\u0441\\u0442\\u0430, \\u043f\\u0440\\u043e\\u0434\\u043b\\u0438\\u0442\\u0435 \\u043b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044e.',"
        "'\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f<br>\\u041e\\u0442\\u043e\\u0437\\u0432\\u0430\\u043d\\u0430','\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f \\u044d\\u0442\\u043e\\u0433\\u043e \\u041f\\u041e \\u0431\\u044b\\u043b\\u0430 \\u043e\\u0442\\u043e\\u0437\\u0432\\u0430\\u043d\\u0430. \\u041e\\u0431\\u0440\\u0430\\u0442\\u0438\\u0442\\u0435\\u0441\\u044c \\u043a \\u043f\\u043e\\u0441\\u0442\\u0430\\u0432\\u0449\\u0438\\u043a\\u0443.',"
        "'\\u0414\\u043e\\u043c\\u0435\\u043d<br>\\u041d\\u0435 \\u0410\\u0432\\u0442\\u043e\\u0440\\u0438\\u0437\\u043e\\u0432\\u0430\\u043d','\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f \\u044d\\u0442\\u043e\\u0433\\u043e \\u041f\\u041e \\u043d\\u0435\\u0434\\u0435\\u0439\\u0441\\u0442\\u0432\\u0438\\u0442\\u0435\\u043b\\u044c\\u043d\\u0430 \\u0434\\u043b\\u044f \\u044d\\u0442\\u043e\\u0433\\u043e \\u0434\\u043e\\u043c\\u0435\\u043d\\u0430. \\u041e\\u0431\\u0440\\u0430\\u0442\\u0438\\u0442\\u0435\\u0441\\u044c \\u043a \\u043f\\u043e\\u0441\\u0442\\u0430\\u0432\\u0449\\u0438\\u043a\\u0443.',"
        "'\\u041e\\u0448\\u0438\\u0431\\u043a\\u0430<br>\\u041f\\u0440\\u043e\\u0432\\u0435\\u0440\\u043a\\u0438','\\u041f\\u0440\\u043e\\u0432\\u0435\\u0440\\u043a\\u0430 \\u043b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u0438 \\u043d\\u0435 \\u0443\\u0434\\u0430\\u043b\\u0430\\u0441\\u044c. \\u041f\\u043e\\u0432\\u0442\\u043e\\u0440\\u0438\\u0442\\u0435 \\u043f\\u043e\\u043f\\u044b\\u0442\\u043a\\u0443 \\u043f\\u043e\\u0437\\u0436\\u0435 \\u0438\\u043b\\u0438 \\u043e\\u0431\\u0440\\u0430\\u0442\\u0438\\u0442\\u0435\\u0441\\u044c \\u043a \\u043f\\u043e\\u0441\\u0442\\u0430\\u0432\\u0449\\u0438\\u043a\\u0443.'"
        "]},"
        "{g:'cn',code:'zh',d:["
        "'\\u8bb8\\u53ef\\u8bc1<br>\\u5df2\\u6682\\u505c','\\u6b64\\u8f6f\\u4ef6\\u7684\\u8bb8\\u53ef\\u8bc1\\u5df2\\u88ab\\u6682\\u505c\\u3002\\u8bf7\\u8054\\u7cfb\\u60a8\\u7684\\u8f6f\\u4ef6\\u63d0\\u4f9b\\u5546\\u3002',"
        "'\\u8bb8\\u53ef\\u8bc1<br>\\u5df2\\u8fc7\\u671f','\\u6b64\\u8f6f\\u4ef6\\u7684\\u8bb8\\u53ef\\u8bc1\\u5df2\\u8fc7\\u671f\\u3002\\u8bf7\\u7eed\\u8ba2\\u8bb8\\u53ef\\u8bc1\\u4ee5\\u7ee7\\u7eed\\u4f7f\\u7528\\u3002',"
        "'\\u8bb8\\u53ef\\u8bc1<br>\\u5df2\\u540a\\u9500','\\u6b64\\u8f6f\\u4ef6\\u7684\\u8bb8\\u53ef\\u8bc1\\u5df2\\u88ab\\u6c38\\u4e45\\u540a\\u9500\\u3002\\u8bf7\\u8054\\u7cfb\\u60a8\\u7684\\u8f6f\\u4ef6\\u63d0\\u4f9b\\u5546\\u3002',"
        "'\\u57df\\u540d<br>\\u672a\\u6388\\u6743','\\u6b64\\u8f6f\\u4ef6\\u7684\\u8bb8\\u53ef\\u8bc1\\u5bf9\\u6b64\\u57df\\u540d\\u65e0\\u6548\\u3002\\u8bf7\\u8054\\u7cfb\\u60a8\\u7684\\u8f6f\\u4ef6\\u63d0\\u4f9b\\u5546\\u3002',"
        "'\\u9a8c\\u8bc1<br>\\u5931\\u8d25','\\u8bb8\\u53ef\\u8bc1\\u9a8c\\u8bc1\\u5931\\u8d25\\u3002\\u8bf7\\u7a0d\\u540e\\u91cd\\u8bd5\\u6216\\u8054\\u7cfb\\u60a8\\u7684\\u8f6f\\u4ef6\\u63d0\\u4f9b\\u5546\\u3002'"
        "]},"
        "{g:'jp',code:'ja',d:["
        "'\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9<br>\\u505c\\u6b62\\u4e2d','\\u3053\\u306e\\u30bd\\u30d5\\u30c8\\u30a6\\u30a7\\u30a2\\u306e\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9\\u304c\\u505c\\u6b62\\u3055\\u308c\\u3066\\u3044\\u307e\\u3059\\u3002\\u30bd\\u30d5\\u30c8\\u30a6\\u30a7\\u30a2\\u30d7\\u30ed\\u30d0\\u30a4\\u30c0\\u30fc\\u306b\\u304a\\u554f\\u3044\\u5408\\u308f\\u305b\\u304f\\u3060\\u3055\\u3044\\u3002',"
        "'\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9<br>\\u671f\\u9650\\u5207\\u308c','\\u3053\\u306e\\u30bd\\u30d5\\u30c8\\u30a6\\u30a7\\u30a2\\u306e\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9\\u304c\\u671f\\u9650\\u5207\\u308c\\u3067\\u3059\\u3002\\u7d9a\\u3051\\u308b\\u305f\\u3081\\u306b\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9\\u3092\\u66f4\\u65b0\\u3057\\u3066\\u304f\\u3060\\u3055\\u3044\\u3002',"
        "'\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9<br>\\u5931\\u52b9','\\u3053\\u306e\\u30bd\\u30d5\\u30c8\\u30a6\\u30a7\\u30a2\\u306e\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9\\u304c\\u6c38\\u4e45\\u306b\\u5931\\u52b9\\u3057\\u307e\\u3057\\u305f\\u3002\\u30d7\\u30ed\\u30d0\\u30a4\\u30c0\\u30fc\\u306b\\u304a\\u554f\\u3044\\u5408\\u308f\\u305b\\u304f\\u3060\\u3055\\u3044\\u3002',"
        "'\\u30c9\\u30e1\\u30a4\\u30f3<br>\\u672a\\u627f\\u8a8d','\\u3053\\u306e\\u30c9\\u30e1\\u30a4\\u30f3\\u3067\\u306f\\u30bd\\u30d5\\u30c8\\u30a6\\u30a7\\u30a2\\u306e\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9\\u304c\\u6709\\u52b9\\u3067\\u306f\\u3042\\u308a\\u307e\\u305b\\u3093\\u3002\\u30d7\\u30ed\\u30d0\\u30a4\\u30c0\\u30fc\\u306b\\u304a\\u554f\\u3044\\u5408\\u308f\\u305b\\u304f\\u3060\\u3055\\u3044\\u3002',"
        "'\\u8a8d\\u8a3c<br>\\u5931\\u6557','\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9\\u8a8d\\u8a3c\\u306b\\u5931\\u6557\\u3057\\u307e\\u3057\\u305f\\u3002\\u5f8c\\u3067\\u3082\\u3046\\u4e00\\u5ea6\\u304a\\u8a66\\u3057\\u3044\\u305f\\u3060\\u304f\\u304b\\u3001\\u30d7\\u30ed\\u30d0\\u30a4\\u30c0\\u30fc\\u306b\\u304a\\u554f\\u3044\\u5408\\u308f\\u305b\\u304f\\u3060\\u3055\\u3044\\u3002'"
        "]},"
        "{g:'sa',code:'ar',d:["
        "'\\u0627\\u0644\\u062a\\u0631\\u062e\\u064a\\u0635<br>\\u0645\\u0639\\u0644\\u0642','\\u062a\\u0645 \\u062a\\u0639\\u0644\\u064a\\u0642 \\u062a\\u0631\\u062e\\u064a\\u0635 \\u0647\\u0630\\u0627 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c. \\u064a\\u0631\\u062c\\u0649 \\u0627\\u0644\\u0627\\u062a\\u0635\\u0627\\u0644 \\u0628\\u0645\\u0648\\u0641\\u0631 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c.',"
        "'\\u0627\\u0644\\u062a\\u0631\\u062e\\u064a\\u0635<br>\\u0645\\u0646\\u062a\\u0647\\u064a \\u0627\\u0644\\u0635\\u0644\\u0627\\u062d\\u064a\\u0629','\\u0627\\u0646\\u062a\\u0647\\u062a \\u0635\\u0644\\u0627\\u062d\\u064a\\u0629 \\u062a\\u0631\\u062e\\u064a\\u0635 \\u0647\\u0630\\u0627 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c. \\u064a\\u0631\\u062c\\u0649 \\u062a\\u062c\\u062f\\u064a\\u062f \\u0627\\u0644\\u062a\\u0631\\u062e\\u064a\\u0635 \\u0644\\u0644\\u0627\\u0633\\u062a\\u0645\\u0631\\u0627\\u0631.',"
        "'\\u062a\\u0645 \\u0625\\u0644\\u063a\\u0627\\u0621<br>\\u0627\\u0644\\u062a\\u0631\\u062e\\u064a\\u0635','\\u062a\\u0645 \\u0625\\u0644\\u063a\\u0627\\u0621 \\u062a\\u0631\\u062e\\u064a\\u0635 \\u0647\\u0630\\u0627 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c \\u0628\\u0634\\u0643\\u0644 \\u062f\\u0627\\u0626\\u0645. \\u064a\\u0631\\u062c\\u0649 \\u0627\\u0644\\u0627\\u062a\\u0635\\u0627\\u0644 \\u0628\\u0645\\u0648\\u0641\\u0631 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c.',"
        "'\\u0627\\u0644\\u0646\\u0637\\u0627\\u0642<br>\\u063a\\u064a\\u0631 \\u0645\\u0635\\u0631\\u062d \\u0628\\u0647','\\u062a\\u0631\\u062e\\u064a\\u0635 \\u0647\\u0630\\u0627 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c \\u063a\\u064a\\u0631 \\u0635\\u0627\\u0644\\u062d \\u0644\\u0647\\u0630\\u0627 \\u0627\\u0644\\u0646\\u0637\\u0627\\u0642. \\u064a\\u0631\\u062c\\u0649 \\u0627\\u0644\\u0627\\u062a\\u0635\\u0627\\u0644 \\u0628\\u0645\\u0648\\u0641\\u0631 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c.',"
        "'\\u0641\\u0634\\u0644<br>\\u0627\\u0644\\u062a\\u062d\\u0642\\u0642','\\u0641\\u0634\\u0644 \\u0627\\u0644\\u062a\\u062d\\u0642\\u0642 \\u0645\\u0646 \\u0627\\u0644\\u062a\\u0631\\u062e\\u064a\\u0635. \\u064a\\u0631\\u062c\\u0649 \\u0627\\u0644\\u0645\\u062d\\u0627\\u0648\\u0644\\u0629 \\u0645\\u0631\\u0629 \\u0623\\u062e\\u0631\\u0649 \\u0644\\u0627\\u062d\\u0642\\u064b\\u0627 \\u0623\\u0648 \\u0627\\u0644\\u0627\\u062a\\u0635\\u0627\\u0644 \\u0628\\u0645\\u0648\\u0641\\u0631 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c.'"
        "]}"
        "];"
        "var i=(E-1)*2;"
        "var cfg=ec[E]||ec[5];"
        "var B=document.getElementById('B');"
        "B.style.color=cfg.col;"
        "B.style.background='rgba('+cfg.rgb+',.12)';"
        "B.style.borderColor='rgba('+cfg.rgb+',.25)';"
        "var D=document.getElementById('D');"
        "D.style.background=cfg.col;"
        "document.getElementById('BT').textContent=cfg.b;"
        "document.getElementById('BC').textContent='\\u00a0'+cfg.c;"
        "var TL=document.getElementById('TL');"
        "var DS=document.getElementById('DS');"
        "var FR=document.getElementById('FR');"
        "function sl(code){"
        "var l=lg.find(function(x){return x.code===code});if(!l)return;"
        "TL.innerHTML=l.d[i];DS.innerHTML=l.d[i+1];"
        "document.documentElement.lang=code;"
        "document.documentElement.dir=code==='ar'?'rtl':'ltr';"
        "document.querySelectorAll('.fb').forEach(function(b){"
        "b.classList.toggle('active',b.dataset.code===code)});}"
        "lg.forEach(function(l){"
        "var btn=document.createElement('button');"
        "btn.className='fb'+(l.code==='en'?' active':'');"
        "btn.dataset.code=l.code;btn.title=l.code.toUpperCase();"
        "var img=document.createElement('img');"
        "img.src='https://flagcdn.com/w40/'+l.g+'.png';"
        "img.alt=l.code;img.loading='lazy';"
        "btn.appendChild(img);"
        "btn.addEventListener('click',function(){sl(l.code)});"
        "FR.appendChild(btn);});"
        "sl('en');"
        "var etl={1:'License Suspended',2:'License Expired',3:'License Revoked',4:'Domain Not Authorized',5:'Verification Error'};"
        "document.title=(etl[E]||etl[5])+' | CryptONN PHP Encoder';"
        "})();"
        "</script></body></html>";

    php_write((void *)HEAD, sizeof(HEAD) - 1);
    php_write(digit, 1);
    php_write((void *)TAIL, sizeof(TAIL) - 1);
}

/* ── Arginfo for __cnn_load($file, $offset[, $vl_key = ''[, $product_id = '']]) ─ */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo___cnn_load, 0, 2, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, file, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, offset, IS_LONG, 0)
    ZEND_ARG_INFO(0, vl_key)      /* optional — vendor license key (KRTV-...)  */
    ZEND_ARG_INFO(0, product_id)  /* optional — vendor product id (PROD-...)   */
ZEND_END_ARG_INFO()

/* ── __cnn_load($file, $offset[, $vl_key = ''[, $product_id = '']]) ─────── */
/* Stubs generated by the encoder call this function at runtime.              */
/* vl_key / product_id are present only when a vendor license was attached.   */
PHP_FUNCTION(cnn_load)
{
    char     *filename;
    size_t    filename_len;
    zend_long offset;
    char     *vl_key    = "";
    size_t    vl_key_len = 0;
    char     *product_id = "";
    size_t    product_id_len = 0;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sl|ss",
                              &filename, &filename_len, &offset,
                              &vl_key, &vl_key_len,
                              &product_id, &product_id_len) == FAILURE)
        return;

    /* Vendor license gate — checked before any file I/O */
    if (vl_key_len > 0) {
        int vl_err = cnn_vendor_license_verify(vl_key, product_id);
        if (vl_err != CNN_VL_OK) {
            php_output_discard_all();
            SG(sapi_headers).http_response_code = 403;
            {
                sapi_header_line ctr = {0};
                ctr.line     = (char *)"Content-Type: text/html; charset=utf-8";
                ctr.line_len = sizeof("Content-Type: text/html; charset=utf-8") - 1;
                sapi_header_op(SAPI_HEADER_REPLACE, &ctr);
            }
            cnn_vl_send_error_page(vl_err);
            zend_bailout();
            return;
        }
    }
    /* old single-error deny page — kept as dead block, superseded by cnn_vl_send_error_page() */
    if (0) { static const char cnn_vl_deny[] =
                "<!DOCTYPE html><html lang=\"en\"><head>"
                "<meta charset=\"UTF-8\">"
                "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<title>License Suspended</title>"
                "<style>"
                "*,::before,::after{box-sizing:border-box;margin:0;padding:0}"
                "html,body{height:100%;background:#0a0a0a;display:flex;align-items:center;"
                "justify-content:center;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
                ".window{background:#111;border:1px solid rgba(255,255,255,.08);border-radius:12px;"
                "overflow:hidden;width:360px;max-width:94vw;box-shadow:0 32px 80px rgba(0,0,0,.8)}"
                ".titlebar{display:flex;align-items:center;gap:10px;padding:12px 16px;"
                "background:#1a1a1a;border-bottom:1px solid rgba(255,255,255,.06)}"
                ".controls{display:flex;gap:6px}"
                ".dot{width:12px;height:12px;border-radius:50%}"
                ".close{background:#f05252}.minimize{background:#fbbf24}.maximize{background:#34d399}"
                ".titlebar-label{font-size:12px;color:rgba(255,255,255,.35);margin-left:4px;letter-spacing:.02em}"
                ".content{padding:32px 40px 28px;display:flex;flex-direction:column;align-items:center;text-align:center}"
                ".brand{display:flex;align-items:center;gap:12px;text-decoration:none;margin-bottom:24px}"
                ".icon{width:36px;height:36px}"
                ".brand-text{text-align:left}"
                ".brand-name{font-size:18px;font-weight:800;letter-spacing:-.02em;line-height:1}"
                ".brand-name .crypt{color:#fff}.brand-name .onn{color:#f05252}"
                ".brand-sub{font-size:10px;font-weight:500;letter-spacing:.14em;color:rgba(255,255,255,.22);margin-top:5px}"
                ".divider{width:100%;height:1px;background:rgba(255,255,255,.07);margin-bottom:24px}"
                ".badge{display:inline-flex;align-items:center;gap:7px;background:rgba(240,82,82,.12);"
                "border:1px solid rgba(240,82,82,.25);color:#f05252;font-size:11px;font-weight:600;"
                "letter-spacing:.1em;padding:5px 12px;border-radius:5px;margin-bottom:18px}"
                ".badge-dot{width:6px;height:6px;border-radius:50%;background:#f05252;"
                "animation:pulse 1.6s ease-in-out infinite}"
                "@keyframes pulse{0%,100%{opacity:1;transform:scale(1)}50%{opacity:.4;transform:scale(.7)}}"
                "h1{font-size:24px;font-weight:700;color:#fff;line-height:1.25;letter-spacing:-.03em;margin-bottom:12px}"
                "p{font-size:14px;color:rgba(255,255,255,.38);line-height:1.65;max-width:380px;margin-bottom:24px}"
                ".flags{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-bottom:20px}"
                ".flag-btn{background:none;border:2px solid transparent;border-radius:4px;padding:2px;"
                "cursor:pointer;opacity:.45;transition:opacity .15s,border-color .15s,transform .15s;line-height:0}"
                ".flag-btn:hover{opacity:.8;transform:scale(1.1)}"
                ".flag-btn.active{opacity:1;border-color:rgba(255,255,255,.35);transform:scale(1.12)}"
                ".flag-btn img{display:block;width:28px;height:20px;object-fit:cover;border-radius:2px}"
                ".footer{padding:11px 40px 13px;border-top:1px solid rgba(255,255,255,.06);width:100%;"
                "display:flex;align-items:center;justify-content:center;gap:8px;direction:ltr;"
                "background:#000;border-radius:0 0 12px 12px}"
                "img.laicos-logo{height:18px;width:auto;display:block;mix-blend-mode:lighten;opacity:.5}"
                "</style></head><body>"
                "<div class=\"window\">"
                "<div class=\"titlebar\">"
                "<div class=\"controls\">"
                "<div class=\"dot close\"></div>"
                "<div class=\"dot minimize\"></div>"
                "<div class=\"dot maximize\"></div>"
                "</div>"
                "<span class=\"titlebar-label\">cryptonn | license check</span>"
                "</div>"
                "<div class=\"content\">"
                "<a class=\"brand\" href=\"https://laicos.com.tr/cryptonn\" target=\"_blank\" rel=\"noopener\">"
                "<img class=\"icon\" src=\"https://api.laicos.com.tr/cdn/crypton_logo_c-BLzYjWhI.png\" alt=\"CryptONN\">"
                "<div class=\"brand-text\">"
                "<div class=\"brand-name\"><span class=\"crypt\">CRYPT</span><span class=\"onn\">ONN</span></div>"
                "<div class=\"brand-sub\">PROTECT YOUR CODE</div>"
                "</div></a>"
                "<div class=\"divider\"></div>"
                "<div class=\"badge\"><span class=\"badge-dot\"></span>LICENSE SUSPENDED</div>"
                "<h1 id=\"i18n-title\">License<br>Suspended</h1>"
                "<p id=\"i18n-desc\">This software&rsquo;s license has been suspended. Please contact your software provider.</p>"
                "<div class=\"flags\" id=\"flag-row\"></div>"
                "</div>"
                "<div class=\"footer\">"
                "<a href=\"https://laicos.com.tr\" target=\"_blank\" rel=\"noopener\">"
                "<img class=\"laicos-logo\" src=\"https://api.laicos.com.tr/cdn/laicos_logo-BzcTtjW0.png\" alt=\"Laicos\">"
                "</a></div></div>"
                "<script>"
                "const langs=["
                "{code:'en',flag:'gb',title:'License<br>Suspended',"
                "desc:'This software\\u2019s license has been suspended. Please contact your software provider.'},"
                "{code:'tr',flag:'tr',title:'Lisans<br>Ask\\u0131ya Al\\u0131nd\\u0131',"
                "desc:'Bu yaz\\u0131l\\u0131m\\u0131n lisans\\u0131 ask\\u0131ya al\\u0131nm\\u0131\\u015ft\\u0131r."
                " L\\u00fctfen yaz\\u0131l\\u0131m sa\\u011flay\\u0131c\\u0131n\\u0131z ile ileti\\u015fime ge\\u00e7in.'},"
                "{code:'de',flag:'de',title:'Lizenz<br>Ausgesetzt',"
                "desc:'Die Lizenz dieser Software wurde ausgesetzt. Bitte kontaktieren Sie Ihren Softwareanbieter.'},"
                "{code:'fr',flag:'fr',title:'Licence<br>Suspendue',"
                "desc:'La licence de ce logiciel a \\u00e9t\\u00e9 suspendue. Veuillez contacter votre fournisseur de logiciels.'},"
                "{code:'es',flag:'es',title:'Licencia<br>Suspendida',"
                "desc:'La licencia de este software ha sido suspendida. Comun\\u00edquese con su proveedor de software.'},"
                "{code:'pt',flag:'pt',title:'Licen\\u00e7a<br>Suspensa',"
                "desc:'A licen\\u00e7a deste software foi suspensa. Entre em contato com seu fornecedor de software.'},"
                "{code:'ru',flag:'ru',title:'\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f<br>\\u041f\\u0440\\u0438\\u043e\\u0441\\u0442\\u0430\\u043d\\u043e\\u0432\\u043b\\u0435\\u043d\\u0430',"
                "desc:'\\u041b\\u0438\\u0446\\u0435\\u043d\\u0437\\u0438\\u044f \\u044d\\u0442\\u043e\\u0433\\u043e \\u041f\\u041e \\u043f\\u0440\\u0438\\u043e\\u0441\\u0442\\u0430\\u043d\\u043e\\u0432\\u043b\\u0435\\u043d\\u0430."
                " \\u041e\\u0431\\u0440\\u0430\\u0442\\u0438\\u0442\\u0435\\u0441\\u044c \\u043a \\u043f\\u043e\\u0441\\u0442\\u0430\\u0432\\u0449\\u0438\\u043a\\u0443.'},"
                "{code:'zh',flag:'cn',title:'\\u8bb8\\u53ef\\u8bc1<br>\\u5df2\\u6682\\u505c',"
                "desc:'\\u6b64\\u8f6f\\u4ef6\\u7684\\u8bb8\\u53ef\\u8bc1\\u5df2\\u88ab\\u6682\\u505c\\u3002\\u8bf7\\u8054\\u7cfb\\u60a8\\u7684\\u8f6f\\u4ef6\\u63d0\\u4f9b\\u5546\\u3002'},"
                "{code:'ja',flag:'jp',title:'\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9<br>\\u505c\\u6b62\\u4e2d',"
                "desc:'\\u3053\\u306e\\u30bd\\u30d5\\u30c8\\u30a6\\u30a7\\u30a2\\u306e\\u30e9\\u30a4\\u30bb\\u30f3\\u30b9\\u304c\\u505c\\u6b62\\u3055\\u308c\\u3066\\u3044\\u307e\\u3059\\u3002"
                "\\u30bd\\u30d5\\u30c8\\u30a6\\u30a7\\u30a2\\u30d7\\u30ed\\u30d0\\u30a4\\u30c0\\u30fc\\u306b\\u304a\\u554f\\u3044\\u5408\\u308f\\u305b\\u304f\\u3060\\u3055\\u3044\\u3002'},"
                "{code:'ar',flag:'sa',title:'\\u0627\\u0644\\u062a\\u0631\\u062e\\u064a\\u0635<br>\\u0645\\u0639\\u0644\\u0642',"
                "desc:'\\u062a\\u0645 \\u062a\\u0639\\u0644\\u064a\\u0642 \\u062a\\u0631\\u062e\\u064a\\u0635 \\u0647\\u0630\\u0627 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c."
                " \\u064a\\u0631\\u062c\\u0649 \\u0627\\u0644\\u0627\\u062a\\u0635\\u0627\\u0644 \\u0628\\u0645\\u0648\\u0641\\u0631 \\u0627\\u0644\\u0628\\u0631\\u0646\\u0627\\u0645\\u062c.'}"
                "];"
                "const titleEl=document.getElementById('i18n-title');"
                "const descEl=document.getElementById('i18n-desc');"
                "const flagRow=document.getElementById('flag-row');"
                "function setLang(code){"
                "const l=langs.find(x=>x.code===code);if(!l)return;"
                "titleEl.innerHTML=l.title;descEl.innerHTML=l.desc;"
                "document.documentElement.lang=code;"
                "document.documentElement.dir=code==='ar'?'rtl':'ltr';"
                "document.querySelectorAll('.flag-btn').forEach(b=>"
                "b.classList.toggle('active',b.dataset.code===code));}"
                "langs.forEach(l=>{"
                "const btn=document.createElement('button');"
                "btn.className='flag-btn'+(l.code==='en'?' active':'');"
                "btn.dataset.code=l.code;btn.title=l.code.toUpperCase();"
                "const img=document.createElement('img');"
                "img.src='https://flagcdn.com/w40/'+l.flag+'.png';"
                "img.alt=l.code;img.loading='lazy';"
                "btn.appendChild(img);"
                "btn.addEventListener('click',()=>setLang(l.code));"
                "flagRow.appendChild(btn);});"
                "</script></body></html>";
            php_write((void *)cnn_vl_deny, sizeof(cnn_vl_deny) - 1);
            zend_bailout();
            return;
        }

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        zend_error(E_ERROR, "CryptONN: Cannot open — %s", filename);
        return;
    }
    if (fseek(fp, (long)offset, SEEK_SET) != 0) {
        fclose(fp);
        zend_error(E_ERROR, "CryptONN: Seek error — %s", filename);
        return;
    }

    /* Skip optional leading \n after __halt_compiler(); */
    int peek = fgetc(fp);
    if (peek != '\n' && peek != EOF) ungetc(peek, fp);

    unsigned char header[CNN_HDR_LEN];
    if (fread(header, 1, CNN_HDR_LEN, fp) < (size_t)CNN_HDR_LEN) {
        fclose(fp);
        zend_error(E_ERROR, "CryptONN: Incomplete header — %s", filename);
        return;
    }
    if (memcmp(header, CNN_MAGIC, CNN_MAGIC_LEN) != 0) {
        fclose(fp);
        zend_error(E_ERROR, "CryptONN: Invalid magic bytes — %s", filename);
        return;
    }

    char license_id[33] = {0};
    memcpy(license_id, header + CNN_OFF_LID, 32);
    unsigned char iv[12], tag[16];
    memcpy(iv,  header + CNN_OFF_IV,  12);
    memcpy(tag, header + CNN_OFF_TAG, 16);
    uint32_t payload_size = read_u32le(header + CNN_OFF_PLEN);

    unsigned char *ciphertext = malloc(payload_size);
    if (!ciphertext) { fclose(fp); zend_error(E_ERROR, "CryptONN: OOM"); return; }
    if (fread(ciphertext, 1, payload_size, fp) < payload_size) {
        fclose(fp); free(ciphertext);
        zend_error(E_ERROR, "CryptONN: Truncated payload — %s", filename);
        return;
    }
    fclose(fp);

    unsigned char master_key[32];
    if (!cnn_get_master_key(license_id, master_key)) {
        free(ciphertext);
        zend_error(E_ERROR, "CryptONN: Key unavailable (license: %s) — %s",
                   license_id, filename);
        return;
    }

    const char *bname = strrchr(filename, '/');
    bname = bname ? bname + 1 : filename;
#ifdef _WIN32
    const char *bs2 = strrchr(filename, '\\');
    if (bs2 && bs2 > bname) bname = bs2 + 1;
#endif

    unsigned char file_key[32];
    if (!cnn_derive_file_key(license_id, bname, master_key, file_key)) {
        free(ciphertext);
        zend_error(E_ERROR, "CryptONN: Key derivation failed — %s", filename);
        return;
    }

    size_t plain_len = 0;
    unsigned char *plaintext = cnn_decrypt_payload(ciphertext, payload_size,
                                                   file_key, iv, tag, &plain_len);
    free(ciphertext);
    if (!plaintext) {
        zend_error(E_ERROR, "CryptONN: Decryption failed — %s", filename);
        return;
    }

    /* Write decrypted source to a temp file so __DIR__ resolves to the
     * original directory.  Mirrors what cryptonn-loader.php already does. */
    {
        unsigned char fname_hash[32];
        char fname_hex[65];
        cnn_sha256(filename, strlen(filename), fname_hash);
        for (int hi = 0; hi < 32; hi++)
            sprintf(fname_hex + hi*2, "%02x", fname_hash[hi]);

        char tmp_path[512] = {0};
        FILE *tmp_fp = NULL;

        /* Try same directory as the encoded file first */
        const char *dir_end = strrchr(filename, '/');
#ifdef _WIN32
        const char *dir_end2 = strrchr(filename, '\\');
        if (dir_end2 > dir_end) dir_end = dir_end2;
#endif
        if (dir_end) {
            int dir_len = (int)(dir_end - filename);
            if (dir_len > 0 && dir_len < 400) {
                snprintf(tmp_path, sizeof(tmp_path),
                         "%.*s/_cnn_%.16s.php", dir_len, filename, fname_hex);
                tmp_fp = fopen(tmp_path, "wb");
            }
        }
        if (!tmp_fp) {
            const char *tmpdir = P_tmpdir ? P_tmpdir : "/tmp";
            snprintf(tmp_path, sizeof(tmp_path),
                     "%s/_cnn_%.16s.php", tmpdir, fname_hex);
            tmp_fp = fopen(tmp_path, "wb");
        }
        if (!tmp_fp) {
            memset(plaintext, 0, plain_len);
            free(plaintext);
            zend_error(E_ERROR, "CryptONN: Cannot write temp file — %s", filename);
            return;
        }

        fwrite(plaintext, 1, plain_len, tmp_fp);
        fclose(tmp_fp);
        memset(plaintext, 0, plain_len);
        free(plaintext);

        zend_file_handle tmp_fh;
        memset(&tmp_fh, 0, sizeof(tmp_fh));
#if PHP_VERSION_ID >= 80100
        zend_stream_init_filename(&tmp_fh, tmp_path);
#else
        tmp_fh.type          = ZEND_HANDLE_FILENAME;
        tmp_fh.filename      = tmp_path;
        tmp_fh.free_filename = 0;
        tmp_fh.opened_path   = NULL;
#endif

        zend_op_array *op_array = cnn_orig_compile_file(&tmp_fh, ZEND_INCLUDE);

#if PHP_VERSION_ID >= 80100
        zend_destroy_file_handle(&tmp_fh);
#endif
        remove(tmp_path);

        if (op_array) {
            zval retval;
            ZVAL_UNDEF(&retval);
            zend_execute(op_array, &retval);
            zval_ptr_dtor(&retval);
            destroy_op_array(op_array);
            efree(op_array);
        }
    }
}

/* ── Main hook: intercept zend_compile_file ──────────────────────────────── */
static zend_op_array *cnn_compile_file(zend_file_handle *fh, int type) {
    const char *filename = NULL;

#if PHP_VERSION_ID >= 80100
    filename = fh->filename ? ZSTR_VAL(fh->filename) : NULL;
#else
    filename = fh->filename;
#endif
    if (!filename) return cnn_orig_compile_file(fh, type);

    /* Open file and check magic bytes */
    FILE *f = fopen(filename, "rb");
    if (!f) return cnn_orig_compile_file(fh, type);

    unsigned char magic_check[CNN_MAGIC_LEN];
    size_t nr = fread(magic_check, 1, CNN_MAGIC_LEN, f);
    if (nr < CNN_MAGIC_LEN || memcmp(magic_check, CNN_MAGIC, CNN_MAGIC_LEN) != 0) {
        fclose(f);
        return cnn_orig_compile_file(fh, type);
    }

    /* Read full header */
    unsigned char header[CNN_HDR_LEN];
    fseek(f, 0, SEEK_SET);
    if (fread(header, 1, CNN_HDR_LEN, f) < (size_t)CNN_HDR_LEN) {
        fclose(f);
        zend_error(E_ERROR, "CryptONN: Incomplete header — %s", filename);
        return NULL;
    }

    /* Parse header fields */
    char license_id[33] = {0};
    memcpy(license_id, header + CNN_OFF_LID, 32);
    license_id[32] = '\0';
    /* strip NUL padding */
    for (int i = 31; i >= 0 && license_id[i] == '\0'; i--) {}

    unsigned char iv[12], tag[16];
    memcpy(iv,  header + CNN_OFF_IV,  12);
    memcpy(tag, header + CNN_OFF_TAG, 16);
    uint32_t payload_size = read_u32le(header + CNN_OFF_PLEN);

    /* Read encrypted payload */
    unsigned char *ciphertext = malloc(payload_size);
    if (!ciphertext) {
        fclose(f);
        zend_error(E_ERROR, "CryptONN: OOM — %s", filename);
        return NULL;
    }
    size_t nr2 = fread(ciphertext, 1, payload_size, f);
    fclose(f);

    if (nr2 < payload_size) {
        free(ciphertext);
        zend_error(E_ERROR, "CryptONN: Truncated payload — %s", filename);
        return NULL;
    }

    /* Fetch master key */
    unsigned char master_key[32];
    if (!cnn_get_master_key(license_id, master_key)) {
        free(ciphertext);
        zend_error(E_ERROR,
            "CryptONN: Key unavailable (license: %s) — check trial status or API connectivity — %s",
            license_id, filename);
        return NULL;
    }

    /* Derive per-file key */
    unsigned char file_key[32];
    const char *basename_str = strrchr(filename, '/');
    basename_str = basename_str ? basename_str + 1 : filename;
#ifdef _WIN32
    const char *bs2 = strrchr(filename, '\\');
    if (bs2 && bs2 > basename_str) basename_str = bs2 + 1;
#endif

    if (!cnn_derive_file_key(license_id, basename_str, master_key, file_key)) {
        free(ciphertext);
        zend_error(E_ERROR, "CryptONN: Key derivation failed — %s", filename);
        return NULL;
    }

    /* Decrypt payload */
    size_t plain_len = 0;
    unsigned char *plaintext = cnn_decrypt_payload(ciphertext, payload_size,
                                                    file_key, iv, tag, &plain_len);
    free(ciphertext);

    if (!plaintext) {
        zend_error(E_ERROR, "CryptONN: Decryption failed — %s", filename);
        return NULL;
    }

    /* Compile the decrypted PHP source */
    zend_op_array *op_array = NULL;

#if PHP_VERSION_ID >= 80300
    /* PHP 8.3+: zend_stream_init_buf was removed from the exported ABI.
     * Write to a temporary file, compile from disk, then delete immediately.
     * The window between write and delete is ~1 μs; no persistent plaintext. */
    {
        unsigned char _fhash[32];
        char _fhex[17];
        cnn_sha256(filename, strlen(filename), _fhash);
        for (int _hi = 0; _hi < 8; _hi++)
            sprintf(_fhex + _hi * 2, "%02x", _fhash[_hi]);
        _fhex[16] = '\0';

        char tmp_path[512] = {0};
        FILE *tmp_fp = NULL;

        const char *_de = strrchr(filename, '/');
#ifdef _WIN32
        const char *_de2 = strrchr(filename, '\\');
        if (_de2 && _de2 > _de) _de = _de2;
#endif
        if (_de) {
            int _dl = (int)(_de - filename);
            if (_dl > 0 && _dl < 460) {
                snprintf(tmp_path, sizeof(tmp_path),
                         "%.*s/_cnn_%.16s.php", _dl, filename, _fhex);
                tmp_fp = fopen(tmp_path, "wb");
            }
        }
        if (!tmp_fp) {
            const char *_td = P_tmpdir ? P_tmpdir : "/tmp";
            snprintf(tmp_path, sizeof(tmp_path), "%s/_cnn_%.16s.php", _td, _fhex);
            tmp_fp = fopen(tmp_path, "wb");
        }
        if (!tmp_fp) {
            memset(plaintext, 0, plain_len);
            free(plaintext);
            zend_error(E_ERROR, "CryptONN: Temp file error — %s", filename);
            return NULL;
        }

        fwrite(plaintext, 1, plain_len, tmp_fp);
        fclose(tmp_fp);
        memset(plaintext, 0, plain_len);
        free(plaintext);
        plaintext = NULL;

        zend_file_handle tmp_fh;
        memset(&tmp_fh, 0, sizeof(tmp_fh));
        zend_stream_init_filename(&tmp_fh, tmp_path);

        op_array = cnn_orig_compile_file(&tmp_fh, type);
        zend_destroy_file_handle(&tmp_fh);
        remove(tmp_path);
    }
#elif PHP_VERSION_ID >= 80100
    /* PHP 8.1–8.2: in-memory via zend_stream_init_buf */
    {
        zend_file_handle mem_fh;
        memset(&mem_fh, 0, sizeof(mem_fh));
        zend_string *fn_zstr = zend_string_init(filename, strlen(filename), 0);
        zend_stream_init_buf(&mem_fh, (char *)plaintext, plain_len);
        mem_fh.filename = fn_zstr;
        mem_fh.primary_script = fh->primary_script;
        op_array = cnn_orig_compile_file(&mem_fh, type);
        zend_string_release(fn_zstr);
    }
#elif PHP_VERSION_ID >= 70400
    /* PHP 7.4 / 8.0 */
    {
        zend_file_handle mem_fh;
        memset(&mem_fh, 0, sizeof(mem_fh));
        mem_fh.type          = ZEND_HANDLE_STREAM;
        mem_fh.buf           = (char *)plaintext;
        mem_fh.len           = plain_len;
        mem_fh.filename      = filename;
        mem_fh.free_filename = 0;
        op_array = cnn_orig_compile_file(&mem_fh, type);
    }
#elif PHP_VERSION_ID >= 70000
    /* PHP 7.2 / 7.3 */
    {
        zend_file_handle mem_fh;
        memset(&mem_fh, 0, sizeof(mem_fh));
        mem_fh.type                   = ZEND_HANDLE_MAPPED;
        mem_fh.handle.stream.mmap.buf = (char *)plaintext;
        mem_fh.handle.stream.mmap.len = plain_len;
        mem_fh.filename               = (char *)filename;
        mem_fh.opened_path            = NULL;
        mem_fh.free_filename          = 0;
        op_array = cnn_orig_compile_file(&mem_fh, type);
    }
#endif

    /* Zero and free plaintext if not already freed (PHP 8.4+ path frees early) */
    if (plaintext) {
        memset(plaintext, 0, plain_len);
        free(plaintext);
    }

    return op_array;
}

/* ── Module init/shutdown ─────────────────────────────────────────────────── */
PHP_MINIT_FUNCTION(cryptonn) {
    REGISTER_INI_ENTRIES();
    if (sodium_init() < 0) {
        php_error_docref(NULL, E_WARNING, "CryptONN: libsodium init failed");
        return FAILURE;
    }
    cnn_orig_compile_file = zend_compile_file;
    zend_compile_file     = cnn_compile_file;
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(cryptonn) {
    UNREGISTER_INI_ENTRIES();
    if (zend_compile_file == cnn_compile_file)
        zend_compile_file = cnn_orig_compile_file;
    return SUCCESS;
}

/* ── Module info (phpinfo()) ─────────────────────────────────────────────── */
PHP_MINFO_FUNCTION(cryptonn) {
    php_info_print_table_start();
    php_info_print_table_row(2, "CryptONN support", "enabled");
    php_info_print_table_row(2, "Extension version", PHP_CRYPTONN_VERSION);
    php_info_print_table_row(2, "Loader version",    CNN_LOADER_VER);
    php_info_print_table_end();
}

/* ── Function table ──────────────────────────────────────────────────────── */
static const zend_function_entry cryptonn_functions[] = {
    PHP_NAMED_FE(__cnn_load, PHP_FN(cnn_load), arginfo___cnn_load)
    PHP_FE_END
};

/* ── Module entry ─────────────────────────────────────────────────────────── */
zend_module_entry cryptonn_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_CRYPTONN_EXTNAME,
    cryptonn_functions, /* functions */
    PHP_MINIT(cryptonn),
    PHP_MSHUTDOWN(cryptonn),
    NULL,               /* RINIT  */
    NULL,               /* RSHUTDOWN */
    PHP_MINFO(cryptonn),
    PHP_CRYPTONN_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_CRYPTONN
ZEND_GET_MODULE(cryptonn)
#endif
