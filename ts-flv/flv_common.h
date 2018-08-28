
#ifndef _FLV_COMMON_H
#define _FLV_COMMON_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>

#include "flv_tag.h"


#define REMOVE_HEADER(bufp, hdr_loc, name, len) \
    do { \
        TSMLoc the_loc; \
        the_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name, len); \
        if (the_loc != TS_NULL_MLOC) { \
            TSMimeHdrFieldDestroy(bufp, hdr_loc, the_loc); \
            TSHandleMLocRelease(bufp, hdr_loc, the_loc); \
        } \
    } while (0)

class IOHandle
{
public:
    IOHandle(): vio(NULL), buffer(NULL), reader(NULL)
    {
    }

    ~IOHandle()
    {
        if (reader) {
            TSIOBufferReaderFree(reader);
            reader = NULL;
        }

        if (buffer) {
            TSIOBufferDestroy(buffer);
            buffer = NULL;
        }
    }

public:
    TSVIO               vio;
    TSIOBuffer          buffer;
    TSIOBufferReader    reader;
};


class FlvTransformContext
{
public:
    FlvTransformContext(double s, double e, int64_t n, char *url, void *ih): total(0), parse_over(false)
    {
        res_buffer = TSIOBufferCreate();
        res_reader = TSIOBufferReaderAlloc(res_buffer);

        // change by dnion
        ftag.start = s;
        ftag.end = e;
        ftag.cl = n;
        ftag.url = url;
        ftag.config = (FlvConfig*)ih;
        if ((int64_t)ftag.start >= ftag.cl)
        {
            ftag.start = 0;
        }
    }

    ~FlvTransformContext()
    {
        if (res_reader) {
            TSIOBufferReaderFree(res_reader);
        }

        if (res_buffer) {
            TSIOBufferDestroy(res_buffer);
        }
    }

public:
    IOHandle            output;
    TSIOBuffer          res_buffer;
    TSIOBufferReader    res_reader;
    FlvTag              ftag;

    int64_t             total;
    bool                parse_over;
};


class FlvContext
{
public:
    FlvContext(double s, double e): start(s), end(e), cl(0), transform_added(false),
        url(NULL), ftc(NULL), ih(NULL)
    {
    }

    ~FlvContext()
    {
        if (ftc) {
            delete ftc;
            ftc = NULL;
        }
        if (url)
        {
            TSfree(url);
            url = NULL;
        }
    }

public:
    double      start;
    double      end;
    int64_t     cl;
    bool        transform_added;
    char        *url;

    FlvTransformContext *ftc;
    void        *ih;
};

#endif

