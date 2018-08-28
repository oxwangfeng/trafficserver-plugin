
#include "flv_tag.h"
#include "flv_common.h"

#define PRINT_METADATA      1

#define FLV_NUMBER      0
#define FLV_BOOLEAN     1
#define FLV_STRING      2
#define FLV_OBJECT      3
#define FLV_MOVIECLIP   4
#define FLV_NULL        5
#define FLV_UNDEFINED   6
#define FLV_REFERENCE   7
#define FLV_ECMA        8
#define FLV_END         9
#define FLV_STRICTARRAY 10
#define FLV_DATE        11
#define FLV_LONGSTRING  12

#define al_flv_get_value8(p)                      \
    ( ((uint64_t) ((u_char *) (p))[0] << 56)        \
      + ((uint64_t) ((u_char *) (p))[1] << 48)      \
      + ((uint64_t) ((u_char *) (p))[2] << 40)      \
      + ((uint64_t) ((u_char *) (p))[3] << 32)      \
      + ((uint64_t) ((u_char *) (p))[4] << 24)      \
      + (           ((u_char *) (p))[5] << 16)      \
      + (           ((u_char *) (p))[6] << 8)       \
      + (           ((u_char *) (p))[7]) )

#define al_flv_get_value4(p)   \
    (((uint32_t)((u_char *)(p))[0] << 24)   \
     + (        ((u_char *)(p))[1] << 16)   \
     + (        ((u_char *)(p))[2] << 8)    \
     + (        ((u_char *)(p))[3]))

#define al_flv_get_value3(p)   \
    (((uint32_t)((u_char *)(p))[0] << 16)   \
     + (        ((u_char *)(p))[1] << 8)    \
     + (        ((u_char *)(p))[2]))

#define al_flv_get_value2(p)   \
    (((uint32_t)((u_char *)(p))[0] << 8)   \
     + (        ((u_char *)(p))[1]))

typedef union
{
    uint64_t    uint64_value;
    double      double_value;
} uni_double;


// 自动free的buffer
class AlBuffer
{
public:
    AlBuffer() : data(NULL), size(0)
    {
    }

    ~AlBuffer()
    {
        if (data)
        {
            TSfree(data);
            data = NULL;
        }
    }

    inline uint64_t alloc(uint64_t s)
    {
        if (data)
        {
            TSfree(data);
            size = 0;
        }

        data = (char *)TSmalloc(s);
        if (data)
        {
            size = s;
        }

        return size;
    }

public:
    char *data;
    uint64_t    size;
};

static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);
static double FlvGetDoubleValue(const char* data);


int
FlvTag::process_tag(TSIOBufferReader readerp, bool complete)
{
    int64_t     avail, head_avail;
    int         rc;

    avail = TSIOBufferReaderAvail(readerp);
    TSIOBufferCopy(tag_buffer, readerp, avail, 0);

    // 记录当前flv是否已经接收完全
    // 没有metatada信息时,如果start在最后一个关键帧内,无法通过跟关键帧的比较还获取
    // 认为当文件接收完全,但还没有找到start位置时,start就是最后一个关键帧
    file_complete = complete;

    TSIOBufferReaderConsume(readerp, avail);

    rc = (this->*current_handler)();

    if (rc == 0 && complete) {
        rc = -1;
    }

    if (rc) {       // success or error.
        head_avail = TSIOBufferReaderAvail(head_reader);

        if (has_metadata)
        {
            if (end_pos <= 0)
            {
                content_length = head_avail;
            }
            else
            {
                content_length = head_avail + end_pos - start_pos;
            }
        }
        else
        {
            content_length = head_avail + cl - dup_pos;
        }
        //TSError("process tag, start_pos=%zd, end_pos=%zd, content_length=%zd",
        //        start_pos, end_pos, content_length);
    }

    return rc;
}

