dnl CryptONN PHP Extension
dnl config.m4 — phpize / PHP build system

PHP_ARG_ENABLE([cryptonn],
  [whether to enable cryptonn support],
  [AS_HELP_STRING([--enable-cryptonn], [Enable CryptONN loader extension])])

if test "$PHP_CRYPTONN" != "no"; then

  dnl ── libcurl ───────────────────────────────────────────────────────────────
  PHP_CHECK_LIBRARY(curl, curl_easy_init,
    [PHP_ADD_LIBRARY(curl, 1, CRYPTONN_SHARED_LIBADD)],
    [AC_MSG_ERROR([libcurl not found. Install libcurl-dev / curl-devel.])]
  )

  dnl ── libsodium (BLAKE2b key derivation) ───────────────────────────────────
  PHP_CHECK_LIBRARY(sodium, sodium_init,
    [PHP_ADD_LIBRARY(sodium, 1, CRYPTONN_SHARED_LIBADD)],
    [AC_MSG_ERROR([libsodium not found. Install libsodium-dev.])]
  )

  dnl ── OpenSSL (AES-256-GCM) ─────────────────────────────────────────────────
  PHP_CHECK_LIBRARY(ssl, SSL_library_init,
    [PHP_ADD_LIBRARY(ssl, 1, CRYPTONN_SHARED_LIBADD)],
    [dnl OpenSSL 1.1+ doesn't expose SSL_library_init — try EVP_EncryptInit_ex
     PHP_CHECK_LIBRARY(crypto, EVP_EncryptInit_ex,
       [PHP_ADD_LIBRARY(crypto, 1, CRYPTONN_SHARED_LIBADD)],
       [AC_MSG_ERROR([OpenSSL not found. Install libssl-dev / openssl-devel.])]
     )
    ]
  )
  PHP_ADD_LIBRARY(crypto, 1, CRYPTONN_SHARED_LIBADD)

  PHP_SUBST(CRYPTONN_SHARED_LIBADD)
  PHP_NEW_EXTENSION(cryptonn, cryptonn.c, $ext_shared)
fi
