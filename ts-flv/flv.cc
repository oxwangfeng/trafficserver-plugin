
#include "flv_common.h"
#include <math.h>


char * ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len);
static int flv_handler(TSCont contp, TSEvent event, void *edata);
static void flv_cache_lookup_complete(FlvContext *fc, TSHttpTxn txnp);
static void flv_reset_request_url(TSHttpTxn txnp);
static void flv_read_response(FlvContext *fc, TSHttpTxn txnp);
static void flv_add_transform(FlvContext *fc, TSHttpTxn txnp);
static int flv_transform_entry(TSCont contp, TSEvent event, void *edata);
static int flv_transform_handler(TSCont contp, FlvContext *fc);


TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
    if (!api_info)
        return TS_ERROR;

    if (api_info->size < sizeof(TSRemapInterface))
        return TS_ERROR;

    // Zerkkro
    int txn_slot = -1;
    if (TSHttpArgIndexReserve("ts_flv", NULL, &txn_slot) != TS_SUCCESS)
    {
        TSError("[ts_flv] failed to reserve private data slot");
    }

    return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char* argv[], void** ih, char* errbuf, int errbuf_size)
{
    int         i;
    char        *pos;
    FlvConfig *flv_config = (FlvConfig*)TSmalloc(sizeof(FlvConfig));

    flv_config->type = FLV_TYPE_BYTES;
    flv_config->use_metadata = 1;
    strcpy(flv_config->start_arg, "start");
    strcpy(flv_config->end_arg, "end");

    if (argc > 2)
    {
        for (i = 2; i < argc; i++)
        {
            if (argv[i][1] != ':')
                continue;

            if (argv[i][0] == 't')
            {
                if (strncmp(argv[i], "t:time", sizeof("time") - 1) == 0)
                {
                    flv_config->type = FLV_TYPE_TIME;
                }
            }
            else if (argv[i][0] == 'm')
            {
                if (argv[i][2] == '0')
                {
                    flv_config->use_metadata = 0;
                }
            }
            else if (argv[i][0] == 's')
            {
                pos = (char*)argv[i];
                pos += 2;
                strncpy(flv_config->start_arg, pos, 23);
                flv_config->start_arg[23] = 0;
            }
            else if (argv[i][0] == 'e')
            {
                pos = (char*)argv[i];
                pos += 2;
                strncpy(flv_config->end_arg, pos, 23);
                flv_config->end_arg[23] = 0;
            }
        }
    }

    *ih = (void*)flv_config;
    return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void* ih)
{
    if (ih)
    {
        FlvConfig *flv_config = (FlvConfig*)ih;
        TSfree(flv_config);
    }
    return;
}