int64_t
FlvTag::write_out(TSIOBuffer buffer)
{
    int64_t     head_avail, dup_avail, pass;

    head_avail = TSIOBufferReaderAvail(head_reader);
    dup_avail = TSIOBufferReaderAvail(dup_reader);
    pass = 0;

    if (head_avail > 0) {
        TSIOBufferCopy(buffer, head_reader, head_avail, 0);
        TSIOBufferReaderConsume(head_reader, head_avail);
    }

    // 判断要发送的数据
    if (dup_avail > 0)
    {
        pass = end_pos - dup_pos;
        if (pass < 0)
        {
            pass = 0;
        }
        if (pass > dup_avail)
        {
            pass = dup_avail;
        }
        if (pass > 0)
        {
            TSIOBufferCopy(buffer, dup_reader, pass, 0);
        }
        TSIOBufferReaderConsume(dup_reader, dup_avail);
    }

    total_offset = dup_pos + pass;
    //TSError("write out, start_pos=%zd, end_pos=%zd, content_length=%zd, total_offset=%zd",
    //        start_pos, end_pos, content_length, total_offset);
    return head_avail + pass;
}

int
FlvTag::process_header()
{
    int64_t     avail;
    char        buf[13];

    avail = TSIOBufferReaderAvail(tag_reader);
    if (avail < 13)
        return 0;

    IOBufferReaderCopy(tag_reader, buf, 13);

    // 如果不是合法的FLV文件,返回全文件
    if ((buf[0] != 'F' || buf[1] != 'L' || buf[2] != 'V')
        || (*(uint32_t*)(buf + 9) != 0))
    {
        start_pos = 0;
        end_pos = cl;
        TSError("ts-flv, invalid flv file, %s", url);
        this->current_handler = &FlvTag::process_output_data;
        return process_output_data();
    }

    TSIOBufferCopy(head_buffer, tag_reader, 13, 0);
    TSIOBufferReaderConsume(tag_reader, 13);

    tag_pos += 13;

    // 首次给start_pos赋值
    start_pos = tag_pos;
    end_pos = cl;

    this->current_handler = &FlvTag::process_initial_body;
    return process_initial_body();
}

// 此函数分析metadata信息，找到start和end对应的偏移量
int
FlvTag::parse_script_tag(const char* buffer, int64_t size)
{
    int64_t    offset, key_offset, prev_offset;
    uint32_t    entries;
    int         type;
    bool        start_key_found;

    if (this->config->type == FLV_TYPE_TIME)
    {
        return parse_script_tag_with_times(buffer, size);
    }

    const char *data = buffer;
    offset = 11;
    // "filepositions"
    if (this->config->type == FLV_TYPE_BYTES)
    {
        offset = 11;
        while (offset + (int64_t)sizeof("filepositions") - 1 < size)
        {
            if (data[offset] == 'f' && data[offset + 1] == 'i'
                && al_flv_get_value2(data + offset - 2) == sizeof("filepositions") - 1)
            {
                if (strncmp(data + offset, "filepositions", sizeof("filepositions") - 1) == 0)
                {
                    offset += sizeof("filepositions") - 1;
                    break;
                }
            }
            offset++;
        }
        if (offset + (int64_t)sizeof("filepositions") - 1 >= size)
        {
            return 0;
        }

        type = data[offset];
        entries = al_flv_get_value4(data + offset + 1);
        if (type != FLV_STRICTARRAY || offset + 9 * entries >= size)
        {
            return 0;
        }
        start_key_found = false;

        offset += 5;
        prev_offset = 0;

        // last key frame offset
        key_offset = (int64_t)FlvGetDoubleValue(data + offset + (entries - 1) * 9 + 1);

        // 如果start偏移量超过最后一个关键帧开始的位置
        // 就赋值,不再寻找start
        if ((int64_t) start >= key_offset)
        {
            start = (double)key_offset;
            start_pos = key_offset;
            start_key_found = true;
        }

        if ((int64_t)end >= cl || (int64_t)end <= 0)
        {
            end = 0;
            end_pos = cl;
        }
        else if ((int64_t)end < cl && key_offset <= (int64_t)end)
        {
            end = 0;
            end_pos = key_offset;
        }

        for (uint32_t i = 0; i < entries; i++)
        {
            type = data[offset + i * 9];
            if (type != FLV_NUMBER)
            {
                TSError("Zerkkro-> strict array invalid type: %d", type);
                return 0;
            }
            key_offset = (uint64_t)FlvGetDoubleValue(data + offset + i * 9 + 1);

            if (!start_key_found)
            {
                if ((int64_t)start < key_offset)
                {
                    start_key_found = true;

                    if (prev_offset == 0)
                        start_pos = key_offset;
                    else
                        start_pos = prev_offset;
                }
            }
            if (start_key_found)
            {
                if (end > 0)
                {
                    if ((int64_t)end < key_offset)
                    {
                        if (prev_offset == 0)
                            end_pos = key_offset;
                        else
                            end_pos = prev_offset;


                        if (end_pos <= start_pos)
                        {
                            end_pos = 0;
                        }

                        break;
                    }
                }
                else
                {
                    break;
                }
            }

            prev_offset = key_offset;
        }
    }

    //TSError("Zerkkro-> ts-flv: start=%lf, end=%lf, start_pos=%zd, end_pos=%zd",
    //        start, end, start_pos, end_pos);
    return 1;
}

