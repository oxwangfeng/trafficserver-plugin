#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ts/ts.h"
#include "ts/remap.h"

#include "ts/ink_defs.h"
#include "ts/ink_memory.h"

#define PLUGIN_NAME "cache_status"

static char *header_name = TSstrdup("X-Ats-Cache");

static int status_handler(TSCont contp, TSEvent event, void *edata);

static int ts_cache_status_send_response(TSCont contp, TSHttpTxn txnp)
{
    TSMBuffer bufp;
    TSMLoc hdr_loc;
    TSMLoc field_loc;

    char *buf = (char*)TSContDataGet(contp);

    if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
        TSError("[%s] Unable to retrieve client request header", PLUGIN_NAME);
        return 0;
    }

    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header_name, strlen(header_name));
    if (field_loc) {
         // if we find this header, we just return to client.
         // TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, buf, buf_len);
	if(buf != NULL)
            TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, buf, strlen(buf));
    } else {
        if (TSMimeHdrFieldCreate(bufp, hdr_loc, &field_loc) != TS_SUCCESS) {
            TSError("[%s] can't create header", PLUGIN_NAME);
            TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
            return 0;
        }
        
	if (TSMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, header_name, strlen(header_name)) != TS_SUCCESS) {
            TSError("[%s] TSMimeHdrFieldNameSet failed", PLUGIN_NAME);
            goto done;
   	}
	if(buf == NULL ){
            if (TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, -1, "Hit Stale", sizeof("Hit Stale") - 1) != TS_SUCCESS) {
                TSError("[%s] TSMimeHdrFieldValueStringInsert failed", PLUGIN_NAME);
                goto done;
            }
	} else {
            if (TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, -1, buf, strlen(buf)) != TS_SUCCESS) {
                TSError("[%s] TSMimeHdrFieldValueStringInsert failed", PLUGIN_NAME);
                goto done;
            }
	}

        if (TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc) != TS_SUCCESS) {
            TSError("[%s] TSMimeHdrFieldAppend failed", PLUGIN_NAME);
            goto done;
        }
     }

done:
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    return 0;
}

static int ts_cache_status_read_response(TSCont contp, TSHttpTxn txnp)
{
    TSMBuffer bufp = (TSMBuffer)NULL;
    TSMLoc hdr_loc = TS_NULL_MLOC;
    TSMLoc field_loc;
    TSHttpStatus resp_status;
    const char *val;
    int val_len;

    char *cache_status = NULL;

    if (TS_SUCCESS != TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc)) {
      TSDebug(PLUGIN_NAME, "unable to get server response");
      return TS_ERROR;
    }

    resp_status = TSHttpHdrStatusGet(bufp, hdr_loc);
    //对于slice而言，由于已经将206修改成200，所以这儿看到的是200
    if (resp_status == 200) {
       cache_status = TSstrdup("Refresh Miss");
    } else if (resp_status == 304) {
       cache_status = TSstrdup("Refresh Hit");
    } else if (resp_status == 206) {
      //--对于没有修改206状态码的请求，不缓存的，直接proxy
      cache_status = TSstrdup("Miss");
    } else {
	field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL);
  	if (field_loc != TS_NULL_MLOC) {
	   val = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &val_len);
	   if(val){
      		cache_status = TSstrdup("Refresh Miss");
	   }
	   TSHandleMLocRelease(bufp, hdr_loc, field_loc);
	}
	if(cache_status == NULL){
      	   cache_status = TSstrdup("Miss");
	}
    } 
    TSContDataSet(contp, cache_status);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return 0;
}

static void ts_cache_status_cache_lookup_complete(TSCont contp, TSHttpTxn txnp)
{
    int obj_status = TS_ERROR;
    char *cache_status = NULL;

    if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
       return;
    }

    TSCont ncontp = TSContCreate(status_handler, NULL);
    switch (obj_status) {
       case TS_CACHE_LOOKUP_MISS:
	   cache_status = TSstrdup("Miss");
	   break;
       case TS_CACHE_LOOKUP_HIT_STALE:
    	   TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, ncontp);
	   break;
       case TS_CACHE_LOOKUP_HIT_FRESH:
	   cache_status = TSstrdup("Hit");
	   break;
       case TS_CACHE_LOOKUP_SKIPPED:
	   cache_status = TSstrdup("Miss");
           break;
      default:
	   break;
    }
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, ncontp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, ncontp);
    TSContDataSet(ncontp, cache_status);
}

static int status_handler(TSCont contp, TSEvent event, void *edata)
{
    TSHttpTxn txnp = (TSHttpTxn)edata;
    char *data     = (char *)TSContDataGet(contp);
    switch (event) {
        case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
            ts_cache_status_cache_lookup_complete(contp, txnp);
            break;
	case TS_EVENT_HTTP_READ_RESPONSE_HDR:
      	    ts_cache_status_read_response(contp, txnp);
	    break;
        case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
            ts_cache_status_send_response(contp, txnp);
            break;

        case TS_EVENT_HTTP_TXN_CLOSE:
	    if(data){
            	TSfree(data);
	    }
            TSContDestroy(contp);
            break;
        default:
            break;
    }

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"cache_status";
  info.vendor_name   = (char *)"";
  info.support_email = (char *)"";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed.", PLUGIN_NAME);
  }

  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, TSContCreate(status_handler, NULL));
}

