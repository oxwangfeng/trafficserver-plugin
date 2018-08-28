#include "wildcard_purge.h"

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    return TS_ERROR;
  }

  if (api_info->size < sizeof(TSRemapInterface)) {
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  int ret;
  *ih = NULL;

  if (argc != 4) {
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] less argument, should be: data_path task_ttl");
    return TS_ERROR;
  }

  ret = wild_purge_init(argv[2], atoi(argv[3]));
  if (ret) {
    snprintf(errbuf, errbuf_size, "[TSRemapNewInstance] purge_data_mananger_init(%s, %s, %s) = %d, failed", argv[2], argv[3],
             argv[4], ret);
    return TS_ERROR;
  }

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  const char *host_value;
  const char *method, *tmp;
  char *path_value;
  int method_len;
  int path_len;
  int host_len;

  method = TSHttpHdrMethodGet(rri->requestBufp, rri->requestHdrp, &method_len);
  if (method_len != 5 || strncmp(method, "PURGE", 5)) {
    return TSREMAP_NO_REMAP;
  }

  TSMLoc field_loc;
  field_loc = TSMimeHdrFieldFind(rri->requestBufp, rri->requestHdrp, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);
  if (!field_loc) {
    return TSREMAP_NO_REMAP;
  }

  host_value = TSMimeHdrFieldValueStringGet(rri->requestBufp, rri->requestHdrp, field_loc, -1, &host_len);
  if (host_value == NULL || host_len == 0 || *host_value != '.') {
    goto done;
  }

  tmp = TSUrlPathGet(rri->requestBufp, rri->requestUrl, &path_len);
  path_value = (char *)TSmalloc(path_len + 2);
  memcpy(path_value + 1, tmp, path_len);
  path_value[0] = '/';
  path_value[++path_len] = 0;
  build_tree(host_value, host_len, path_value, path_len);

  TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, field_loc);
  TSfree(path_value);
  TSHttpTxnSetHttpRetStatus(rh, TS_HTTP_STATUS_OK);
  return TSREMAP_NO_REMAP_STOP;

done:
  TSHandleMLocRelease(rri->requestBufp, rri->requestHdrp, field_loc);
  return TSREMAP_NO_REMAP;
}