TSRemapStatus
TSRemapDoRemap(void* ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
    const char          *method, *query;
    char                *url;
    int                 method_len, query_len, url_len;
    size_t              val_len;
    const char          *start_val, *val;
    int                 ret;
    // float精度不够, 改用double
    // change by dnion
    double               start, end;
    char                buf[1024], buf_end[1024];
    int                 buf_len, buf_len_end;
    int                 left, right;
    char                *pointer_query = NULL;
    TSCont              contp;
    FlvContext          *fc;
    FlvConfig           *flv_config;

    if (ih == NULL)
    {
        return TSREMAP_NO_REMAP;
    }
    flv_config = (FlvConfig*)ih;

    method = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);
    if (method != TS_HTTP_METHOD_GET) {
        return TSREMAP_NO_REMAP;
    }

    start = 0;
    end = 0;
    query = TSUrlHttpQueryGet(rri->requestBufp, rri->requestUrl, &query_len);
    if (query == NULL)
    {
        return TSREMAP_NO_REMAP;
    }

    if (query_len > 1024)
    {
        if (TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, "", 0) == TS_ERROR)
        {
            TSError("[ts_flv] Set TSUrlHttpQuery Error...\n");
        }

        return TSREMAP_NO_REMAP;
    }

    pointer_query = (char *)TSmalloc(query_len + 1);
    if (pointer_query != NULL)
    {
        memcpy(pointer_query, query, query_len);
        pointer_query[query_len] = 0;
    }
    else
    {
        TSError("TSmalloc apply failed..");
    }

    int txn_slot = -1;
    if (TSHttpArgIndexNameLookup("ts_flv", &txn_slot, NULL) == TS_ERROR)
    {
        TSError("TSHttpArgIndexNameLookup ts_flv error..");
        return TSREMAP_NO_REMAP;
    }
    TSHttpTxnArgSet(rh, txn_slot, (void *)pointer_query);

    //val = ts_arg(query, query_len, "start", sizeof("start")-1, &val_len);
    val = ts_arg(query, query_len, flv_config->start_arg,
            strlen(flv_config->start_arg), &val_len);
    start_val = val;
    if (val != NULL) {
        ret = sscanf(val, "%lf", &start);
        if (ret != 1)
        {
            start = 0;
        }

        if (start < 0) {
            start = 0;
        }

        // reset args
        //left = val - sizeof("start") - query;
        left = val - strlen(flv_config->start_arg) - 1 - query;
        right = query + query_len - val - val_len;

        if (left > 0) {
            left--;
        }

        if (left == 0 && right > 0) {
            right--;
        }

        buf_len = sprintf(buf, "%.*s%.*s", left, query, right, query+query_len-right);
    }
    else
    {
        memcpy(buf, query, query_len);
        buf_len = query_len;
    }

    // get end      @change by dnion
    val = ts_arg(buf, buf_len, flv_config->end_arg,
            strlen(flv_config->end_arg), &val_len);
    if (val != NULL) {
        ret = sscanf(val, "%lf", &end);
        if (ret != 1)
        {
            end = 0;
        }

        if (end <= start) {
            return TSREMAP_NO_REMAP;
        }

        left = val - strlen(flv_config->end_arg) - 1 - buf;
        right = buf + buf_len - val - val_len;

        if (left > 0) {
            left--;
        }

        if (left == 0 && right > 0) {
            right--;
        }

        buf_len_end = sprintf(buf_end, "%.*s%.*s", left, buf, right, buf+buf_len-right);
        //TSError("end_val, buf_end: %s, buf_end_len:%d", buf_end, buf_len_end);
        TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, buf_end, buf_len_end);
    } 
    else if (start_val != NULL)
    {
        // Zerkkro: start only
        //TSError("start only, buf: %s, buf_len:%d", buf, buf_len);
        TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, buf, buf_len);
    }
    else
    {
        //if (TSUrlHttpQuerySet(rri->requestBufp, rri->requestUrl, "", 0) == TS_ERROR)
        //{
        //    TSError("[ts_flv] Set TSUrlHttpQuery Error..");
        //}

        return TSREMAP_NO_REMAP;
    }

    // Get url      @Zerkkro
    // change by dnion
    url = TSUrlStringGet(rri->requestBufp, rri->requestUrl, &url_len);
    if (!url || url_len > 1024)
    {
        TSError("TSUrlStringGet, get url failed, url:%s, len:%d", url, url_len);
        return TSREMAP_NO_REMAP;
    }

    // remove Accept-Encoding
    REMOVE_HEADER(rri->requestBufp, rri->requestHdrp,
            TS_MIME_FIELD_ACCEPT_ENCODING, TS_MIME_LEN_ACCEPT_ENCODING);

    // remove Range
    REMOVE_HEADER(rri->requestBufp, rri->requestHdrp,
                  TS_MIME_FIELD_RANGE, TS_MIME_LEN_RANGE);

    fc = new FlvContext(start, end);
    fc->url = url;
    fc->ih = ih;
    contp = TSContCreate(flv_handler, NULL);
    TSContDataSet(contp, fc);

    TSHttpTxnHookAdd(rh, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
    TSHttpTxnHookAdd(rh, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    // ts_flv add hooks for reset query args for access.log
    TSHttpTxnHookAdd(rh, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
    TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, contp);

    return TSREMAP_NO_REMAP;
}

static int
flv_handler(TSCont contp, TSEvent event, void *edata)
{
    TSHttpTxn       txnp;
    FlvContext      *fc;

    txnp = (TSHttpTxn)edata;
    fc = (FlvContext*)TSContDataGet(contp);

    switch (event) {

        case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
            flv_cache_lookup_complete(fc, txnp);
            break;

        case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
            flv_reset_request_url(txnp);
            break;

        case TS_EVENT_HTTP_READ_RESPONSE_HDR:
            flv_read_response(fc, txnp);
            break;

        case TS_EVENT_HTTP_TXN_CLOSE:
            delete fc;
            TSContDestroy(contp);
            break;

        default:
            break;
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
}

static void
flv_cache_lookup_complete(FlvContext *fc, TSHttpTxn txnp)
{
    TSMBuffer       bufp;
    TSMLoc          hdrp;
    TSMLoc          cl_field;
    TSHttpStatus    code;
    int             obj_status;
    int64_t         n;

    if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
        TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
        return;
    }

    if (obj_status != TS_CACHE_LOOKUP_HIT_STALE && obj_status != TS_CACHE_LOOKUP_HIT_FRESH)
        return;

    if (TSHttpTxnCachedRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
        TSError("[%s] Couldn't get cache resp", __FUNCTION__);
        return;
    }

    code = TSHttpHdrStatusGet(bufp, hdrp);
    if (code != TS_HTTP_STATUS_OK) {
        goto release;
    }

    n = 0;

    cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
    if (cl_field) {
        n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
        TSHandleMLocRelease(bufp, hdrp, cl_field);
    }

    if (n <= 0)
        goto release;

    fc->cl = n;
    flv_add_transform(fc, txnp);

release:

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
flv_reset_request_url(TSHttpTxn txnp)
{
    int txn_slot = -1  ;
    char *getargs = NULL;
    TSHttpArgIndexNameLookup("ts_flv", &txn_slot, NULL);

    TSMBuffer reqp;    
    TSMLoc hdr_loc = NULL, url_loc = NULL, field_loc = NULL;

    do
    {
        if(TSHttpTxnClientReqGet((TSHttpTxn) txnp, &reqp, &hdr_loc) != TS_SUCCESS) {
            TSError( "[ts_flv] could not get request data");
            break;
        }

        if(TSHttpHdrUrlGet(reqp, hdr_loc, &url_loc) != TS_SUCCESS) {
            TSError("[ts_flv] couldn't retrieve request url");
            break;
        }

        getargs  = (char *)TSHttpTxnArgGet(txnp,txn_slot);
        if(getargs == NULL) {
            TSError("[ts_flv] getargs NULL");
            break;
        }
        if(TSUrlHttpQuerySet(reqp, url_loc, (const char *)getargs, strlen(getargs)) == TS_ERROR) {
            TSError("[ts_flv]  Set TSUrlHttpQuery Error ! \n");  
            break;
        }
    }while (0); 

    TSHttpTxnArgSet(txnp,txn_slot,NULL);

    if(getargs != NULL)
    {
        TSfree((void *)getargs);
        getargs = NULL;
    }

    TSHandleMLocRelease(reqp, hdr_loc, field_loc);

    if(url_loc)
    {
        TSHandleMLocRelease(reqp, hdr_loc, url_loc);
    }
    if(hdr_loc)
    {
        TSHandleMLocRelease(reqp, TS_NULL_MLOC, hdr_loc);     
    }
}

static void
flv_read_response(FlvContext *fc, TSHttpTxn txnp)
{
    TSMBuffer       bufp;
    TSMLoc          hdrp;
    TSMLoc          cl_field;
    TSHttpStatus    status;
    int64_t         n;

    if (TSHttpTxnServerRespGet(txnp, &bufp, &hdrp) != TS_SUCCESS) {
        TSError("[%s] could not get request os data", __FUNCTION__);
        return;
    }

    status = TSHttpHdrStatusGet(bufp, hdrp);
    if (status != TS_HTTP_STATUS_OK)
        goto release;

    n = 0;
    cl_field = TSMimeHdrFieldFind(bufp, hdrp, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
    if (cl_field) {
        n = TSMimeHdrFieldValueInt64Get(bufp, hdrp, cl_field, -1);
        TSHandleMLocRelease(bufp, hdrp, cl_field);
    }

    if (n <= 0)
        goto release;

    fc->cl = n;
    flv_add_transform(fc, txnp);

release:

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdrp);
}

static void
flv_add_transform(FlvContext *fc, TSHttpTxn txnp)
{
    TSVConn             connp;
    FlvTransformContext *ftc;

    if (fc->transform_added)
        return;

    ftc = new FlvTransformContext(fc->start, fc->end, fc->cl, fc->url, fc->ih);

    TSHttpTxnUntransformedRespCache(txnp, 1);
    TSHttpTxnTransformedRespCache(txnp, 0);

    connp = TSTransformCreate(flv_transform_entry, txnp);
    TSContDataSet(connp, fc);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);

    fc->transform_added = true;
    fc->ftc = ftc;
}

static int
flv_transform_entry(TSCont contp, TSEvent event, void *edata)
{
    TSVIO        input_vio;
    FlvContext   *fc = (FlvContext*)TSContDataGet(contp);

    if (TSVConnClosedGet(contp)) {
        TSContDestroy(contp);
        return 0;
    }

    switch (event) {

        case TS_EVENT_ERROR:
            input_vio = TSVConnWriteVIOGet(contp);
            TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
            break;

        case TS_EVENT_VCONN_WRITE_COMPLETE:
            TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
            break;

        case TS_EVENT_VCONN_WRITE_READY:
        default:
            flv_transform_handler(contp, fc);
            break;
    }

    return 0;
}

static int
flv_transform_handler(TSCont contp, FlvContext *fc)
{
    TSVConn             output_conn;
    TSVIO               input_vio;
    TSIOBufferReader    input_reader;
    int64_t             avail, toread, upstream_done, tag_avail, to_send;
    int                 ret;
    bool                write_down;
    FlvTag              *ftag;
    FlvTransformContext *ftc;

    ftc = fc->ftc;
    ftag = &(ftc->ftag);

    output_conn = TSTransformOutputVConnGet(contp);
    input_vio = TSVConnWriteVIOGet(contp);
    input_reader = TSVIOReaderGet(input_vio);

    if (!TSVIOBufferGet(input_vio)) {
        if (ftc->output.vio) {
            TSVIONBytesSet(ftc->output.vio, ftc->total);
            TSVIOReenable(ftc->output.vio);
        }
        return 1;
    }

    avail = TSIOBufferReaderAvail(input_reader);
    upstream_done = TSVIONDoneGet(input_vio);

    TSIOBufferCopy(ftc->res_buffer, input_reader, avail, 0);
    TSIOBufferReaderConsume(input_reader, avail);
    TSVIONDoneSet(input_vio, upstream_done + avail);

    toread = TSVIONTodoGet(input_vio);
    write_down = false;

    if (!ftc->parse_over)
    {
        ret = ftag->process_tag(ftc->res_reader, toread <= 0);
        if (ret == 0)
            goto trans;

        ftc->parse_over = true;
        ftc->output.buffer = TSIOBufferCreate();
        ftc->output.reader = TSIOBufferReaderAlloc(ftc->output.buffer);
        ftc->output.vio = TSVConnWrite(output_conn, contp, ftc->output.reader, ftag->content_length);

        tag_avail = ftag->write_out(ftc->output.buffer);
        if (tag_avail > 0)
        {
            ftc->total += tag_avail;
            write_down = true;
        }
    }
    avail = TSIOBufferReaderAvail(ftc->res_reader);
    if (avail > 0)
    {
        // 根据解析到的end_pos判断数据是否发送
        // change by dnion
        to_send = ftag->end_pos - ftag->total_offset;
        if (to_send > 0)
        {
            if (to_send > avail)
                to_send = avail;

            TSIOBufferCopy(ftc->output.buffer, ftc->res_reader, to_send, 0);
            ftc->total += to_send;
            ftag->total_offset += to_send;
            write_down = true;
        }
        TSIOBufferReaderConsume(ftc->res_reader, avail);
    }

trans:

    if (write_down)
        TSVIOReenable(ftc->output.vio);

    if (toread > 0) {
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);

    } else {
        TSVIONBytesSet(ftc->output.vio, ftc->total);
        TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
    }

    return 1;
}


char *
ts_arg(const char *param, size_t param_len, const char *key, size_t key_len, size_t *val_len)
{
    const char  *p, *last;
    const char  *val;

    *val_len = 0;

    if (!param || !param_len)
        return NULL;

    p = param;
    last = p + param_len;

    for ( ; p < last; p++) {

        p = (char*)memmem(p, last-p, key, key_len);

        if (p == NULL)
            return NULL;

        if ((p == param || *(p - 1) == '&') && *(p + key_len) == '=') {

            val = p + key_len + 1;

            p = (char*)memchr(p, '&', last-p);

            if (p == NULL)
                p = param + param_len;

            *val_len = p - val;

            return (char*)val;
        }
    }

    return NULL;
}

