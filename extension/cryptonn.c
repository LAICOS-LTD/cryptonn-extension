/*
 * CryptONN PHP Extension v1.0.0
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
#include "php_cryptonn.h"

#include <openssl/evp.h>
#include <openssl/sha.h>
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
    SHA256((unsigned char *)buf, strlen(buf), digest);

    for (int i = 0; i < 32; i++)
        sprintf(out_hex + i * 2, "%02x", digest[i]);
    out_hex[64] = '\0';
}

/* ── Delivery key: SHA-256(license_id_upper + "|" + fingerprint) ─────────── */
static void cnn_delivery_key(const char *lid_upper, const char *fp, unsigned char out[32]) {
    char buf[512] = {0};
    snprintf(buf, sizeof(buf), "%s|%s", lid_upper, fp);
    SHA256((unsigned char *)buf, strlen(buf), out);
}

/* ── File-cache path: /tmp/_cnn_mk_<first16hex(sha256(lid))>.json ────────── */
static void cnn_cache_path(const char *lid_upper, char out[256]) {
    unsigned char digest[32];
    SHA256((unsigned char *)lid_upper, strlen(lid_upper), digest);
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

/* ── File cache: read ─────────────────────────────────────────────────────── */
/* Returns 1 if fresh cache hit, fills master_key_out.                       */
/* Sets *trial_expired=1 if cached trial_ends_at has passed (no key).        */
static int cnn_file_cache_read(
    const char          *cache_path,
    const unsigned char *delivery_key,
    unsigned char        master_key_out[32],
    int                 *trial_expired,
    int                  grace          /* 1 = allow up to 72h stale */
) {
    *trial_expired = 0;
    struct stat st;
    if (stat(cache_path, &st) != 0) return 0;

    time_t age = time(NULL) - st.st_mtime;
    long max_age = grace ? CNN_CACHE_TTL_GRACE : CNN_CACHE_TTL_FRESH;
    if (age > max_age) return 0;

    FILE *f = fopen(cache_path, "r");
    if (!f) return 0;

    char *json = NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0 && sz < 16384) {
        json = malloc(sz + 1);
        if (json) {
            fread(json, 1, sz, f);
            json[sz] = '\0';
        }
    }
    fclose(f);
    if (!json) return 0;

    /* Trial expiry check */
    int ts_found = 0;
    long trial_ends_at = json_long(json, "trial_ends_at", &ts_found);
    if (ts_found && trial_ends_at > 0 && time(NULL) > trial_ends_at) {
        free(json);
        *trial_expired = 1;
        return 0;
    }

    /* Compute AAD — mirrors what the API used when encrypting */
    char aad[32] = {0};
    if (ts_found && trial_ends_at > 0)
        snprintf(aad, sizeof(aad), "%ld", trial_ends_at);

    int ok = cnn_parse_and_decrypt_bundle(json, delivery_key, aad, master_key_out);
    free(json);
    return ok;
}

/* ── File cache: write ────────────────────────────────────────────────────── */
static void cnn_file_cache_write(
    const char *cache_path,
    const char *bundle_json,  /* raw customer_key JSON object */
    long        trial_ends_at /* 0 = non-trial */
) {
    FILE *f = fopen(cache_path, "w");
    if (!f) return;
    /* Append trial_ends_at to the bundle JSON */
    /* Input: {"enc":"...","iv":"...","tag":"...","algorithm":"aes-256-gcm"} */
    /* Find closing brace and insert field before it */
    size_t blen = strlen(bundle_json);
    if (blen > 0 && bundle_json[blen - 1] == '}') {
        fwrite(bundle_json, 1, blen - 1, f);
        if (trial_ends_at > 0)
            fprintf(f, ",\"trial_ends_at\":%ld}", trial_ends_at);
        else
            fprintf(f, ",\"trial_ends_at\":null}");
    } else {
        fwrite(bundle_json, 1, blen, f);
    }
    fclose(f);
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

    char cache_path[256];
    cnn_cache_path(lid, cache_path);

    /* ── Layer 2: file cache (fresh ≤ 24 h) ─────────────────────────────── */
    int trial_expired = 0;
    if (cnn_file_cache_read(cache_path, delivery_key, out, &trial_expired, 0)) {
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
                            cnn_file_cache_write(cache_path, bundle_json, trial_ends_at);
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
    if (cnn_file_cache_read(cache_path, delivery_key, out, &trial_expired, 1)) {
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

    /* Compile the decrypted PHP source in memory */
    zend_op_array *op_array = NULL;
    {
        zend_file_handle mem_fh;
        memset(&mem_fh, 0, sizeof(mem_fh));

#if PHP_VERSION_ID >= 80100
        zend_string *fn_zstr = zend_string_init(filename, strlen(filename), 0);
        zend_stream_init_buf(&mem_fh, (char *)plaintext, plain_len);
        mem_fh.filename = fn_zstr;
        mem_fh.primary_script = fh->primary_script;
#elif PHP_VERSION_ID >= 70000
        mem_fh.type           = ZEND_HANDLE_MAPPED;
        mem_fh.handle.stream.mmap.buf     = (char *)plaintext;
        mem_fh.handle.stream.mmap.len     = plain_len;
        mem_fh.filename       = (char *)filename;
        mem_fh.opened_path    = NULL;
        mem_fh.free_filename  = 0;
#endif

        op_array = cnn_orig_compile_file(&mem_fh, type);

#if PHP_VERSION_ID >= 80100
        zend_string_release(fn_zstr);
#endif
    }

    /* Zero and free plaintext — don't leave decrypted PHP in memory */
    memset(plaintext, 0, plain_len);
    free(plaintext);

    return op_array;
}

/* ── Module init/shutdown ─────────────────────────────────────────────────── */
PHP_MINIT_FUNCTION(cryptonn) {
    if (sodium_init() < 0) {
        php_error_docref(NULL, E_WARNING, "CryptONN: libsodium init failed");
        return FAILURE;
    }
    cnn_orig_compile_file = zend_compile_file;
    zend_compile_file     = cnn_compile_file;
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(cryptonn) {
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

/* ── Module entry ─────────────────────────────────────────────────────────── */
zend_module_entry cryptonn_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_CRYPTONN_EXTNAME,
    NULL,               /* functions */
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
