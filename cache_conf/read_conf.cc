#include "read_conf.h"

const char *
parseConfigLine(char *line, line_info *p_line, const matcher_tags *tags)
{
#define MATCH_HOST 1
#define MATCH_NONE 0

  enum pState {
    FIND_LABEL,
    PARSE_LABEL,
    PARSE_VAL,
    START_PARSE_VAL,
    CONSUME,
  };

  pState state      = PARSE_LABEL;
  bool inQuote      = false;
  char *copyForward = NULL;
  char *copyFrom    = NULL;
  char *s           = line;
  char *label       = s;
  char *val         = NULL;
  int num_el        = 0;

  // Zero out the parsed line structure
  memset(p_line, 0, sizeof(line_info));

  if (*s == '\0') {
    return NULL;
  }

  do {
    switch (state) {
    case FIND_LABEL:
      if (!isspace(*s)) {
        state = PARSE_LABEL;
        label = s;
      }
      s++;
      break;
    case PARSE_LABEL:
      if (*s == '=') {
        *s    = '\0';
        state = START_PARSE_VAL;
      }
      s++;
      break;
    case START_PARSE_VAL:
      // Init state needed for parsing values
      copyForward = NULL;
      copyFrom    = NULL;

      if (*s == '"') {
        inQuote = true;
        val     = s + 1;
      } else if (*s == '\\') {
        inQuote = false;
        val     = s + 1;
      } else {
        inQuote = false;
        val     = s;
      }

      if (inQuote == false && (isspace(*s) || *(s + 1) == '\0')) {
        state = CONSUME;
      } else {
        state = PARSE_VAL;
      }

      s++;
      break;
    case PARSE_VAL:
      if (inQuote == true) {
        if (*s == '\\') {
          // The next character is esacped
          //
          // To remove the escaped character
          // we need to copy
          //  the rest of the entry over it
          //  but since we do not know where the
          //  end is right now, defer the work
          //  into the future

          if (copyForward != NULL) {
            // Perform the prior copy forward
            int bytesCopy = s - copyFrom;
            memcpy(copyForward, copyFrom, s - copyFrom);
            TSAssert(bytesCopy > 0);

            copyForward += bytesCopy;
            copyFrom = s + 1;
          } else {
            copyForward = s;
            copyFrom    = s + 1;
          }

          // Scroll past the escape character
          s++;

          // Handle the case that places us
          //  at the end of the file
          if (*s == '\0') {
            break;
          }
        } else if (*s == '"') {
          state = CONSUME;
          *s    = '\0';
        }
      } else if ((*s == '\\' && is_digit(*(s + 1))) || !is_char(*s)) {
        // INKqa10511
        // traffic server need to handle unicode characters
        // right now ignore the entry
        return "Unrecognized encoding scheme";
      } else if (*s == ' ') {
        state = CONSUME;
        *s    = '\0';
      }

      s++;

      // If we are now at the end of the line,
      //   we need to consume final data
      if (*s == '\0') {
        state = CONSUME;
      }
      break;
    case CONSUME:
      break;
    }

    if (state == CONSUME) {
      // See if there are any quote copy overs
      //   we've pushed into the future
      if (copyForward != NULL) {
        int toCopy = (s - 1) - copyFrom;
        memcpy(copyForward, copyFrom, toCopy);
        *(copyForward + toCopy) = '\0';
      }

      /* p_line->line[0][num_el] = label;
      p_line->line[1][num_el] = val;
      type                    = MATCH_NONE;

      // Check to see if this the primary specifier we are looking for
      if (tags->match_host && strcasecmp(tags->match_host, label) == 0) {
        type = MATCH_HOST;
      } else if (tags->match_domain && strcasecmp(tags->match_domain, label) == 0) {
        type = MATCH_DOMAIN;
      } else if (tags->match_regex && strcasecmp(tags->match_regex, label) == 0) {
        type = MATCH_REGEX;
      } else if (tags->match_url && strcasecmp(tags->match_url, label) == 0) {
        type = MATCH_URL;
      } else if (tags->match_host_regex && strcasecmp(tags->match_host_regex, label) == 0) {
        type = MATCH_HOST_REGEX;
      } */

      if ((strlen(label) == strlen("dest_domain")) && (strcmp(label, "dest_domain")) == 0 && !(p_line->dest_domain)) {
        p_line->dest_domain = TSstrdup(val);
      } else if ((strlen(label) == strlen("action")) && (strcmp(label, "action")) == 0 && !(p_line->action)) {
        p_line->action = TSstrdup(val);

      } else if ((strlen(label) == strlen("scheme")) && (strcmp(label, "scheme")) == 0 && !(p_line->http_method)) {
        if (strcmp(val, "http") == 0) {
          p_line->http_method = 1;
        } else {
          p_line->http_method = 0;
        }
      } else if ((strlen(label) == strlen("revalidate")) && (strcmp(label, "revalidate")) == 0 && !(p_line->revalidate)) {
        p_line->revalidate = atoi(val);
      } else if ((strlen(label) == strlen("suffix")) && (strcmp(label, "suffix")) == 0 && !(p_line->suffix)) {
        p_line->suffix = TSstrdup(val);
      } else if ((strlen(label) == strlen("ttl-in-cache")) && (strcmp(label, "ttl-in-cache")) == 0 && !(p_line->ttl_in_cache)) {
        p_line->ttl_in_cache = atoi(val);
      } else if ((strlen(label) == strlen("prefix")) && (strcmp(label, "prefix")) == 0 && !(p_line->prefix)) {
        p_line->prefix = TSstrdup(val);
      } else {
        TSError("unvalid token, skip!");
      }

      num_el++;

      if (num_el > MATCHER_MAX_TOKENS) {
        return "Malformed line: Too many tokens";
      }

      state = FIND_LABEL;
    }
  } while (*s != '\0');

  if (state != CONSUME && state != FIND_LABEL) {
    return "Malformed entry";
  }

  return NULL;
}

