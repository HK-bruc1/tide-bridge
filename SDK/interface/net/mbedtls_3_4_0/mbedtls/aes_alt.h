#ifndef MBEDTLS_AES_ALT_H
#define MBEDTLS_AES_ALT_H

#ifdef MBEDTLS_AES_ALT
#ifdef __cplusplus
extern "C" {
#endif


typedef unsigned char uint8_t;
#include "asm/aes_hw.h"
typedef struct {
    uint8_t key_len;
    uint8_t key[32];
    uint8_t xts_setkey;

    uint32_t aes_key[8];
    uint32_t aes_key2[8];
    uint32_t           ctx_save_cr; /* save context for multi-context  */
    CRYP_HandleTypeDef hcryp_aes;   //AES context
} mbedtls_aes_context;

#if 1//defined(MBEDTLS_CIPHER_MODE_XTS)
/**
 *\brief The AES XTS context-type definition.
 */
typedef struct mbedtls_aes_xts_context {
    mbedtls_aes_context crypt; /*!< The AES context to use for AES block
												  encryption or decryption. */
    mbedtls_aes_context tweak; /*!< The AES context used for tweak
												  computation. */
} mbedtls_aes_xts_context;
#endif /* MBEDTLS_CIPHER_MODE_XTS */


#endif

#ifdef __cplusplus
}
#endif

#endif/* aes_alt.h */
