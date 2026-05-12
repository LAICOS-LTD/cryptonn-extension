#ifndef PHP_CRYPTONN_H
#define PHP_CRYPTONN_H

#define PHP_CRYPTONN_EXTNAME  "cryptonn"
#define PHP_CRYPTONN_VERSION  "1.2.0"

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

/* Machine secret — hex path in php.ini: cryptonn.machine_key = <64 hex chars> */
#define CNN_MACHINE_KEY_INI  "cryptonn.machine_key"
#define CNN_MACHINE_KEY_FILE "/etc/cryptonn/machine.key"

/* Cache TTLs — master key */
#define CNN_CACHE_TTL_FRESH   86400    /* 24 h — file cache valid      */
#define CNN_CACHE_TTL_GRACE   259200   /* 72 h — stale grace period    */
#define CNN_CACHE_TTL_APCU    3600     /* 1  h — APCu default TTL      */
#define CNN_CACHE_TTL_APCU_MIN 60      /* minimum APCu TTL             */

/* Cache TTLs — vendor license */
#define CNN_VL_TTL_VALID      900      /* 15 min — valid result        */
#define CNN_VL_TTL_INVALID    300      /*  5 min — invalid result      */
#define CNN_VL_TTL_GRACE      7200     /*  2 h   — grace (valid only)  */

/* Vendor license error types — returned by cnn_vendor_license_verify() */
#define CNN_VL_OK         0   /* license valid                    */
#define CNN_VL_SUSPENDED  1   /* license suspended        CR-403  */
#define CNN_VL_EXPIRED    2   /* license expired          CR-402  */
#define CNN_VL_REVOKED    3   /* license revoked          CR-401  */
#define CNN_VL_DOMAIN     4   /* domain not authorised    CR-403  */
#define CNN_VL_GENERAL    5   /* network / unknown error  CR-501  */

/* API */
#define CNN_API_DEFAULT    "https://api.laicos.com.tr"
#define CNN_LOADER_VER     "ext-1.2.0"
#define CNN_API_TIMEOUT    8L

extern zend_module_entry cryptonn_module_entry;
#define phpext_cryptonn_ptr &cryptonn_module_entry

PHP_MINIT_FUNCTION(cryptonn);
PHP_MSHUTDOWN_FUNCTION(cryptonn);

#endif /* PHP_CRYPTONN_H */
