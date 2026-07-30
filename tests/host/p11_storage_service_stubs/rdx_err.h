#ifndef __P11_STORAGE_SERVICE_RDX_ERR_H__
#define __P11_STORAGE_SERVICE_RDX_ERR_H__

typedef enum {
    RDX_OK = 0,
    RDX_ERR_IO = -5,
    RDX_ERR_NOMEM = -12,
    RDX_ERR_INVAL = -22,
    RDX_ERR_NOTSUP = -95,
} rdx_err_t;

#endif
