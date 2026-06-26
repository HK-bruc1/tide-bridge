#ifndef __RDX_ERR_H__
#define __RDX_ERR_H__

typedef enum {
    RDX_OK          = 0,
    RDX_ERR_INVAL   = -22,   /* -EINVAL */
    RDX_ERR_TIMEOUT = -110,  /* -ETIMEDOUT */
    RDX_ERR_NOMEM   = -12,   /* -ENOMEM */
    RDX_ERR_NOTSUP  = -95,   /* -ENOTSUP */
    RDX_ERR_IO      = -5,    /* -EIO */
    RDX_ERR_NOENT   = -2,    /* -ENOENT */
    RDX_ERR_BUSY    = -16,   /* -EBUSY */
    RDX_ERR_AGAIN   = -11,   /* -EAGAIN */
} rdx_err_t;

#define RDX_IS_ERR(x)   ((x) < 0)

#endif
