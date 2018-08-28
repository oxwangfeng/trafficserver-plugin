#include "wildcard_purge.h"

static CharTrie<int64_t> lower_tree(true);
static CharTrie<MapEntry> tree(true);

static long int _max_ttl = 10000;
static char filename[256];
static FILE *_binlog_fp = NULL;

static void
add_host(const char *host_value, int host_len, time_t timestamp)
{
  int64_t *old = lower_tree.fullLookup(host_value, host_len);
  if (!old)
    lower_tree.insert(host_value, host_len, (int64_t *)MAKE_NODE(timestamp, _max_ttl));
  else {
    int64_t ov = (int64_t)old;
    if (GET_TIMESTAMP(ov) < timestamp) {
      lower_tree.insert(host_value, host_len, (int64_t *)MAKE_NODE(timestamp, _max_ttl));
    }
  }
}

static void
add_path(char *host_value, int host_len, const char *path_value, int path_len, time_t timestamp)
{
  MapEntry *old = tree.fullLookup(host_value, host_len);
  if (!old) {
    MapEntry *newEntry = new MapEntry(MAKE_NODE(timestamp, _max_ttl));
    newEntry->paths->insert(path_value, path_len, (int64_t *)MAKE_NODE(timestamp, _max_ttl));
    tree.insert(host_value, host_len, newEntry);
  } else {
    if (GET_TIMESTAMP(old->timestamp) < timestamp) {
      old->timestamp = MAKE_NODE(timestamp, _max_ttl);
      old->paths->insert(path_value, path_len, (int64_t *)MAKE_NODE(timestamp, _max_ttl));
      tree.insert(host_value, host_len, old);
    }
  }
}

static int
build_log(const char *host_value, int host_len, const char *path_value, int path_len, time_t tt)
{
  int result = 0;
  char host[1024];
  char path[1024];

  memset(host, 0, 1024);
  memset(path, 0, 1024);
  snprintf(host, host_len > 1020 ? 1021 : host_len + 1, "%s", host_value);
  snprintf(path, path_len > 1020 ? 1021 : path_len + 1, "%s", path_value);

  if (fprintf(_binlog_fp, "%d %d %d %s %s\n", (int)time(NULL), (int)tt, (int)_max_ttl, host, path_len ? path : "0") < 0) {
    result = errno != 0 ? errno : EIO;
    fprintf(stderr, "file: " __FILE__ ", line: %d, "
                    "write to file %s, errno: %d, error info: %s\n",
            __LINE__, filename, result, strerror(result));
  }

  fflush(_binlog_fp);
  return result;
}

void
build_tree(const char *host_value, int host_len, const char *path_value, int path_len)
{
  time_t tt  = time(NULL);
  char *host = strrev(host_value, host_len);
  // if (path_value == NULL || path_len == 0)
  //  add_host(host, host_len, tt);
  // else
  add_path(host, host_len, path_value, path_len, tt);

  build_log(host, host_len, path_value, path_len, tt);
  free(host);
}

int
wild_purge_init(char *name, int ttl)
{
  int len, cache_timestamp, log_timestamp, task_ttl;
  char host[1024];
  char path[1024];
  char line[4096];
  time_t ti = time(NULL);

  memset(host, 0, 1024);
  memset(path, 0, 1024);

  _max_ttl = ttl;

  len = snprintf(filename, 255, "%s", name);
  if (len > 0 && filename[len - 1] == '/') {
    filename[len - 1] = '\0';
  }

  _binlog_fp = fopen(name, "a+");
  if (_binlog_fp == NULL) {
    fprintf(stderr, "file: " __FILE__ ", line: %d, "
                    "open file %s fail, errno: %d, error info: %s\n",
            __LINE__, filename, errno, strerror(errno));
    return errno != 0 ? errno : EPERM;
  }

  //  fprintf(_binlog_fp, "aaa");
  //  fflush(_binlog_fp);

  FILE *fp;
  fp = fopen(name, "at+");
  if (fp == NULL) {
    fprintf(stderr, "file: " __FILE__ ", line: %d, "
                    "open file %s fail, errno: %d, error info: %s\n",
            __LINE__, filename, errno, strerror(errno));
    return errno != 0 ? errno : EPERM;
  }

  while (fgets(line, sizeof(line), fp) != NULL) {
    if (sscanf(line, "%d %d %d %s %s", &log_timestamp, &cache_timestamp, &task_ttl, host, path) != 5) {
      fprintf(stderr, "file: " __FILE__ ", line: %d, "
                      "file %s, invalid line: %s\n",
              __LINE__, filename, line);
      continue;
    }

    if (cache_timestamp + task_ttl < ti) {
      continue;
    }

    if (*path == '0') {
      add_host(host, strlen(host), cache_timestamp);
    } else {
      add_path(host, strlen(host), path, strlen(path), cache_timestamp);
    }
  }
  fclose(fp);
  return 0;
}

