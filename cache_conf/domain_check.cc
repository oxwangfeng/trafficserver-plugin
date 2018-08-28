#include "read_conf.h"

static bool
prefix_check(char *des, char *url_src, int url_len)
{
  char *p, *url, *start, *end;

  if (des == NULL) {
    return true;
  }

  url = (char *)malloc(url_len + 1);
  memset(url, 0, url_len + 1);
  memcpy(url, url_src, url_len);

  p = url;
  if (*p == 'h' && strncmp(p, "http://", 7) == 0) {
    p += 7;
  }

  start = strchr(p, '/');
  end   = strchr(p, '?');
  if (end == NULL) {
    end = strrchr(url, '/');
  }

  if (start == NULL || end == NULL || end <= start) {
    free(url);
    return false;
  }

  if (strncmp(start, des, strlen(des)) == 0) {
    free(url);
    return true;
  }

  free(url);
  return false;
}

static bool
suffix_check(char *des, char *url_src, int url_len)
{
  char *p, *url, *start;

  if (des == NULL) {
    return true;
  }

  url = (char *)malloc(url_len + 1);
  memset(url, 0, url_len + 1);
  memcpy(url, url_src, url_len);

  start = p = url;
  if (*p == 'h' && strncmp(p, "http://", 7) == 0) {
    p += 7;
  }

  p = strrchr(url, '/');
  if (p == NULL || p <= start) {
    free(url);
    return false;
  }

  p = strchr(url, '?');
  if (p == NULL) {
    p = url + url_len;
  }

  p -= strlen(des);
  if (p <= start || strncmp(p, des, strlen(des)) != 0) {
    free(url);
    return false;
  }
  free(url);
  return true;
}

static bool
host_check(char *des, const char *host_src, int host_len)
{
  char *p, *host;

  if (des == NULL) {
    return false;
  }

  host = (char *)malloc(host_len + 1);
  memset(host, 0, host_len + 1);
  memcpy(host, host_src, host_len);

  p = host;
  if (*p == 'h' && strncmp(p, "http://", 7) == 0) {
    p += 7;
  }

  if (*des == '.') {
    p = host + host_len - strlen(des);
    if (p <= host) {
      free(host);
      return false;
    }

    if (strncmp(p, des, strlen(des)) == 0) {
      free(host);
      return true;
    }
  } else {
    if (strncmp(p, des, strlen(des)) == 0) {
      free(host);
      return true;
    }
  }

  free(host);
  return false;
}

line_info *
checker(link_info *config, char *url_src, int url_len, const char *host_src, int host_len)
{
  if (host_src != NULL) {
    for (line_info *line = config->head; line; line = line->next) {
      if (line->dest_domain && host_check(line->dest_domain, host_src, host_len) && suffix_check(line->suffix, url_src, url_len) &&
          (prefix_check(line->prefix, url_src, url_len))) {
        return line;
      }
    }
  }
  return NULL;
}
