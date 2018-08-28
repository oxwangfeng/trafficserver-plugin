#ifndef classB_H_
#define classB_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ts/ts.h"
#include "ts/remap.h"

#include "ts/ink_defs.h"
#include "ts/ink_memory.h"

#define PLUGIN_NAME "cache_conf"

// token info
struct matcher_tags {
  const char *match_host;
  const char *match_domain;
  const char *match_ip;
  const char *match_regex;
  const char *match_url;
  const char *match_host_regex;
  bool dest_error_msg; // whether to use src or destination in any error messages

  bool
  empty() const
  {
    return this->match_host == NULL && this->match_domain == NULL && this->match_ip == NULL && this->match_regex == NULL &&
           this->match_url == NULL && this->match_host_regex == NULL;
  }
};

const matcher_tags tags = {"dest_host", "dest_domain", "dest_ip", "url_regex", "url", "host_regex", true};

// line info
const int MATCHER_MAX_TOKENS = 40;

struct line_info {
  char *dest_domain;
  char *action;
  char *suffix;
  char *prefix;
  bool http_method;
  int ttl_in_cache;
  int revalidate;
  int line_num;
  line_info *next;
};

// link info
typedef struct {
  line_info *head;
  int num;
} link_info;

bool load_config_file(const char *config_file, link_info *global_config);
line_info *checker(link_info *config, char *value, int len, const char *host, int host_len);

static inline bool
is_digit(const char c)
{
  return (c >= '0' && c <= '9') ? true : false;
}

static inline bool
is_char(const char ch)
{
  return ((ch & 0x80) == 0);
}

#endif
