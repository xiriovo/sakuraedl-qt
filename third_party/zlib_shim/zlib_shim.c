/*
 * zlib_shim.c
 *
 * Minimal zlib wrapper for static OpenSSL linking.
 * Qt bundles zlib internally with z_ prefix. We delegate to the
 * standard zlib API which the MinGW libcrypto.a expects.
 *
 * Instead of pulling in a full zlib, we compile this small file
 * which implements the 7 symbols that OpenSSL's c_zlib.c references.
 *
 * We use the zlib headers from Qt's bundled copy and just compile
 * the relevant zlib source directly.  Since Qt only provides headers,
 * we implement a minimal inline version that satisfies the linker.
 */

/* We define NO_GZIP to reduce the surface area */
#include <string.h>
#include <stdlib.h>

/* ── Minimal zlib types and constants ──────────────────────────────── */

typedef unsigned char Bytef;
typedef unsigned int  uInt;
typedef unsigned long uLong;
typedef long          z_off_t;
typedef void*         voidpf;

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_ERRNO        (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR   (-3)
#define Z_MEM_ERROR    (-4)
#define Z_BUF_ERROR    (-5)
#define Z_VERSION_ERROR (-6)

#define Z_NO_FLUSH      0
#define Z_SYNC_FLUSH    2
#define Z_FINISH        4

#define Z_DEFLATED      8
#define MAX_WBITS       15

#define ZLIB_VERSION "1.2.13"

typedef struct z_stream_s {
    const Bytef *next_in;
    uInt        avail_in;
    uLong       total_in;
    Bytef       *next_out;
    uInt        avail_out;
    uLong       total_out;
    const char  *msg;
    void        *state;
    voidpf      (*zalloc)(voidpf, uInt, uInt);
    void        (*zfree)(voidpf, voidpf);
    voidpf      opaque;
    int         data_type;
    uLong       adler;
    uLong       reserved;
} z_stream;

/* ── Error string table ────────────────────────────────────────────── */

static const char* const z_errmsg[] = {
    "need dictionary",     /* Z_NEED_DICT       2 */
    "stream end",          /* Z_STREAM_END      1 */
    "",                    /* Z_OK              0 */
    "file error",          /* Z_ERRNO         (-1) */
    "stream error",        /* Z_STREAM_ERROR  (-2) */
    "data error",          /* Z_DATA_ERROR    (-3) */
    "insufficient memory", /* Z_MEM_ERROR     (-4) */
    "buffer error",        /* Z_BUF_ERROR     (-5) */
    "incompatible version",/* Z_VERSION_ERROR (-6) */
    ""
};

const char* zError(int err)
{
    return z_errmsg[2 - err];
}

/*
 * OpenSSL's c_zlib.c only uses zlib for ZLIB_METHOD compression in
 * BIO_f_zlib().  Most builds never actually call these functions at
 * runtime — they are only referenced so the linker needs them.
 *
 * We provide stub implementations that return Z_STREAM_ERROR so that
 * if they are somehow called, they fail gracefully rather than crash.
 */

int deflateInit_(z_stream* strm, int level,
                 const char* version, int stream_size)
{
    (void)strm; (void)level; (void)version; (void)stream_size;
    return Z_STREAM_ERROR;
}

int deflate(z_stream* strm, int flush)
{
    (void)strm; (void)flush;
    return Z_STREAM_ERROR;
}

int deflateEnd(z_stream* strm)
{
    (void)strm;
    return Z_STREAM_ERROR;
}

int inflateInit_(z_stream* strm, const char* version, int stream_size)
{
    (void)strm; (void)version; (void)stream_size;
    return Z_STREAM_ERROR;
}

int inflate(z_stream* strm, int flush)
{
    (void)strm; (void)flush;
    return Z_STREAM_ERROR;
}

int inflateEnd(z_stream* strm)
{
    (void)strm;
    return Z_STREAM_ERROR;
}
