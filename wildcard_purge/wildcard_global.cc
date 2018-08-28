#include "wildcard_purge.h"

static int
wild_purge_check_cache(TSCont contp, TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc url_loc;
  int host_len;
  int path_len;
  int obj_status;
  int timestamp;
  time_t cacheTime;
  LookupCursor *cursor = NULL, *child = NULL;

  const char *tmp, *tmp2;
  char *host = NULL;
  char *path = NULL;

  if (TSHttpTxnCacheLookupStatusGet(txnp, &obj_status) == TS_ERROR) {
    TSError("[%s] Couldn't get cache status of object", __FUNCTION__);
    return -1;
  }

  if (obj_status != TS_CACHE_LOOKUP_HIT_FRESH) {
    return 0;
  }

  if (TSHttpTxnCachedRespTimeGet(txnp, &cacheTime) != TS_SUCCESS) {
    TSError("[%s] TSHttpTxnCachedRespTimeGet fail", __FUNCTION__);
    return -1;
  }

  if (TSHttpTxnPristineUrlGet(txnp, &bufp, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Error while retrieving hdr url", __FUNCTION__);
    return -1;
  }

  cursor = (LookupCursor *)TSmalloc(sizeof(LookupCursor));
  child  = (LookupCursor *)TSmalloc(sizeof(LookupCursor));
  memset(cursor, 0, sizeof(LookupCursor));
  memset(child, 0, sizeof(LookupCursor));

  cursor->child = child;
  
  tmp  = TSUrlHostGet(bufp, url_loc, &host_len);
  tmp2 = TSUrlPathGet(bufp, url_loc, &path_len);

  host = strrev(tmp, host_len);
  path = (char *)TSmalloc(path_len + 2);
  memcpy(path + 1, tmp2, path_len);
  path[0]          = '/';
  path[++path_len] = 0;

  if (find_path_first(host, host_len, path, path_len, cursor, &timestamp) != 0)
    goto release;

  do {
    if (cacheTime <= timestamp) {
      TSHttpTxnCacheLookupStatusSet(txnp, TS_CACHE_LOOKUP_HIT_STALE);
      goto release;
    }
  } while (find_path_next(cursor, &timestamp) == 0);

release:
  if (path)
    TSfree(path);
  if (cursor && cursor->child)
    TSfree(cursor->child);
  if (cursor)
    TSfree(cursor);
  if (host)
    TSfree(host);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);
  return 0;
}

static int
wildcard_purge_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    wild_purge_check_cache(contp, txnp);
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
  TSCont contp;

  info.plugin_name   = (char *)"wild_purge_global";
  info.vendor_name   = (char *)"";
  info.support_email = (char *)"";

  if (TSPluginRegister(&info) != TS_SUCCESS)
    return;

  contp = TSContCreate(wildcard_purge_handler, NULL);
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
}