bool
load_config_file(const char *config_file, link_info *global_config)
{
  char buffer[1024];
  TSFile fh;
  line_info *first   = NULL;
  line_info *current = NULL;
  line_info *last    = NULL;
  char *eol          = NULL;

  /* locations in a config file line, end of line, split start, split end */
  int lineno = 0;

  // type counts
  int numEntries = 0;
  int line_num   = 0;

  TSDebug(PLUGIN_NAME, "Opening config file: %s", config_file);
  fh = TSfopen(config_file, "r");

  if (!fh) {
    TSError("[%s] Unable to open %s. No patterns will be loaded", PLUGIN_NAME, config_file);
    return false;
  }

  while (TSfgets(fh, buffer, sizeof(buffer) - 1)) {
    lineno++;
    char *tmp = buffer;

    // make sure line was not bigger than buffer
    if ((eol = strchr(buffer, '\n')) == NULL && (eol = strstr(buffer, "\r\n")) == NULL) {
      // Malformed line - skip
      TSError("%s: config line too long, did not get a good line in cfg, skipping, line: %s", PLUGIN_NAME, buffer);
      memset(buffer, 0, sizeof(buffer));
      continue;
    } else {
      *eol = 0;
    }
    // make sure line has something useful on it
    // or allow # Comments, only at line beginning
    if (eol - buffer < 2 || buffer[0] == '#') {
      memset(buffer, 0, sizeof(buffer));
      continue;
    }

    while (*tmp && isspace(*tmp)) {
      tmp++;
    }

    const char *errptr;

    current = (line_info *)ats_malloc(sizeof(line_info));
    memset(current, 0, sizeof(line_info));
    errptr = parseConfigLine((char *)tmp, current, &tags);

    if (errptr != NULL) {
      TSfclose(fh);
      ats_free(current);
      TSError("configure file parsed error: %s", config_file);
      return false;
    } else {
      // Line parsed ok.  Figure out what the destination
      //  type is and link it into our list
      numEntries++;
      current->line_num = line_num;

      if (first == NULL) {
        TSAssert(last == NULL);
        first = last = current;
      } else {
        last->next = current;
        last       = current;
      }
    }
  }

  TSfclose(fh);
  TSAssert(first);
  global_config->head = first;
  global_config->num  = numEntries;

  return true;
}
