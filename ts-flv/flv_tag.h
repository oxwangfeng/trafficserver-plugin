
#ifndef _FLV_TAG_H
#define _FLV_TAG_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ts/ts.h>

#define FLV_TYPE_BYTES  0
#define FLV_TYPE_TIME   1

#define FLV_PARSE_OVER  10

// change by dnion
// flv 配置
struct FlvConfig
{
    int     type;
    int     use_metadata;
    char    start_arg[24];
    char    end_arg[24];
};

class FlvTag;
typedef int (FlvTag::*FTHandler) ();

class FlvTag
{
public:
    FlvTag(): tag_buffer(NULL), tag_reader(NULL), dup_reader(NULL),
        head_buffer(NULL), head_reader(NULL),
        tag_pos(0), dup_pos(0), url(NULL),
        start_pos(0), end_pos(0),total_offset(0), cl(0), content_length(0),
        has_metadata(0), zero_tag_count(0), start(0), end(0), key_found(false),
        config(NULL)
    {
        tag_buffer = TSIOBufferCreate();
        tag_reader = TSIOBufferReaderAlloc(tag_buffer);
        dup_reader = TSIOBufferReaderAlloc(tag_buffer);

        head_buffer = TSIOBufferCreate();
        head_reader = TSIOBufferReaderAlloc(head_buffer);

        current_handler = &FlvTag::process_header;
    }

    ~FlvTag()
    {
        if (tag_reader) {
            TSIOBufferReaderFree(tag_reader);
            tag_reader = NULL;
        }

        if (dup_reader) {
            TSIOBufferReaderFree(dup_reader);
            dup_reader = NULL;
        }

        if (tag_buffer) {
            TSIOBufferDestroy(tag_buffer);
            tag_buffer = NULL;
        }

        if (head_reader) {
            TSIOBufferReaderFree(head_reader);
            head_reader = NULL;
        }

        if (head_buffer) {
            TSIOBufferDestroy(head_buffer);
            head_buffer = NULL;
        }
    }

    int process_tag(TSIOBufferReader reader, bool complete);
    int64_t write_out(TSIOBuffer buffer);

    int process_header();
    int process_initial_body();

    // change by dnion
    int parse_script_tag(const char *data, int64_t size);
    int parse_script_tag_with_times(const char *data, int64_t size);
    int process_medial_body();
    int process_medial_body_by_time();
    int process_output_data();

public:
    TSIOBuffer          tag_buffer;
    TSIOBufferReader    tag_reader;
    TSIOBufferReader    dup_reader;

    TSIOBuffer          head_buffer;
    TSIOBufferReader    head_reader;

    FTHandler           current_handler;
    int64_t             tag_pos;
    int64_t             dup_pos;

    const char          *url;
    int64_t             start_pos;
    int64_t             end_pos;
    int64_t             total_offset;
    int64_t             cl;
    int64_t             content_length;

    int                 has_metadata;
    int                 zero_tag_count;

    double              start;
    double              end;
    bool                key_found;
    bool                file_complete;
    FlvConfig           *config;
};

#endif

