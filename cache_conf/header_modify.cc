#include <time.h>

#include "read_conf.h"
#include "header_modify.h"

void
set_expires(TSMBuffer bufp, TSMLoc hdr_loc, int tti, bool force)
{
  TSMLoc expires_loc;
  TSMLoc cactl_loc;
  time_t ti;
  char tbuf[1024] = {0};
  int len         = 0;

  // time
  char week[10];
  char mon[10];
  int year;
  int day;
  int hour;
  int min;
  int sec;

  ti = time(NULL);
  ti += tti;

  char *tmp = ctime(&ti);

  sscanf(tmp, "%s %s %d %d:%d:%d %d\n", week, mon, &day, &hour, &min, &sec, &year);
  len = snprintf(tbuf, 1024, "%s, %d %s %d %d:%d:%d GMT", week, day, mon, year, hour, min, sec);

  expires_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_EXPIRES, TS_MIME_LEN_EXPIRES);
  if (expires_loc) {
    if (force) {
      TSMimeHdrFieldValueStringSet(bufp, hdr_loc, expires_loc, -1, tbuf, len);
    }
    TSHandleMLocRelease(bufp, hdr_loc, expires_loc);
  } else {
    if (force) {
      if (TSMimeHdrFieldCreate(bufp, hdr_loc, &expires_loc) != TS_SUCCESS) {
        TSError("[%s] can't create header", PLUGIN_NAME);
        return;
      }
      if (TSMimeHdrFieldNameSet(bufp, hdr_loc, expires_loc, TS_MIME_FIELD_EXPIRES, TS_MIME_LEN_EXPIRES) != TS_SUCCESS) {
        TSHandleMLocRelease(bufp, hdr_loc, expires_loc);
        TSError("[%s] TSMimeHdrFieldNameSet failed", PLUGIN_NAME);
        return;
      }
      if (TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, expires_loc, -1, tbuf, len) != TS_SUCCESS) {
        TSHandleMLocRelease(bufp, hdr_loc, expires_loc);
        TSError("[%s] TSMimeHdrFieldValueStringInsert failed", PLUGIN_NAME);
        return;
      }
      if (TSMimeHdrFieldAppend(bufp, hdr_loc, expires_loc) != TS_SUCCESS) {
        TSHandleMLocRelease(bufp, hdr_loc, expires_loc);
        TSError("[%s] TSMimeHdrFieldAppend failed", PLUGIN_NAME);
        return;
      }
      TSHandleMLocRelease(bufp, hdr_loc, expires_loc);
    }
  }

  memset(tbuf, 0, 1024);
  len       = snprintf(tbuf, 1024, "max-age=%d", tti);
  cactl_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL);
  if (cactl_loc) {
    if (force) {
      TSMimeHdrFieldValueStringSet(bufp, hdr_loc, cactl_loc, -1, tbuf, len);
    }
    TSHandleMLocRelease(bufp, hdr_loc, cactl_loc);
  } else {
    if (force) {
      if (TSMimeHdrFieldCreate(bufp, hdr_loc, &cactl_loc) != TS_SUCCESS) {
        TSError("[%s] can't create header", PLUGIN_NAME);
        return;
      }
      if (TSMimeHdrFieldNameSet(bufp, hdr_loc, cactl_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL) != TS_SUCCESS) {
        TSHandleMLocRelease(bufp, hdr_loc, cactl_loc);
        TSError("[%s] TSMimeHdrFieldNameSet failed", PLUGIN_NAME);
        return;
      }
      if (TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, cactl_loc, -1, tbuf, len) != TS_SUCCESS) {
        TSHandleMLocRelease(bufp, hdr_loc, cactl_loc);
        TSError("[%s] TSMimeHdrFieldValueStringInsert failed", PLUGIN_NAME);
        return;
      }
      if (TSMimeHdrFieldAppend(bufp, hdr_loc, cactl_loc) != TS_SUCCESS) {
        TSHandleMLocRelease(bufp, hdr_loc, cactl_loc);
        TSError("[%s] TSMimeHdrFieldAppend failed", PLUGIN_NAME);
        return;
      }
      TSHandleMLocRelease(bufp, hdr_loc, cactl_loc);
    }
  }
}