int
find_next(LookupCursor *cursor, int *timestamp)
{
  while (!cursor->trie_done) {
    int64_t *value = cursor->lower_tree->lookupNext(&cursor->lower_state);
    if (value == NULL) {
      cursor->trie_done = true;
      break;
    } else {
      cursor->trie_done = false;
      *timestamp        = GET_TIMESTAMP((int64_t)value);
      if (*timestamp + GET_TTL((int64_t)value) > TShrtime() / TS_HRTIME_SECOND) {
        return 0;
      }
    }
  }
  return 1;
}

int
find_first(const char *host, const int host_len, LookupCursor *cursor, int *timestamp, bool init)
{
  if (init) {
    cursor->str        = host;
    cursor->len        = host_len;
    cursor->lower_tree = &lower_tree;
  }

  memset(&cursor->lower_state, 0, sizeof(CharTrie<int64_t>::LookupState));
  int64_t *value = cursor->lower_tree->lookupFirst(cursor->str, cursor->len, &cursor->lower_state);
  if (value == NULL) {
    cursor->trie_done = true;
    return 1;
  } else {
    cursor->trie_done = false;
    *timestamp        = GET_TIMESTAMP((int64_t)value);

    if (*timestamp + GET_TTL((int64_t)value) > TShrtime() / TS_HRTIME_SECOND) {
      return 0;
    } else {
      return find_next(cursor, timestamp);
    }
  }
}

int
find_path_next(LookupCursor *cursor, int *timestamp)
{
  if (find_next(cursor->child, timestamp) == 0) {
    return 0;
  }

  while (!cursor->trie_done) {
    MapEntry *value = cursor->tree->lookupNext(&cursor->tree_state);
    if (value == NULL) {
      cursor->trie_done = true;
      break;
    } else {
      cursor->trie_done         = false;
      *timestamp                = GET_TIMESTAMP((int64_t)value->timestamp);
      cursor->child->lower_tree = value->paths;
      if (*timestamp + GET_TTL((int64_t)value->timestamp) > TShrtime() / TS_HRTIME_SECOND) {
        find_first(cursor->child->str, cursor->child->len, cursor->child, timestamp, false);
        return 0;
      }
    }
  }
  return 1;
}

int
find_path_first(const char *host, const int host_len, const char *path, const int path_len, LookupCursor *cursor, int *timestamp)
{
  cursor->str        = host;
  cursor->len        = host_len;
  cursor->tree       = &tree;
  cursor->child->str = path;
  cursor->child->len = path_len;

  memset(&cursor->lower_state, 0, sizeof(CharTrie<MapEntry>::LookupState));
  MapEntry *value = cursor->tree->lookupFirst(cursor->str, cursor->len, &cursor->tree_state);
  if (value == NULL) {
    cursor->trie_done = true;
    return 1;
  } else {
    cursor->trie_done = false;
    *timestamp        = GET_TIMESTAMP((int64_t)value->timestamp);
    if (*timestamp + GET_TTL((int64_t)value->timestamp) > TShrtime() / TS_HRTIME_SECOND) {
      cursor->child->lower_tree = value->paths;
      if (find_first(path, path_len, cursor->child, timestamp, false) == 0)
        return 0;
      return find_path_next(cursor, timestamp);
    } else {
      return find_path_next(cursor, timestamp);
    }
  }
}