int 
FlvTag::parse_script_tag_with_times(const char *buffer, int64_t size)
{
    uint32_t        entries;
    uint32_t        start_key, end_key;
    bool            start_key_found, end_key_found;
    int64_t         offset;
    double          duration, key_value, last_key_value;
    const char      *filepositions_pos, *times_pos;
    int             type;

    const char *data = buffer;
    start_key = 0;
    end_key = 0;
    start_key_found = false;
    end_key_found = false;
    duration = 0;
    key_value = 0;
    offset = 11;
    filepositions_pos = NULL;
    times_pos = NULL;

    while (offset + (int64_t)sizeof("duration") - 1 < size)
    {
        if (data[offset] == 'd' && data[offset + 1] == 'u')
        {
            if (strncmp(data + offset, "duration", sizeof("duration") - 1) == 0)
            {
                offset += sizeof("duration") - 1;
                break;
            }
        }
        offset++;
    }
    if (offset + (int64_t)sizeof("duration") - 1 >= size)
    {
        return 0;
    }
    type = data[offset];
    duration = FlvGetDoubleValue(data + offset + 1);
    if (start >= duration)
    {
        start_pos = 0;
        return 1;
    }

    offset = 11;
    while (offset + (int64_t)sizeof("times") - 1 < size)
    {
        if ( data[offset] == 't' && data[offset + 1] == 'i'
            && al_flv_get_value2(data + offset - 2) == sizeof("times") - 1)
        {
            if (strncmp(data + offset, "times", sizeof("times") - 1) == 0)
            {
                offset += sizeof("times") - 1;
                break;
            }
        }
        offset++;
    }
    if (offset + (int64_t)sizeof("times") - 1 >= size)
    {
        return 0;
    }
    type = data[offset];
    entries = al_flv_get_value4(data + offset + 1);
    if (type != FLV_STRICTARRAY || offset + entries * 9 + 1 >= size)
    {
        return 0;
    }
    times_pos = data + offset + 5;

    offset = 11;
    while (offset + (int64_t)sizeof("filepositions") - 1 < size)
    {
        if (data[offset] == 'f' && data[offset + 1] == 'i'
                && al_flv_get_value2(data + offset - 2) == sizeof("filepositions") - 1)
        {
            if (strncmp(data + offset, "filepositions", sizeof("filepositions") - 1) == 0)
            {
                offset += sizeof("filepositions") - 1;
                break;
            }
        }
        offset++;
    }
    if (offset + (int64_t)sizeof("filepositions") - 1 >= size)
    {
        return 0;
    }
    type = data[offset];
    entries = al_flv_get_value4(data + offset + 1);
    if (type != FLV_STRICTARRAY || offset + entries * 9 + 1 >= size)
    {
        return 0;
    }
    filepositions_pos = data + offset + 5;

    // last key frame
    last_key_value = FlvGetDoubleValue(times_pos + 9 * (entries - 1) + 1);
    if (start > last_key_value)
    {
        start_key_found = true;
        start_key = entries - 1;
    }
    if (end >= duration || end <= 0)
    {
        end_key_found = true;
        end_key = entries;
    }
    else if (end >= last_key_value)
    {
        end_key_found = true;
        end_key = entries - 1;
    }

    for (uint32_t i = 0; i < entries; i++)
    {
        key_value = FlvGetDoubleValue(times_pos + 9 * i + 1);

        if (!start_key_found)
        {
            if (start < key_value)
            {
                start_key = i - 1;
                start_key_found = true;
            }
        }
        if (start_key_found)
        {
            if (end > 0 && end < key_value)
            {
                end_key = i - 1;
                end_key_found = true;
                break;
            }
        }

        if (start_key_found && end_key_found)
        {
            break;
        }
    }

    if (end_key <= start_key)
    {
        end_pos = 0;
        return 1;
    }

    start_pos = (int64_t)FlvGetDoubleValue(filepositions_pos + 9 * start_key + 1);
    if (end_key >= entries)
    {
        end_pos = cl;
    }
    else
    {
        end_pos = (int64_t)FlvGetDoubleValue(filepositions_pos + 9 * end_key + 1);
    }

    return 1;
}

