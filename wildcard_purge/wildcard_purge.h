#ifndef WILD_PURGE_COMMON_H
#define WILD_PURGE_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/experimental.h>

#include "common/CharTrie.h"

#define MAKE_NODE(timestamp, ttl) (((int64_t)timestamp << 32) | (int64_t)ttl << 1)
#define GET_TIMESTAMP(value) (value >> 32)
#define GET_TTL(value) ((value & 0xFFFFFFFF) >> 1)

typedef struct LookupCursor LookupCursor;

struct MapEntry {
  CharTrie<int64_t> *paths;
  int64_t timestamp;
  MapEntry(int64_t ts) : timestamp(ts) { paths = new CharTrie<int64_t>(true); }
};

struct LookupCursor {
  MapEntry *value;
  CharTrie<int64_t> *lower_tree;
  CharTrie<MapEntry> *tree;
  const char *str;
  int len;
  int regex_index;
  bool trie_done;
  CharTrie<int64_t>::LookupState lower_state;
  CharTrie<MapEntry>::LookupState tree_state;
  LookupCursor *child;
};

static inline char *
strrev(const char *dest, int len)
{
  char *tmp, *p;
  tmp = (char *)TSmalloc(len + 1);
  memset(tmp, 0, len + 1);

  const char *d = dest + len - 1;
  p             = tmp;

  while (d >= dest) {
    *p++ = *d--;
  }

  return tmp;
}

void build_tree(const char *host_value, int host_len, const char *path_value, int path_len);
int wild_purge_init(char *name, int ttl);
int find_first(const char *host, const int host_len, LookupCursor *cursor, int *timestamp, bool init = true);
int find_next(LookupCursor *cursor, int *timestamp);
int find_path_first(const char *host, const int host_len, const char *path, const int path_len, LookupCursor *cursor,
                    int *timestamp);
int find_path_next(LookupCursor *cursor, int *timestamp);
#endif
