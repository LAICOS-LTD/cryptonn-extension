#ifndef PHP_CRYPTONN_H
#define PHP_CRYPTONN_H

#define PHP_CRYPTONN_EXTNAME  "cryptonn"
#define PHP_CRYPTONN_VERSION  "1.0.0"

/* Magic bytes: "CRYPTNN\x01\x00" (9 bytes) */
#define CNN_MAGIC          "CRYPTNN\x01\x00"
#define CNN_MAGIC_LEN      9

/* Header layout (total 85 bytes after magic) */
#define CNN_HDR_LEN        85
#define CNN_OFF_FMT_VER    9    /* u32 LE — format version          */
#define CNN_OFF_PHP_ABI    13   /* u32 LE — php abi                 */
#define CNN_OFF_ENC_VER    17   /* u32 LE — encoder version         */
#define CNN_OFF_LID        21   /* 32 bytes — license_id (NUL-pad)  */
#define CNN_OFF_IV         53   /* 12 bytes — AES-GCM nonce         */
#define CNN_OFF_TAG        65   /* 16 bytes — AES-GCM auth tag      */
#define CNN_OFF_PLEN       81   /* u32 LE — payload size            */

/* Cache TTLs */
#define CNN_CACHE_TTL_FRESH   86400    /* 24 h — file cache valid      */
#define CNN_CACHE_TTL_GRACE   259200   /* 72 h — stale grace period    */
#define CNN_CACHE_TTL_APCU    3600     /* 1  h — APCu default TTL      */
#define CNN_CACHE_TTL_APCU_MIN 60      /* minimum APCu TTL             */

/* API */
#define CNN_API_DEFAULT    "https://api.cryptonn.com"
#define CNN_LOADER_VER     "ext-1.0.0"
#define CNN_API_TIMEOUT    8L

extern zend_module_entry cryptonn_module_entry;
#define phpext_cryptonn_ptr &cryptonn_module_entry

PHP_MINIT_FUNCTION(cryptonn);
PHP_MSHUTDOWN_FUNCTION(cryptonn);

#endif /* PHP_CRYPTONN_H */