int
FlvTag::process_initial_body()
{
    int64_t     avail, sz;
    uint32_t    n, ts;
    char        buf[12];

    avail = TSIOBufferReaderAvail(tag_reader);

    do {
        if (avail < 11 + 1)     // tag head + 1 byte
            return 0;

        IOBufferReaderCopy(tag_reader, buf, 12);

        n = (uint32_t)((uint8_t)buf[1] << 16) +
            (uint32_t)((uint8_t)buf[2] << 8) +
            (uint32_t)((uint8_t)buf[3]);

        sz = 11 + n + 4;

        if (avail < sz)     // insure the whole tag
            return 0;

        ts = al_flv_get_value3(buf + 4);
        if (ts != 0)
            goto end;

        // 将脚本帧和第一个0关键帧拷贝到head_buffer
        if (buf[0] == 18)
        {
            // Zerkkro: script tag
            if (this->config->use_metadata)
            {
                AlBuffer    script_buffer;
                char        *data;

                script_buffer.alloc(sz);
                data = script_buffer.data;
                if (data == NULL)
                {
                    return 0;
                }

                IOBufferReaderCopy(tag_reader, data, sz);
                has_metadata = parse_script_tag(data, sz);
            }
        }
        else
        {
            // 按照lighttpd的做法
            // 发送flv文件的前两个0数据帧
            /*
            if (buf[0] == 9 && (((uint8_t)buf[11]) >> 4) == 1)
            {
                if (!key_found)
                {
                    key_found = true;
                }
                else
                {
                    goto end;
                }
            }*/
            zero_tag_count++;
            if (zero_tag_count > 2)
                goto end;
        }

        TSIOBufferCopy(head_buffer, tag_reader, sz, 0);
        TSIOBufferReaderConsume(tag_reader, sz);
        avail -= sz;

        tag_pos += sz;

    } while (avail > 0);

    return 0;

end:

    TSIOBufferReaderConsume(dup_reader, tag_pos);
    dup_pos = tag_pos;

    // dup_pos之前的数据是必然会发的
    // 所以如果start_pos比dup_pos小,start_pos取dup_pos的值
    if (start_pos < dup_pos)
        start_pos = dup_pos;

    key_found = false;
    if (!has_metadata)
    {
#if PRINT_METADATA
        TSError("%s, --- metadata invalid", url);
#endif
        if (this->config->type == FLV_TYPE_BYTES)
        {
            this->current_handler = &FlvTag::process_medial_body;
            return process_medial_body();
        }
        else
        {
            this->current_handler = &FlvTag::process_medial_body_by_time;
            return process_medial_body_by_time();
        }
    }

    this->current_handler = &FlvTag::process_output_data;
    return process_output_data();
}

int
FlvTag::process_medial_body()
{
    int64_t     avail, sz, pass;
    uint32_t    n;
    char        buf[12];

    avail = TSIOBufferReaderAvail(tag_reader);

    do {
        if (avail < 11 + 1)     // tag head + 1 byte
            return 0;

        IOBufferReaderCopy(tag_reader, buf, 12);

        n = (uint32_t)((uint8_t)buf[1] << 16) +
            (uint32_t)((uint8_t)buf[2] << 8) +
            (uint32_t)((uint8_t)buf[3]);

        sz = 11 + n + 4;

        if (avail < sz)     // insure the whole tag
            return 0;

        // 遍历方式解析start的位置
        if (buf[0] == 9 && (((uint8_t)buf[11]) >> 4) == 1)    // key frame
        {
            if (tag_pos <= (int64_t)start) 
            {
                pass = tag_pos - dup_pos;
                if (pass > 0)
                {
                    //TSError("Zerkkro, ts-flv medial body, consume, dup_pos=%zd, "
                    //        "pass=%zd",
                    //        dup_pos, pass);
                    TSIOBufferReaderConsume(dup_reader, pass);
                    dup_pos = tag_pos;
                }

                key_found = true;
            }
            else
            {
                return 1;
            }
        }

        TSIOBufferReaderConsume(tag_reader, sz);
        avail -= sz;

        tag_pos += sz;

    } while (avail > 0);

    // file_complete
    if (file_complete)
    {
        //TSError("Zerkkro, ts-flv, file complete, but start_pos hasn't been found"
        //        "start_pos=%zd",
        //        start_pos);

        return 1;
    }

    return 0;
}

