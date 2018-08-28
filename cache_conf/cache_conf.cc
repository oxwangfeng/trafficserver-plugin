#include "read_conf.h"
#include "header_modify.h"

#define DOMAIN_DONE 0
#define DOMAIN_FIND 1

#define RELEASE_TIME 24 * 60 * 60 * 1000

link_info *global_config;
static char *file_name;

typedef volatile void *vvoidp;

static inline void *
ink_atomic_swap_ptr(vvoidp mem, void *value)
{
  return __sync_lock_test_and_set((void **)mem, value);
}

static int hook_handler(TSCont contp, TSEvent event, void *edata);

void
response_handler(TSHttpTxn txnp, TSCont contp)
{
  line_info *line = (line_info *)TSContDataGet(contp);

  TSMBuffer bufp;
  TSMLoc hdr_loc;

  if (TSHttpTxnServerRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve client request header");
    TSError("[output_header] Couldn't retrieve client request header");
    return;
  }

  TSHttpStatus status = TSHttpHdrStatusGet(bufp, hdr_loc);
  if (status != TS_HTTP_STATUS_OK && status != TS_HTTP_STATUS_PARTIAL_CONTENT && status != TS_HTTP_STATUS_NOT_MODIFIED) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return;
  }

  bool force = line->revalidate < 0 ? false : true;

  if (line->revalidate) {
    set_expires(bufp, hdr_loc, line->revalidate, force);
  }

  if (line->ttl_in_cache) {
    set_expires(bufp, hdr_loc, line->revalidate, force);
  }

  if (line->action) {
    set_expires(bufp, hdr_loc, 0, true);
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return;
}

int
domain_check(TSHttpTxn txnp, TSCont contp, TSEvent event)
{
  line_info *find;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  TSMLoc host_loc;

  int url_len, host_len, scheme_len;
  const char *host = NULL;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSDebug(PLUGIN_NAME, "couldn't retrieve client request header");
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    return DOMAIN_DONE;
  }

  const char *met = TSHttpHdrMethodGet(bufp, hdr_loc, &scheme_len);
  if (!met || (0 != strncmp(met, TS_HTTP_METHOD_GET, scheme_len)) || (0 == strncmp(met, TS_HTTP_METHOD_POST, scheme_len))) {
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return DOMAIN_DONE;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve request url", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    return DOMAIN_DONE;
  }

  host_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_HOST, TS_MIME_LEN_HOST);
  if (host_loc == NULL) {
    TSDebug(PLUGIN_NAME, "request without host header");
  } else {
    host = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, host_loc, -1, &host_len);
  }

  char *url = TSUrlStringGet(bufp, url_loc, &url_len);
  if ((find = checker(global_config, url, url_len, host, host_len)) != NULL) {
    TSCont ncontp = TSContCreate(hook_handler, NULL);
    TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, ncontp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, ncontp);
    TSContDataSet(ncontp, find);
    TSDebug(PLUGIN_NAME, "find domain");
  }

  TSfree(url);
  if (host_loc != NULL) {
    TSHandleMLocRelease(bufp, hdr_loc, host_loc);
  }
  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return DOMAIN_DONE;
}

static int
management_update(TSCont contp, TSEvent event, void *edata)
{
  line_info *p, *q;
  link_info *tmp = NULL, *link_old = NULL;
  TSCont ncontp = NULL;

  switch ((int)event) {
  case TS_EVENT_MGMT_UPDATE:
    tmp = (link_info *)malloc(sizeof(link_info));
    if (!load_config_file(file_name, tmp)) {
      TSError("[%s] file_name reload error", PLUGIN_NAME);
      return 0;
    }
    link_old = (link_info *)ink_atomic_swap_ptr(&global_config, tmp);
    ncontp   = TSContCreate(management_update, TSMutexCreate());
    TSContDataSet(ncontp, link_old);
    TSContSchedule(ncontp, RELEASE_TIME, TS_THREAD_POOL_DEFAULT);
    break;

  case TS_EVENT_TIMEOUT:
    tmp = (link_info *)TSContDataGet(contp);
    if (tmp == NULL) {
      return 0;
    }

    p = tmp->head;
    for (q = p->next; q; p = q, q = q->next) {
      free(p);
    }
    free(p);
    free(tmp);
    TSContDestroy(contp);
    break;
  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

static int
hook_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    domain_check(txnp, contp, event);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    response_handler(txnp, contp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    TSContDestroy(contp);
    TSDebug(PLUGIN_NAME, "Destroy!");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  default:
    TSAssert(!"Unexpected event");
  }

  return 0;
}

void
TSRemapDeleteInstance(void *ih)
{
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  return TSREMAP_NO_REMAP;
}

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf ATS_UNUSED, int errbuf_size ATS_UNUSED)
{
  return TS_SUCCESS;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)"cache_conf";
  info.vendor_name   = (char *)"MyCompany";
  info.support_email = (char *)"ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[cache_conf] Plugin registration failed.");
  }

  if (argc < 2) {
    TSError("[cache_conf] args error");
  }

  TSCont management_contp = TSContCreate(management_update, NULL);
  TSMgmtUpdateRegister(management_contp, PLUGIN_NAME);

  file_name     = TSstrdup(argv[1]);
  global_config = (link_info *)malloc(sizeof(link_info));
  memset(global_config, 0, sizeof(link_info));
  TSAssert(load_config_file(argv[1], global_config));
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(hook_handler, NULL));
}