int
FlvTag::process_medial_body_by_time()
{
    int64_t     avail, sz, pass;
    uint32_t    n, ts;
    char        buf[12];

    avail = TSIOBufferReaderAvail(tag_reader);

    do
    {
        if (avail < 11 + 1)
            return 0;

        IOBufferReaderCopy(tag_reader, buf, 12);

        n = al_flv_get_value3(buf + 1);

        sz = 11 + n + 4;

        if (avail < sz)
            return 0;

        if (buf[0] == 9 && (((uint8_t)buf[11]) >> 4) == 1)
        {
            ts = al_flv_get_value3(buf + 4);

            if (ts <= (uint32_t)(1000 * start))
            {
                pass = tag_pos - dup_pos;
                if (pass > 0)
                {
                    TSIOBufferReaderConsume(dup_reader, pass);
                    dup_pos = tag_pos;
                }
                key_found = true;
            }
            else
            {
                return 1;
            }
        }

        TSIOBufferReaderConsume(tag_reader, sz);
        avail -= sz;

        tag_pos += sz;
    } while (avail > 0);

    if (file_complete)
    {
        return 1;
    }

    return 0;
}

// 此函数判断当前可用的数据中是否包含可以发送给客户的数据
// 如果flv文件包含可用的metadata信息,在分析完metadata后会调用此函数
// 如果没有可用的metadata信息,则在逐帧解析出start_pos后调用
int
FlvTag::process_output_data()
{
    int64_t     avail, pass;

    avail = TSIOBufferReaderAvail(dup_reader);

    if (avail <= 0)
    {
        return 0;
    }

    // 丢掉start_pos之前的数据
    if (dup_pos + avail > start_pos)
    {
        pass = start_pos - dup_pos;
        if (pass > 0)
        {
            TSIOBufferReaderConsume(dup_reader, pass);
            dup_pos += pass;
            //avail -= pass;
            TSIOBufferReaderConsume(tag_reader, pass);
        }
        return 1;
    }
    else
    {
        TSIOBufferReaderConsume(dup_reader, avail);
        TSIOBufferReaderConsume(tag_reader, avail);
        dup_pos += avail;
        return 0;
    }

    // end_pos之前的有效数据
    /*
    if (dup_pos < end_pos)
    {
        pass = (end_pos - dup_pos) > avail ? avail : (end_pos - dup_pos);
        TSIOBufferCopy(out_buffer, dup_reader, pass, 0);
        dup_pos += avail;
        TSIOBufferReaderConsume(dup_reader, avail);
    }
    else
    {
        TSIOBufferReaderConsume(dup_reader, avail);
        return FLV_PARSE_OVER;
    }*/

    return 0;
}


static int64_t
IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length)
{
    int64_t             avail, need, n;
    const char          *start;
    TSIOBufferBlock     blk;

    n = 0;
    blk = TSIOBufferReaderStart(readerp);

    while (blk) {
        start = TSIOBufferBlockReadStart(blk, readerp, &avail);
        need = length < avail ? length : avail;

        if (need > 0) {
            memcpy((char*)buf + n, start, need);
            length -= need;
            n += need;
        }

        if (length == 0)
            break;

        blk = TSIOBufferBlockNext(blk);
    }

    return n;
}

// 获取double值
static double FlvGetDoubleValue(const char* data)
{
    uni_double ret;
    ret.uint64_value = al_flv_get_value8(data);

    return ret.double_value;
}

