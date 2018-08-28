/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "mp4_meta.h"

#define DEBUG_LOG   0

#if DEBUG_LOG
#define al_log_debug(fmt, args...) TSError("<%d>[%s] " fmt, __LINE__, __FUNCTION__, ##args)
#else
#define al_log_debug(fmt, args...) 
#endif

static mp4_atom_handler mp4_atoms[] = {
    { "ftyp", &Mp4Meta::mp4_read_ftyp_atom },
    { "moov", &Mp4Meta::mp4_read_moov_atom },
    { "mdat", &Mp4Meta::mp4_read_mdat_atom },
    { NULL, NULL }
};

static mp4_atom_handler mp4_moov_atoms[] = {
    { "mvhd", &Mp4Meta::mp4_read_mvhd_atom },
    { "trak", &Mp4Meta::mp4_read_trak_atom },
    { "cmov", &Mp4Meta::mp4_read_cmov_atom },
    { NULL, NULL }
};

static mp4_atom_handler mp4_trak_atoms[] = {
    { "tkhd", &Mp4Meta::mp4_read_tkhd_atom },
    { "mdia", &Mp4Meta::mp4_read_mdia_atom },
    { NULL, NULL }
};

static mp4_atom_handler mp4_mdia_atoms[] = {
    { "mdhd", &Mp4Meta::mp4_read_mdhd_atom },
    { "hdlr", &Mp4Meta::mp4_read_hdlr_atom },
    { "minf", &Mp4Meta::mp4_read_minf_atom },
    { NULL, NULL }
};

static mp4_atom_handler mp4_minf_atoms[] = {
    { "vmhd", &Mp4Meta::mp4_read_vmhd_atom },
    { "smhd", &Mp4Meta::mp4_read_smhd_atom },
    { "dinf", &Mp4Meta::mp4_read_dinf_atom },
    { "stbl", &Mp4Meta::mp4_read_stbl_atom },
    { NULL, NULL }
};

static mp4_atom_handler mp4_stbl_atoms[] = {
    { "stsd", &Mp4Meta::mp4_read_stsd_atom },
    { "stts", &Mp4Meta::mp4_read_stts_atom },
    { "stss", &Mp4Meta::mp4_read_stss_atom },
    { "ctts", &Mp4Meta::mp4_read_ctts_atom },
    { "stsc", &Mp4Meta::mp4_read_stsc_atom },
    { "stsz", &Mp4Meta::mp4_read_stsz_atom },
    { "stco", &Mp4Meta::mp4_read_stco_atom },
    { "co64", &Mp4Meta::mp4_read_co64_atom },
    { NULL, NULL }
};


static void mp4_reader_set_32value(TSIOBufferReader readerp, int64_t offset, uint32_t n);
static void mp4_reader_set_64value(TSIOBufferReader readerp, int64_t offset, uint64_t n);
static uint32_t mp4_reader_get_32value(TSIOBufferReader readerp, int64_t offset);
static uint64_t mp4_reader_get_64value(TSIOBufferReader readerp, int64_t offset);
static int64_t IOBufferReaderCopy(TSIOBufferReader readerp, void *buf, int64_t length);


int
Mp4Meta::parse_meta(bool body_complete)
{
    int             ret, rc;

    meta_avail = TSIOBufferReaderAvail(meta_reader);

    if (wait_next && wait_next <= meta_avail) {
        mp4_meta_consume(wait_next);
        wait_next = 0;
    }

    if (meta_avail < MP4_MIN_BUFFER_SIZE && !body_complete)
        return 0;

    ret = this->parse_root_atoms();

    if (ret < 0) {
        return -1;

    } else if (ret == 0) {

        if (body_complete) {
            return -1;

        } else {
            return 0;
        }
    }

    // generate new meta data
    rc = this->post_process_meta();
    if (rc != 0) {
        return -1;
    }

    return 1;
}

void
Mp4Meta::mp4_meta_consume(int64_t size)
{
    TSIOBufferReaderConsume(meta_reader, size);
    meta_avail -= size;
    passed += size;
}


int
Mp4Meta::post_process_meta()
{
    off_t           start_offset, end_offset, adjustment;
    uint32_t        i, j;
    int64_t         avail;
    BufferHandle    *bh;
    Mp4Trak         *trak;



    if (this->trak_num == 0) {
        al_log_debug("trak_num 0 ");
        return -1;
    }

    if (mdat_atom.buffer == NULL) {
        al_log_debug("mdat_atom not found ");
        return -1;
    }

#if DEBUG_LOG
    al_log_debug("mp4->post_process_meta, trak_num: %d", trak_num);
#endif

    out_handle.buffer = TSIOBufferCreate();
    out_handle.reader = TSIOBufferReaderAlloc(out_handle.buffer);

    if (ftyp_atom.buffer) {
        TSIOBufferCopy(out_handle.buffer, ftyp_atom.reader,
                       TSIOBufferReaderAvail(ftyp_atom.reader), 0);
    }

    if (moov_atom.buffer) {
        TSIOBufferCopy(out_handle.buffer, moov_atom.reader,
                       TSIOBufferReaderAvail(moov_atom.reader), 0);
    }

    if (mvhd_atom.buffer) {
        avail = TSIOBufferReaderAvail(mvhd_atom.reader);
        TSIOBufferCopy(out_handle.buffer, mvhd_atom.reader, avail, 0);
        this->moov_size += avail;
    }

    start_offset = cl;
    end_offset   = cl;

    // 将有stss的trak放到第一个
    for (i = 0; i < trak_num; i++)
    {
        trak = trak_vec[i];
        if (trak->stss_data.buffer != NULL)
        {
            // mp4_print_sync_value(trak);
            // 如果是按字节拖动,将字节转换成时间
            if (MP4_TYPE_TIME != this->type)
            {
                mp4_get_time_by_offset(trak);
            }
            if (i != 0)
            {
                trak_vec[i] = trak_vec[0];
                trak_vec[0] = trak;
            }
            break;
        }
    }

    //ts_mp4 start time > total time  return -1 ---> download the whole file  
    if(start > (trak_vec[0]->duration / trak_vec[0]->timescale) * 1000) {
        al_log_debug("ts-mp4 trak[0]--total time: %zd", trak_vec[0]->duration / trak_vec[0]->timescale * 1000);
        return -1;
    }
    if (this->end >= (trak_vec[0]->duration / trak_vec[0]->timescale) * 1000)
    {
        this->end = 0;
    }

    if (this->end > this->start) {
        this->length = this->end - this->start;
    }

#if DEBUG_LOG
    al_log_debug(" ");
    al_log_debug("------> start=%zd, end=%zd, length=%zd",
            this->start, this->end, this->length);
#endif

    for (i = 0; i < trak_num; i++) {
 
        //处理前2个trak 音频 视频流 针对网页播放的mp4文件 基本都是2个trak
        if(i > 1)
           break;

#if DEBUG_LOG
        al_log_debug(" ");
        al_log_debug("-----------------------> trak num: %d", i);
#endif
        trak = trak_vec[i];
      
#if DEBUG_LOG
        al_log_debug(" ");
        al_log_debug("-----------------------> update_stts_atom --");
#endif
        if (mp4_update_stts_atom(trak) != 0) {
            return -1;
        }

#if DEBUG_LOG
        al_log_debug(" ");
        al_log_debug("-----------------------> update_stss_atom --");
#endif
        if (mp4_update_stss_atom(trak) != 0) {
            return -1;
        }

#if DEBUG_LOG
        al_log_debug(" ");
        al_log_debug("-----------------------> update_ctts_atom --");
#endif
        mp4_update_ctts_atom(trak);

#if DEBUG_LOG
        al_log_debug(" ");
        al_log_debug("-----------------------> update_stsc_atom --");
#endif
        if (mp4_update_stsc_atom(trak) != 0) {
            return -1;
        }

#if DEBUG_LOG
        al_log_debug(" ");
        al_log_debug("-----------------------> update_stsz_atom --");
#endif
        if (mp4_update_stsz_atom(trak) != 0) {
            return -1;
        }

#if DEBUG_LOG
        al_log_debug(" ");
        al_log_debug("-----------------------> update stco/co64 --");
#endif
        if (trak->co64_data.buffer) {
           if (mp4_update_co64_atom(trak) != 0)
              return -1;
        } else if (mp4_update_stco_atom(trak) != 0) {
              return -1;
        }

        mp4_update_stbl_atom(trak);
        mp4_update_minf_atom(trak);
        trak->size += trak->mdhd_size;
        trak->size += trak->hdlr_size;
        mp4_update_mdia_atom(trak);
        trak->size += trak->tkhd_size;
        mp4_update_trak_atom(trak);

        this->moov_size += trak->size;

        if (start_offset > trak->start_offset)
            start_offset = trak->start_offset;

        if (this->length) {
           if (end_offset < trak->end_offset)
              end_offset = trak->end_offset;
        }

        bh = &(trak->trak_atom);
        for (j = 0; j <= MP4_LAST_ATOM; j++) {
            if (bh[j].buffer) {
                TSIOBufferCopy(out_handle.buffer, bh[j].reader,
                               TSIOBufferReaderAvail(bh[j].reader), 0);
            }
        }

        mp4_update_tkhd_duration(trak);
        mp4_update_mdhd_duration(trak);
    }

    this->moov_size += 8;

    mp4_reader_set_32value(moov_atom.reader, 0, this->moov_size);
    this->content_length += this->moov_size;

    adjustment = this->ftyp_size + this->moov_size +
            mp4_update_mdat_atom(start_offset, end_offset) - start_offset;

#if DEBUG_LOG
    al_log_debug("----------> adjustment=%zd, content_length=%zd",
            adjustment, this->content_length);
#endif

    TSIOBufferCopy(out_handle.buffer, mdat_atom.reader,
                   TSIOBufferReaderAvail(mdat_atom.reader), 0);

    for (i = 0; i < trak_num; i++) {
        trak = trak_vec[i];

        if (trak->co64_data.buffer) {
            mp4_adjust_co64_atom(trak, adjustment);
        } else {
            mp4_adjust_stco_atom(trak, adjustment);
        }
    }

    mp4_update_mvhd_duration();

    return 0;
}

/*
 * -1: error
 *  0: unfinished
 *  1: success.
 */
int
Mp4Meta::parse_root_atoms()
{
    int         i, ret, rc;
    int64_t     atom_size, atom_header_size;
    char        buf[64];
    char        *atom_header, *atom_name;

    memset(buf, 0, sizeof(buf));

    for (;;) {

        if (meta_avail < (int64_t)sizeof(uint32_t))
            return 0;

        IOBufferReaderCopy(meta_reader, buf, sizeof(mp4_atom_header64));
        atom_size = mp4_get_32value(buf);

        if (atom_size == 0) {
            return 1;
        }

        atom_header = buf;

        if (atom_size < (int64_t)sizeof(mp4_atom_header)) {

            if (atom_size == 1) {
                if (meta_avail < (int64_t)sizeof(mp4_atom_header64)) {
                    return 0;
                }
            } else {
                return -1;
            }

            atom_size = mp4_get_64value(atom_header + 8);
            atom_header_size = sizeof(mp4_atom_header64);

        } else {                                                     // regular atom

            if (meta_avail < (int64_t)sizeof(mp4_atom_header))       // not enough for atom header
                return 0;

            atom_header_size = sizeof(mp4_atom_header);
        }

        atom_name = atom_header + 4;

        if (atom_size + this->passed > this->cl) {
            return -1;
        }

        for (i = 0; mp4_atoms[i].name; i++) {
            if (memcmp(atom_name, mp4_atoms[i].name, 4) == 0) {

                ret = (this->*mp4_atoms[i].handler)(atom_header_size, atom_size - atom_header_size);           // -1: error, 0: unfinished, 1: success

                if (ret <= 0) {
                    return ret;
                } else if (meta_complete) {             // success
                    return 1;
                }

                goto next;
            }
        }

        // nonsignificant atom box
        rc = mp4_atom_next(atom_size, true);            // 0: unfinished, 1: success
        if (rc == 0) {
            return rc;
        }

next:
        continue;
    }

    return 1;
}

int
Mp4Meta::mp4_atom_next(int64_t atom_size, bool wait)
{
    if (meta_avail >= atom_size) {
        mp4_meta_consume(atom_size);
        return 1;
    }

    if (wait) {
        wait_next = atom_size;
        return 0;
    }

    return -1;
}


/*
 *  -1: error
 *   1: success
 */
int
Mp4Meta::mp4_read_atom(mp4_atom_handler *atom, int64_t size)
{
    int         i, ret, rc;
    int64_t     atom_size, atom_header_size;
    char        buf[32];
    char        *atom_header, *atom_name;

    if (meta_avail < size)    // data insufficient, not reasonable for internal atom box.
        return -1;

    while (size > 0) {

        if (meta_avail < (int64_t)sizeof(uint32_t))            // data insufficient, not reasonable for internal atom box.
            return -1;

        IOBufferReaderCopy(meta_reader, buf, sizeof(mp4_atom_header64));
        atom_size = mp4_get_32value(buf);

        if (atom_size == 0) {
            return 1;
        }

        atom_header = buf;

        if (atom_size < (int64_t)sizeof(mp4_atom_header)) {

            if (atom_size == 1) {
                if (meta_avail < (int64_t)sizeof(mp4_atom_header64)) {
                    return -1;
                }
            } else {
                return -1;
            }

            atom_size = mp4_get_64value(atom_header + 8);
            atom_header_size = sizeof(mp4_atom_header64);

        } else {                                        // regular atom

            if (meta_avail < (int64_t)sizeof(mp4_atom_header))
                return -1;

            atom_header_size = sizeof(mp4_atom_header);
        }

        atom_name = atom_header + 4;

        if (atom_size + this->passed > this->cl) {
            return -1;
        }

        for (i = 0; atom[i].name; i++) {
            if (memcmp(atom_name, atom[i].name, 4) == 0) {

                if (meta_avail < atom_size)
                    return -1;

                ret = (this->*atom[i].handler)(atom_header_size, atom_size - atom_header_size);       // -1: error, 0: success.

                if (ret < 0) {
                    return ret;
                }

                goto next;
            }
        }

        // insignificant atom box
        rc = mp4_atom_next(atom_size, false);
        if (rc < 0) {
            return rc;
        }

next:
        size -= atom_size;
        continue;
    }

    return 1;
}

int
Mp4Meta::mp4_read_ftyp_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t         atom_size;

    if (atom_data_size > MP4_MIN_BUFFER_SIZE) {
        al_log_debug("ftyp atom_data_size too large");
        return -1;
    } 

    atom_size = atom_header_size + atom_data_size;

    if (meta_avail < atom_size) {               // data unsufficient, reasonable from the first level
        return 0;
    }

    ftyp_atom.buffer = TSIOBufferCreate();
    ftyp_atom.reader = TSIOBufferReaderAlloc(ftyp_atom.buffer);

    TSIOBufferCopy(ftyp_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    content_length = atom_size;
    ftyp_size = atom_size;

    return 1;
}

int
Mp4Meta::mp4_read_moov_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t         atom_size;
    int             ret;

    if (mdat_atom.buffer != NULL) {     // not reasonable for streaming media
        al_log_debug("mdat_atom not found");
        return -1;
    }

    atom_size = atom_header_size + atom_data_size;

    if (atom_data_size >= MP4_MAX_BUFFER_SIZE) {
        al_log_debug("atom_data_size too large");
        return -1;
    }

    if (meta_avail < atom_size) {       // data unsufficient, wait
        return 0;
    }

    moov_atom.buffer = TSIOBufferCreate();
    moov_atom.reader = TSIOBufferReaderAlloc(moov_atom.buffer);

    TSIOBufferCopy(moov_atom.buffer, meta_reader, atom_header_size, 0);
    mp4_meta_consume(atom_header_size);

    ret = mp4_read_atom(mp4_moov_atoms, atom_data_size);

    return ret;
}

int
Mp4Meta::mp4_read_mvhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size;
    uint32_t            timescale;
    mp4_mvhd_atom       *mvhd;
    mp4_mvhd64_atom     mvhd64;

    if (sizeof(mp4_mvhd_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &mvhd64, sizeof(mp4_mvhd64_atom));
    mvhd = (mp4_mvhd_atom*)&mvhd64;

    if (mvhd->version[0] == 0) {
        timescale = mp4_get_32value(mvhd->timescale);
    } else {        // 64-bit duration
        timescale = mp4_get_32value(mvhd64.timescale);
    }

    this->timescale = timescale;

    atom_size = atom_header_size + atom_data_size;

    mvhd_atom.buffer = TSIOBufferCreate();
    mvhd_atom.reader = TSIOBufferReaderAlloc(mvhd_atom.buffer);

    TSIOBufferCopy(mvhd_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_trak_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int             rc;
    Mp4Trak         *trak;

    if (trak_num >= MP4_MAX_TRAK_NUM - 1){
        al_log_debug("trak num  >= %d Please check mp4 file",MP4_MAX_TRAK_NUM - 1);
        return -1;
    }

    trak = new Mp4Trak();
    trak_vec[trak_num++] = trak;

    trak->trak_atom.buffer = TSIOBufferCreate();
    trak->trak_atom.reader = TSIOBufferReaderAlloc(trak->trak_atom.buffer);

    TSIOBufferCopy(trak->trak_atom.buffer, meta_reader, atom_header_size, 0);
    mp4_meta_consume(atom_header_size);

    rc = mp4_read_atom(mp4_trak_atoms, atom_data_size);

    return rc;
}

int
Mp4Meta::mp4_read_cmov_atom(int64_t /*atom_header_size ATS_UNUSED */, int64_t /* atom_data_size ATS_UNUSED */)
{
    return -1;
}

int
Mp4Meta::mp4_read_tkhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size;
    Mp4Trak             *trak;

    atom_size = atom_header_size + atom_data_size;

    trak = trak_vec[trak_num-1];
    trak->tkhd_size = atom_size;

    trak->tkhd_atom.buffer = TSIOBufferCreate();
    trak->tkhd_atom.reader = TSIOBufferReaderAlloc(trak->tkhd_atom.buffer);

    TSIOBufferCopy(trak->tkhd_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    mp4_reader_set_32value(trak->tkhd_atom.reader,
                           offsetof(mp4_tkhd_atom, size), atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_mdia_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    Mp4Trak             *trak;

    trak = trak_vec[trak_num-1];

    trak->mdia_atom.buffer = TSIOBufferCreate();
    trak->mdia_atom.reader = TSIOBufferReaderAlloc(trak->mdia_atom.buffer);

    TSIOBufferCopy(trak->mdia_atom.buffer, meta_reader, atom_header_size, 0);
    mp4_meta_consume(atom_header_size);

    return mp4_read_atom(mp4_mdia_atoms, atom_data_size);
}

int
Mp4Meta::mp4_read_mdhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size, duration;
    uint32_t            ts;
    Mp4Trak             *trak;
    mp4_mdhd_atom       *mdhd;
    mp4_mdhd64_atom     mdhd64;

    IOBufferReaderCopy(meta_reader, &mdhd64, sizeof(mp4_mdhd64_atom));
    mdhd = (mp4_mdhd_atom*)&mdhd64;

    if (mdhd->version[0] == 0) {
        ts = mp4_get_32value(mdhd->timescale);
        duration = mp4_get_32value(mdhd->duration);
    } else {
        ts = mp4_get_32value(mdhd64.timescale);
        duration = mp4_get_64value(mdhd64.duration);
    }

    atom_size = atom_header_size + atom_data_size;

    trak = trak_vec[trak_num-1];
    trak->mdhd_size = atom_size;
    trak->timescale = ts;
    trak->duration = duration;

    trak->mdhd_atom.buffer = TSIOBufferCreate();
    trak->mdhd_atom.reader = TSIOBufferReaderAlloc(trak->mdhd_atom.buffer);

    TSIOBufferCopy(trak->mdhd_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    mp4_reader_set_32value(trak->mdhd_atom.reader,
                           offsetof(mp4_mdhd_atom, size), atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_hdlr_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size;
    Mp4Trak             *trak;

    atom_size = atom_header_size + atom_data_size;

    trak = trak_vec[trak_num - 1];
    trak->hdlr_size = atom_size;

    trak->hdlr_atom.buffer = TSIOBufferCreate();
    trak->hdlr_atom.reader = TSIOBufferReaderAlloc(trak->hdlr_atom.buffer);

    TSIOBufferCopy(trak->hdlr_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_minf_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    Mp4Trak             *trak;

    trak = trak_vec[trak_num - 1];

    trak->minf_atom.buffer = TSIOBufferCreate();
    trak->minf_atom.reader = TSIOBufferReaderAlloc(trak->minf_atom.buffer);

    TSIOBufferCopy(trak->minf_atom.buffer, meta_reader, atom_header_size, 0);
    mp4_meta_consume(atom_header_size);

    return mp4_read_atom(mp4_minf_atoms, atom_data_size);
}

int
Mp4Meta::mp4_read_vmhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size;
    Mp4Trak             *trak;

    atom_size = atom_data_size + atom_header_size;

    trak = trak_vec[trak_num - 1];
    trak->vmhd_size += atom_size;

    trak->vmhd_atom.buffer = TSIOBufferCreate();
    trak->vmhd_atom.reader = TSIOBufferReaderAlloc(trak->vmhd_atom.buffer);

    TSIOBufferCopy(trak->vmhd_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_smhd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size;
    Mp4Trak             *trak;

    atom_size = atom_data_size + atom_header_size;

    trak = trak_vec[trak_num - 1];
    trak->smhd_size += atom_size;

    trak->smhd_atom.buffer = TSIOBufferCreate();
    trak->smhd_atom.reader = TSIOBufferReaderAlloc(trak->smhd_atom.buffer);

    TSIOBufferCopy(trak->smhd_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_dinf_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size;
    Mp4Trak             *trak;

    atom_size = atom_data_size + atom_header_size;

    trak = trak_vec[trak_num - 1];
    trak->dinf_size += atom_size;

    trak->dinf_atom.buffer = TSIOBufferCreate();
    trak->dinf_atom.reader = TSIOBufferReaderAlloc(trak->dinf_atom.buffer);

    TSIOBufferCopy(trak->dinf_atom.buffer, meta_reader, atom_size, 0);
    mp4_meta_consume(atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_stbl_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    Mp4Trak             *trak;

    trak = trak_vec[trak_num - 1];

    trak->stbl_atom.buffer = TSIOBufferCreate();
    trak->stbl_atom.reader = TSIOBufferReaderAlloc(trak->stbl_atom.buffer);

    TSIOBufferCopy(trak->stbl_atom.buffer, meta_reader, atom_header_size, 0);
    mp4_meta_consume(atom_header_size);

    return mp4_read_atom(mp4_stbl_atoms, atom_data_size);
}

int
Mp4Meta::mp4_read_stsd_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int64_t             atom_size;
    Mp4Trak             *trak;

    atom_size = atom_data_size + atom_header_size;

    trak = trak_vec[trak_num - 1];
    trak->size += atom_size;

    trak->stsd_atom.buffer = TSIOBufferCreate();
    trak->stsd_atom.reader = TSIOBufferReaderAlloc(trak->stsd_atom.buffer);

    TSIOBufferCopy(trak->stsd_atom.buffer, meta_reader, atom_size, 0);

    mp4_meta_consume(atom_size);

    return 1;
}

int
Mp4Meta::mp4_read_stts_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int32_t         entries;
    int64_t         esize;
    mp4_stts_atom   stts;
    Mp4Trak         *trak;

    if (sizeof(mp4_stts_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &stts, sizeof(mp4_stts_atom));

    entries = mp4_get_32value(stts.entries);
    esize = entries * sizeof(mp4_stts_entry);

    if (sizeof(mp4_stts_atom) - 8 + esize > (size_t)atom_data_size)
        return -1;

    trak = trak_vec[trak_num - 1];
    trak->time_to_sample_entries = entries;

    trak->stts_atom.buffer = TSIOBufferCreate();
    trak->stts_atom.reader = TSIOBufferReaderAlloc(trak->stts_atom.buffer);
    TSIOBufferCopy(trak->stts_atom.buffer, meta_reader, sizeof(mp4_stts_atom), 0);

    trak->stts_data.buffer = TSIOBufferCreate();
    trak->stts_data.reader = TSIOBufferReaderAlloc(trak->stts_data.buffer);
    TSIOBufferCopy(trak->stts_data.buffer, meta_reader, esize, sizeof(mp4_stts_atom));

    mp4_meta_consume(atom_data_size + atom_header_size);

    return 1;
}

int
Mp4Meta::mp4_read_stss_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int32_t         entries;
    int64_t         esize;
    mp4_stss_atom   stss;
    Mp4Trak         *trak;

    if (sizeof(mp4_stss_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &stss, sizeof(mp4_stss_atom));
    entries = mp4_get_32value(stss.entries);
    esize = entries * sizeof(int32_t);

    if (sizeof(mp4_stss_atom) - 8 + esize > (size_t)atom_data_size)
        return -1;

    trak = trak_vec[trak_num - 1];
    trak->sync_samples_entries = entries;

    trak->stss_atom.buffer = TSIOBufferCreate();
    trak->stss_atom.reader = TSIOBufferReaderAlloc(trak->stss_atom.buffer);
    TSIOBufferCopy(trak->stss_atom.buffer, meta_reader, sizeof(mp4_stss_atom), 0);

    trak->stss_data.buffer = TSIOBufferCreate();
    trak->stss_data.reader = TSIOBufferReaderAlloc(trak->stss_data.buffer);
    TSIOBufferCopy(trak->stss_data.buffer, meta_reader, esize, sizeof(mp4_stss_atom));

    mp4_meta_consume(atom_data_size + atom_header_size);

    return 1;
}

int
Mp4Meta::mp4_read_ctts_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int32_t         entries;
    int64_t         esize;
    mp4_ctts_atom   ctts;
    Mp4Trak         *trak;

    if (sizeof(mp4_ctts_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &ctts, sizeof(mp4_ctts_atom));
    entries = mp4_get_32value(ctts.entries);
    esize = entries * sizeof(mp4_ctts_entry);

    if (sizeof(mp4_ctts_atom) - 8 + esize > (size_t)atom_data_size)
        return -1;

    trak = trak_vec[trak_num - 1];
    trak->composition_offset_entries = entries;

    trak->ctts_atom.buffer = TSIOBufferCreate();
    trak->ctts_atom.reader = TSIOBufferReaderAlloc(trak->ctts_atom.buffer);
    TSIOBufferCopy(trak->ctts_atom.buffer, meta_reader, sizeof(mp4_ctts_atom), 0);

    trak->ctts_data.buffer = TSIOBufferCreate();
    trak->ctts_data.reader = TSIOBufferReaderAlloc(trak->ctts_data.buffer);
    TSIOBufferCopy(trak->ctts_data.buffer, meta_reader, esize, sizeof(mp4_ctts_atom));

    mp4_meta_consume(atom_data_size + atom_header_size);

    return 1;
}

int
Mp4Meta::mp4_read_stsc_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int32_t         entries;
    int64_t         esize;
    mp4_stsc_atom   stsc;
    Mp4Trak         *trak;

    if (sizeof(mp4_stsc_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &stsc, sizeof(mp4_stsc_atom));
    entries = mp4_get_32value(stsc.entries);
    esize = entries * sizeof(mp4_stsc_entry);

    if (sizeof(mp4_stsc_atom) - 8 + esize > (size_t)atom_data_size)
        return -1;

    trak = trak_vec[trak_num - 1];
    trak->sample_to_chunk_entries = entries;

    trak->stsc_atom.buffer = TSIOBufferCreate();
    trak->stsc_atom.reader = TSIOBufferReaderAlloc(trak->stsc_atom.buffer);
    TSIOBufferCopy(trak->stsc_atom.buffer, meta_reader, sizeof(mp4_stsc_atom), 0);

    trak->stsc_data.buffer = TSIOBufferCreate();
    trak->stsc_data.reader = TSIOBufferReaderAlloc(trak->stsc_data.buffer);
    TSIOBufferCopy(trak->stsc_data.buffer, meta_reader, esize, sizeof(mp4_stsc_atom));

    mp4_meta_consume(atom_data_size + atom_header_size);

    return 1;
}

int
Mp4Meta::mp4_read_stsz_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int32_t         entries, size;
    int64_t         esize, atom_size;
    mp4_stsz_atom   stsz;
    Mp4Trak         *trak;

    if (sizeof(mp4_stsz_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &stsz, sizeof(mp4_stsz_atom));
    entries = mp4_get_32value(stsz.entries);
    esize = entries * sizeof(int32_t);

    trak = trak_vec[trak_num - 1];
    size = mp4_get_32value(stsz.uniform_size);

    trak->sample_sizes_entries = entries;

    trak->stsz_atom.buffer = TSIOBufferCreate();
    trak->stsz_atom.reader = TSIOBufferReaderAlloc(trak->stsz_atom.buffer);
    TSIOBufferCopy(trak->stsz_atom.buffer, meta_reader, sizeof(mp4_stsz_atom), 0);

    if (size == 0) {
        if (sizeof(mp4_stsz_atom) - 8 + esize > (size_t)atom_data_size)
            return -1;

        trak->stsz_data.buffer = TSIOBufferCreate();
        trak->stsz_data.reader = TSIOBufferReaderAlloc(trak->stsz_data.buffer);
        TSIOBufferCopy(trak->stsz_data.buffer, meta_reader, esize, sizeof(mp4_stsz_atom));
    } else {
        atom_size = atom_header_size + atom_data_size;
        trak->size += atom_size;
        mp4_reader_set_32value(trak->stsz_atom.reader, 0, atom_size);
    }

    mp4_meta_consume(atom_data_size + atom_header_size);

    return 1;
}

int
Mp4Meta::mp4_read_stco_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int32_t         entries;
    int64_t         esize;
    mp4_stco_atom   stco;
    Mp4Trak         *trak;

    if (sizeof(mp4_stco_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &stco, sizeof(mp4_stco_atom));
    entries = mp4_get_32value(stco.entries);
    esize = entries * sizeof(int32_t);

    if (sizeof(mp4_stco_atom) - 8 + esize > (size_t)atom_data_size)
        return -1;

    trak = trak_vec[trak_num - 1];
    trak->chunks = entries;

    trak->stco_atom.buffer = TSIOBufferCreate();
    trak->stco_atom.reader = TSIOBufferReaderAlloc(trak->stco_atom.buffer);
    TSIOBufferCopy(trak->stco_atom.buffer, meta_reader, sizeof(mp4_stco_atom), 0);

    trak->stco_data.buffer = TSIOBufferCreate();
    trak->stco_data.reader = TSIOBufferReaderAlloc(trak->stco_data.buffer);
    TSIOBufferCopy(trak->stco_data.buffer, meta_reader, esize, sizeof(mp4_stco_atom));

    mp4_meta_consume(atom_data_size + atom_header_size);

    return 1;
}

int
Mp4Meta::mp4_read_co64_atom(int64_t atom_header_size, int64_t atom_data_size)
{
    int32_t         entries;
    int64_t         esize;
    mp4_co64_atom   co64;
    Mp4Trak         *trak;

    if (sizeof(mp4_co64_atom) - 8 > (size_t)atom_data_size)
        return -1;

    IOBufferReaderCopy(meta_reader, &co64, sizeof(mp4_co64_atom));
    entries = mp4_get_32value(co64.entries);
    esize = entries * sizeof(int64_t);

    if (sizeof(mp4_co64_atom) - 8 + esize > (size_t)atom_data_size)
        return -1;

    trak = trak_vec[trak_num - 1];
    trak->chunks = entries;

    trak->co64_atom.buffer = TSIOBufferCreate();
    trak->co64_atom.reader = TSIOBufferReaderAlloc(trak->co64_atom.buffer);
    TSIOBufferCopy(trak->co64_atom.buffer, meta_reader, sizeof(mp4_co64_atom), 0);

    trak->co64_data.buffer = TSIOBufferCreate();
    trak->co64_data.reader = TSIOBufferReaderAlloc(trak->co64_data.buffer);
    TSIOBufferCopy(trak->co64_data.buffer, meta_reader, esize, sizeof(mp4_co64_atom));

    mp4_meta_consume(atom_data_size + atom_header_size);

    return 1;
}

int
Mp4Meta::mp4_read_mdat_atom(int64_t /* atom_header_size ATS_UNUSED */, int64_t /* atom_data_size ATS_UNUSED */)
{
    mdat_atom.buffer = TSIOBufferCreate();
    mdat_atom.reader = TSIOBufferReaderAlloc(mdat_atom.buffer);

    meta_complete = true;
    return 1;
}

void
Mp4Meta::mp4_print_sync_value(Mp4Trak *trak)
{
    uint32_t            i, j, sync_sample;
    uint32_t            stss_entries, stts_entries, sample, duration, count;
    uint64_t            sum;
    double              sync_time;
    TSIOBufferReader    stss_p, stts_p;
    
    stss_entries = trak->sync_samples_entries;
    stts_entries = trak->time_to_sample_entries;
    stss_p = TSIOBufferReaderClone(trak->stss_data.reader);

#if DEBUG_LOG
    al_log_debug(" ");
    al_log_debug("--------------------> 关键帧列表信息");
    al_log_debug("序号  时间       sample");
#endif
    for (i = 0; i < stss_entries; i++)
    {
        sync_sample = sample = (uint32_t)mp4_reader_get_32value(stss_p, 0);
        if (sample > 0)
        {
            sample -= 1;
        }
        stts_p = TSIOBufferReaderClone(trak->stts_data.reader);
        sum = 0;

        for (j = 0; j < stts_entries; j++)
        {
            duration = (uint32_t)mp4_reader_get_32value(stts_p,
                    offsetof(mp4_stts_entry, duration));
            count = (uint32_t)mp4_reader_get_32value(stts_p,
                    offsetof(mp4_stts_entry, count));

            if (sample < count)
            {
                sum += (uint64_t)sample * duration;
                sync_time = ((double)sum / trak->duration)
                    * ((double)trak->duration / trak->timescale);
                TSError("%d\t%0.3lf\t  %d", i, sync_time, sync_sample);

                break;
            }
            sample -= count;
            sum += (uint64_t)count * duration;
            TSIOBufferReaderConsume(stts_p, sizeof(mp4_stts_entry));
        }
        TSIOBufferReaderFree(stts_p);
        TSIOBufferReaderConsume(stss_p, sizeof(uint32_t));
    }
    TSIOBufferReaderFree(stss_p);
}

int
Mp4Meta::mp4_get_time_by_offset(Mp4Trak* trak)
{
    uint32_t            i, stss_entries, sample;
    double              key_frame_time, key_frame_offset;
    int64_t             start_time, end_time;
    bool                get_start_time, get_end_time;
    TSIOBufferReader    stss_p;

    key_frame_time = key_frame_offset = 0;
    start_time = end_time = 0;
    get_start_time = get_end_time = false;

    if (this->end <= 0)
        get_end_time = true;

    stss_entries = trak->sync_samples_entries;
    stss_p = TSIOBufferReaderClone(trak->stss_data.reader);
    
    for (i = 0; i < stss_entries; ++i)
    {
        sample = (uint32_t)mp4_reader_get_32value(stss_p, 0);
        if (mp4_get_keyframe_time(trak, sample, &key_frame_time) == 0
                && mp4_get_keyframe_offset(trak, sample, &key_frame_offset) == 0)
        {
            // al_log_debug("%d -- %.3lf -- %.0lf", i, key_frame_time, key_frame_offset);
            if (!get_start_time)
            {
                if (this->start < key_frame_offset)
                {
                    this->start = start_time;
                    get_start_time = true;
                }
                else
                {
                    // 这里加200主要是考虑到精度
                    // 赋值给this->start的已经是计算得出的关键帧时间
                    // 但是这个时间只是保留了小数点后三位
                    // 因此有可能出现对齐到前一个关键帧的情况
                    // 现统一加200毫秒的时间
                    start_time =(int64_t)(key_frame_time * 1000) + 200;
                }
            }
            if (!get_end_time)
            {
                if (this->end < key_frame_offset)
                {
                    this->end = end_time;
                    get_end_time = true;
                }
                else
                {
                    end_time = (int64_t)(key_frame_time * 1000) + 200;
                }
            }

            if (get_start_time && get_end_time)
            {
                break;
            }
        }
        else
            break;
        TSIOBufferReaderConsume(stss_p, sizeof(uint32_t));
    }
    TSIOBufferReaderFree(stss_p);

    return 0;
}

int
Mp4Meta::mp4_get_keyframe_time(Mp4Trak *trak, uint32_t key_frame_sample,
        double *key_frame_time)
{
    uint32_t        stts_entries, sample, duration, count;
    uint32_t        i;
    uint64_t        sum;
    bool            get_keyframe_time;
    TSIOBufferReader    stts_p;//, stsc_p;
    
    *key_frame_time = -1;
    get_keyframe_time = false;

    stts_entries = trak->time_to_sample_entries;

    sample = key_frame_sample;
    if (sample > 0)
        sample -= 1;

    // 获取第key_frame个关键帧的时间
    stts_p = TSIOBufferReaderClone(trak->stts_data.reader);
    sum = 0;
    
    for (i = 0; i < stts_entries; i++)
    {
        duration = (uint32_t)mp4_reader_get_32value(stts_p,
                offsetof(mp4_stts_entry, duration));
        count = (uint32_t)mp4_reader_get_32value(stts_p,
                offsetof(mp4_stts_entry, count));

        if (sample < count)
        {
            sum += (uint64_t)sample * duration;
            *key_frame_time = ((double)sum / trak->duration)
                * ((double)trak->duration / trak->timescale);

            get_keyframe_time = true;

            break;
        }

        sample -= count;
        sum += (uint64_t)count * duration;
        TSIOBufferReaderConsume(stts_p, sizeof(mp4_stts_entry));
    }
    TSIOBufferReaderFree(stts_p);

    //stsc_p = TSIOBufferReaderClone(trak->stsc_data.reader);

    if (get_keyframe_time)
        return 0;

    return -1;
}

int
Mp4Meta::mp4_get_keyframe_offset(Mp4Trak *trak, uint32_t key_frame_sample,
        double *key_frame_offset)
{
    uint32_t            chunk, samples, next_chunk;
    uint32_t            i, start_chunk, sample, chunk_samples, n;
    uint64_t            start_offset;
    uint64_t            chunk_samples_size;
    bool                get_keyframe_offset;
    TSIOBufferReader    stsc_p, stsz_p, co_p;

    *key_frame_offset = 0;
    get_keyframe_offset = false;

    if (NULL == trak->stsc_data.buffer)
    {
        al_log_debug("--- %s --- %d ---", __FUNCTION__, __LINE__);
        return -1;
    }

    if (0 == trak->sample_to_chunk_entries)
    {
        al_log_debug("--- %s --- %d ---", __FUNCTION__, __LINE__);
        return -1;
    }

    // 获取第key_frame个关键帧的偏移量
    stsc_p = TSIOBufferReaderClone(trak->stsc_data.reader);
    sample = key_frame_sample;

    chunk = mp4_reader_get_32value(stsc_p, offsetof(mp4_stsc_entry, chunk));
    samples = mp4_reader_get_32value(stsc_p, offsetof(mp4_stsc_entry, samples));

    TSIOBufferReaderConsume(stsc_p, sizeof(mp4_stsc_entry));

    for (i = 1; i < trak->sample_to_chunk_entries; ++i)
    {
        next_chunk = mp4_reader_get_32value(stsc_p,
                offsetof(mp4_stsc_entry, chunk));
        n = (next_chunk - chunk) * samples;

        if (sample <= n)
        {
            goto found;
        }
        sample -= n;

        chunk = next_chunk;
        samples = mp4_reader_get_32value(stsc_p,
                offsetof(mp4_stsc_entry, samples));

        TSIOBufferReaderConsume(stsc_p, sizeof(mp4_stsc_entry));
    }

    next_chunk = trak->chunks;
    n = (next_chunk - chunk) * samples;
    if (sample > n)
    {
        TSIOBufferReaderFree(stsc_p);
        al_log_debug("--------- %s ------ %d ------------", __FUNCTION__, __LINE__);
        return -1;
    }

found:

    TSIOBufferReaderFree(stsc_p);
    start_chunk = chunk - 1;
    start_chunk += sample / samples;
    chunk_samples = sample % samples;

    if (start_chunk > trak->chunks)
    {
        al_log_debug("--------- %s ------ %d ------------", __FUNCTION__, __LINE__);
        return -1;
    }

    stsz_p = TSIOBufferReaderClone(trak->stsz_data.reader);

    TSIOBufferReaderConsume(stsz_p, sizeof(uint32_t) * (key_frame_sample - chunk_samples));
    chunk_samples_size = 0;
    for (i = 0; i < chunk_samples; ++i)
    {
        chunk_samples_size += mp4_reader_get_32value(stsz_p, 0);
        TSIOBufferReaderConsume(stsz_p, sizeof(uint32_t));
    }
    TSIOBufferReaderFree(stsz_p);

    if (trak->co64_data.buffer)
    {
        co_p = TSIOBufferReaderClone(trak->co64_data.reader);
        TSIOBufferReaderConsume(co_p, start_chunk * sizeof(uint64_t));
        start_offset = mp4_reader_get_64value(co_p, 0);
        start_offset += chunk_samples_size;
        *key_frame_offset = start_offset;
        get_keyframe_offset = true;
        TSIOBufferReaderFree(co_p);
    }
    else if (trak->stco_data.buffer)
    {
        co_p = TSIOBufferReaderClone(trak->stco_data.reader);
        TSIOBufferReaderConsume(co_p, start_chunk * sizeof(uint32_t));
        start_offset = mp4_reader_get_32value(co_p, 0);
        start_offset += chunk_samples_size;
        *key_frame_offset = start_offset;
        get_keyframe_offset = true;
        TSIOBufferReaderFree(co_p);
    }

    if (get_keyframe_offset)
        return 0;

    return -1;
}

int
Mp4Meta::mp4_update_stts_atom(Mp4Trak *trak)
{
    uint32_t            i, entries, count, duration, pass;
    uint32_t            start_sample, left;//start_count;
    uint32_t            key_sample;//, old_sample;
    uint64_t            start_time, sum;
    int64_t             atom_size;
    TSIOBufferReader    readerp;

    if (trak->stts_data.buffer == NULL) {
        al_log_debug("trak->stts atom not found");
        return -1;
    }

    sum = 0;

    entries = trak->time_to_sample_entries;
    start_time = this->start * trak->timescale / 1000;

    start_time = this->start * trak->timescale / 1000;
    if (this->rs > 0) {
        start_time = (uint64_t)(this->rs * trak->timescale / 1000);
    }

#if DEBUG_LOG
    al_log_debug("-> update stts, entries=%d, start_time=%zd, rs=%lf", entries, start_time, this->rs);
#endif

    start_sample = 0;
    readerp = TSIOBufferReaderClone(trak->stts_data.reader);

    for (i = 0; i < entries; i++) {
        duration = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, duration));
        count = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, count));

        if (start_time < (uint64_t)count * duration) {
#if DEBUG_LOG
            al_log_debug("---> update stts, start found, get duration=%d, count=%d", duration, count);
#endif
            pass = (uint32_t)(start_time/duration);
            start_sample += pass;
            count -= pass;
            goto found;
        }

        start_sample += count;
        start_time -= (uint64_t)count * duration;
        TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
    }

found:

    TSIOBufferReaderFree(readerp);

    key_sample = this->mp4_find_key_sample(start_sample, trak);      // find the last key frame before start_sample

    start_sample = key_sample;

#if DEBUG_LOG
    al_log_debug("--> update stts, get start key sample=%d", start_sample);
#endif

    readerp = TSIOBufferReaderClone(trak->stts_data.reader);

    trak->start_sample = start_sample;

    for (i = 0; i < entries; i++) {
        duration = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, duration));
        count = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, count));

        if (start_sample < count) {
            count -= start_sample;
            mp4_reader_set_32value(readerp, offsetof(mp4_stts_entry, count), count);
            //al_log_debug("----> update stts, found, set count=%d", count);

            sum += (uint64_t)start_sample * duration;
            break;
        }

        start_sample -= count;
        sum += (uint64_t)count * duration;

        TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
    }

    if (this->rs == 0) {
        this->rs = ((double)sum/trak->duration) * ((double)trak->duration/trak->timescale) * 1000;
        // 对齐到关键帧需要重新指定开始时间
        // 也需要重新指定长度
        // change by Dnion
        if (this->start > (int64_t)this->rs)
        {
            if (this->length > 0)
            {
                this->length += this->start - (uint64_t)this->rs;
            }
            this->start = (uint64_t)this->rs;
        }
#if DEBUG_LOG
        al_log_debug("---> update stts, set rs=%0.3lf, start=%zd, length=%zd",
                this->rs, this->start, this->length);
#endif
    }

    left = entries - i;

    atom_size = sizeof(mp4_stts_atom) + left * sizeof(mp4_stts_entry);
    trak->size += atom_size;

    mp4_reader_set_32value(trak->stts_atom.reader, offsetof(mp4_stts_atom, size), atom_size);
    mp4_reader_set_32value(trak->stts_atom.reader, offsetof(mp4_stts_atom, entries), left);

    TSIOBufferReaderConsume(trak->stts_data.reader, i * sizeof(mp4_stts_entry));
    TSIOBufferReaderFree(readerp);

    if (this->length)
        return mp4_crop_stts_atom(trak);

    return 0;
}

int
Mp4Meta::mp4_crop_stts_atom(Mp4Trak *trak)
{
    uint32_t        i, entries, end_sample, count, duration, pass;
    uint32_t        key_sample, old_sample;
    uint64_t        avail, length_time, sum;
    int64_t         atom_size;
    TSIOBufferReader    readerp;
    
    avail = TSIOBufferReaderAvail(trak->stts_data.reader);
    if (avail <= 0){
        al_log_debug("trak->stts_data.reader no data ");
        return -1;
    }

    sum = 0;

    entries = mp4_reader_get_32value(trak->stts_atom.reader, offsetof(mp4_stts_atom, entries));
    atom_size = mp4_reader_get_32value(trak->stts_atom.reader, offsetof(mp4_stts_atom, size));
    length_time = this->length * trak->timescale / 1000;
#if DEBUG_LOG
    al_log_debug(" ");
    al_log_debug("----------------> crop stts");
    al_log_debug("-> crop stts, entries=%d, length_time=%zd", entries, length_time);
#endif

    end_sample = 0;
    readerp = TSIOBufferReaderClone(trak->stts_data.reader);

    for (i = 0; i < entries; i++) {
        duration = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, duration));
        count = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, count));

        if (length_time < (uint64_t)count * duration) {
            //al_log_debug("---> crop stts, end found, get duration=%d, count=%d", duration, count);
            pass = (uint32_t)(length_time / duration);
            end_sample += pass;
            count -= pass;
            goto found;
        }

        end_sample += count;
        length_time -= (uint64_t)count * duration;
        TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
    }

    //TSIOBufferReaderFree(readerp);
    trak->end_sample = trak->start_sample + end_sample;
    //return 0;

found:

    TSIOBufferReaderFree(readerp);

    //end_sample += trak->start_sample;
    old_sample = end_sample;
    key_sample = this->mp4_find_key_sample(end_sample + trak->start_sample, trak);

    if (old_sample != key_sample - trak->start_sample)
    {
        end_sample = key_sample - trak->start_sample;
    }

#if DEBUG_LOG
    al_log_debug("--> crop stts, get end key sample=%d, old sample=%d", key_sample - 1, old_sample);
#endif

    if(end_sample == 0) {
#if DEBUG_LOG
       al_log_debug("--> start end 在同一个关键帧内");
#endif  
       this->length = 0 ;
       return 0 ;
    }


    readerp = TSIOBufferReaderClone(trak->stts_data.reader);

    trak->end_sample = trak->start_sample + end_sample;
#if DEBUG_LOG
    al_log_debug("---> crop stts, start_sample=%d, end_sample=%d",
            trak->start_sample, trak->end_sample);
#endif

    for (i = 0; i < entries; i++) {
        duration = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, duration));
        count = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_stts_entry, count));

        if (end_sample < count) {
            mp4_reader_set_32value(readerp, offsetof(mp4_stts_entry, count), end_sample);
            //al_log_debug("---> crop stts, get end pos, entry=%d, count=%d", i, end_sample);

            sum += (uint64_t)end_sample * duration;
            break;
        }

        end_sample -= count;
        sum += (uint64_t)count * duration;

        TSIOBufferReaderConsume(readerp, sizeof(mp4_stts_entry));
    }

    if (this->ls == 0)
    {
        this->ls = ((double)sum / trak->duration)
            * ((double)trak->duration / trak->timescale) * 1000;
#if DEBUG_LOG
        al_log_debug("---> reset length, ls=%lf", this->ls);
#endif
    }

    trak->size -= atom_size;
    atom_size = sizeof(mp4_stts_atom) + (i + 1) * sizeof(mp4_stts_entry);
    //trak->size -= (entries - i - 1) * sizeof(mp4_stts_entry);
    trak->size += atom_size;

    mp4_reader_set_32value(trak->stts_atom.reader, offsetof(mp4_stts_atom, size), atom_size);
    mp4_reader_set_32value(trak->stts_atom.reader, offsetof(mp4_stts_atom, entries), i + 1);

    TSIOBufferReaderFree(readerp);

    avail = TSIOBufferReaderAvail(trak->stts_data.reader);
    if (avail < sizeof(mp4_stts_entry) * (i + 1)) {
        al_log_debug("---> crop stts, something error...");
        return -1;
    }

    TSIOBufferCopy(trak->stts_data.buffer, trak->stts_data.reader,
            sizeof(mp4_stts_entry) * (i + 1), 0);

    TSIOBufferReaderConsume(trak->stts_data.reader, avail);
#if DEBUG_LOG
    al_log_debug("--> crop stts, reader avail=%zd, data size=%zd",
            TSIOBufferReaderAvail(trak->stts_data.reader),
            (i + 1) * sizeof(mp4_stts_entry));
#endif

    return 0;
}

int
Mp4Meta::mp4_update_stss_atom(Mp4Trak *trak)
{
    int64_t             atom_size;
    uint32_t            i, j, entries, sample, start_sample, left;
    TSIOBufferReader    readerp;

    if (trak->stss_data.buffer == NULL) {
#if DEBUG_LOG
        al_log_debug("trak->stss_data not found ");
#endif
        return 0;
    }

    readerp = TSIOBufferReaderClone(trak->stss_data.reader);

    start_sample = trak->start_sample + 1;
    entries = trak->sync_samples_entries;
    
#if DEBUG_LOG
    al_log_debug("-> update stss, start_sample=%d, entries=%d", start_sample, entries);
#endif

    for (i = 0; i < entries ; i++) {
        sample = (uint32_t)mp4_reader_get_32value(readerp, 0);

        if (sample >= start_sample) {
            goto found;
        }

        TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
    }

    TSIOBufferReaderFree(readerp);
    return -1;

found:

    left = entries - i;

    start_sample = trak->start_sample;
    for (j = 0; j < left; j++) {
        sample = (uint32_t)mp4_reader_get_32value(readerp, 0);
        sample -= start_sample;
        mp4_reader_set_32value(readerp, 0, sample);
        TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
    }

    atom_size = sizeof(mp4_stss_atom) + left * sizeof(uint32_t);
    trak->size += atom_size;

    mp4_reader_set_32value(trak->stss_atom.reader, offsetof(mp4_stss_atom, size),
                           atom_size);

    mp4_reader_set_32value(trak->stss_atom.reader, offsetof(mp4_stss_atom, entries),
                           left);

    TSIOBufferReaderConsume(trak->stss_data.reader, i * sizeof(uint32_t));
    TSIOBufferReaderFree(readerp);

    if (this->length)
        return mp4_crop_stss_atom(trak);

    return 0;
}

int
Mp4Meta::mp4_crop_stss_atom(Mp4Trak *trak)
{
    int64_t         atom_size, avail;
    uint32_t        i, entries, sample, end_sample;
    TSIOBufferReader    readerp;

    readerp = TSIOBufferReaderClone(trak->stss_data.reader);

    end_sample = trak->end_sample - trak->start_sample + 1;
    entries = mp4_reader_get_32value(trak->stss_atom.reader, offsetof(mp4_stss_atom, entries));
    atom_size = mp4_reader_get_32value(trak->stss_atom.reader, offsetof(mp4_stss_atom, size));

#if DEBUG_LOG
    al_log_debug(" ");
    al_log_debug("----------------> crop stss");
    al_log_debug("-> crop stss, need sample=%d, entries=%d", end_sample, entries);
#endif

    for (i = 0; i < entries; i++) {
        sample = (uint32_t)mp4_reader_get_32value(readerp, 0);

        if (sample >= end_sample) {
            //al_log_debug("--->crop stss, end found, sample=%d", sample);
            goto found;
        }

        TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
    }

    TSIOBufferReaderFree(readerp);
    return 0;

found:

    end_sample = trak->end_sample;
    trak->size -= atom_size;
    atom_size = sizeof(mp4_stss_atom) + i * sizeof(uint32_t);
    //trak->size -= (entries - i) * sizeof(uint32_t);
    trak->size += atom_size;
    //al_log_debug("--> crop stss, atom_size=%zd, entries=%d", atom_size, i);

    mp4_reader_set_32value(trak->stss_atom.reader, offsetof(mp4_stss_atom, size), atom_size);
    mp4_reader_set_32value(trak->stss_atom.reader, offsetof(mp4_stss_atom, entries), i);

    TSIOBufferReaderFree(readerp);
    avail = TSIOBufferReaderAvail(trak->stss_data.reader);

    TSIOBufferCopy(trak->stss_data.buffer, trak->stss_data.reader, i* sizeof(uint32_t), 0);
    TSIOBufferReaderConsume(trak->stss_data.reader, avail);
#if DEBUG_LOG
    al_log_debug("--> crop stss, reader avail=%zd, data size=%zd",
            TSIOBufferReaderAvail(trak->stss_data.reader),
            i * sizeof(uint32_t));
#endif

    return 0;
}

int
Mp4Meta::mp4_update_ctts_atom(Mp4Trak *trak)
{
    int64_t             atom_size;
    uint32_t            i, entries, start_sample, left;
    uint32_t            count;
    TSIOBufferReader    readerp;

    if (trak->ctts_data.buffer == NULL) {
#if DEBUG_LOG
        al_log_debug("ctts_data.buffer NULL");
#endif
        return 0;
    }

    readerp = TSIOBufferReaderClone(trak->ctts_data.reader);

    start_sample = trak->start_sample + 1;
    entries = trak->composition_offset_entries;

    for (i = 0; i < entries; i++) {
        count = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_ctts_entry, count));

        if (start_sample <= count) {
            count -= (start_sample - 1);
            mp4_reader_set_32value(readerp, offsetof(mp4_ctts_entry, count), count);
            goto found;
        }

        start_sample -= count;
        TSIOBufferReaderConsume(readerp, sizeof(mp4_ctts_entry));
    }

    if (trak->ctts_atom.reader) {
        TSIOBufferReaderFree(trak->ctts_atom.reader);
        TSIOBufferDestroy(trak->ctts_atom.buffer);

        trak->ctts_atom.buffer = NULL;
        trak->ctts_atom.reader = NULL;
    }

    if (trak->ctts_data.reader) {
        TSIOBufferReaderFree(trak->ctts_data.reader);
        TSIOBufferDestroy(trak->ctts_data.buffer);

        trak->ctts_data.reader = NULL;
        trak->ctts_data.buffer = NULL;
    }

    TSIOBufferReaderFree(readerp);
    return 0;

found:

    left = entries - i;
    atom_size = sizeof(mp4_ctts_atom) + left * sizeof(mp4_ctts_entry);
    trak->size += atom_size;

    mp4_reader_set_32value(trak->ctts_atom.reader, offsetof(mp4_ctts_atom, size), atom_size);
    mp4_reader_set_32value(trak->ctts_atom.reader, offsetof(mp4_ctts_atom, entries), left);

    TSIOBufferReaderConsume(trak->ctts_data.reader, i * sizeof(mp4_ctts_entry));
    TSIOBufferReaderFree(readerp);

    if (this->length)
        return mp4_crop_ctts_atom(trak);

    return 0;
}

int
Mp4Meta::mp4_crop_ctts_atom(Mp4Trak *trak)
{
    int64_t         atom_size, avail;
    uint32_t        i, entries, end_sample, rest;
    uint32_t        count;
    TSIOBufferReader    readerp;

    readerp = TSIOBufferReaderClone(trak->ctts_data.reader);
    
    end_sample = trak->end_sample - trak->start_sample + 1;
    entries = mp4_reader_get_32value(trak->ctts_atom.reader, offsetof(mp4_ctts_atom, entries));
    atom_size = mp4_reader_get_32value(trak->ctts_atom.reader, offsetof(mp4_ctts_atom, size));

    for (i = 0; i < entries; i++) {
        count = (uint32_t)mp4_reader_get_32value(readerp, offsetof(mp4_ctts_entry, count));

        if (end_sample <= count) {
            rest = end_sample - 1;
            mp4_reader_set_32value(readerp, offsetof(mp4_ctts_entry, count), rest);
            goto found;
        }

        end_sample -= count;
        TSIOBufferReaderConsume(readerp, sizeof(mp4_ctts_entry));
    }

    TSIOBufferReaderFree(readerp);
    return 0;

found:

    TSIOBufferReaderFree(readerp);

    trak->size -= atom_size;
    atom_size = sizeof(mp4_ctts_atom) + (i + 1) * sizeof(mp4_ctts_entry);
    //trak->size -= (entries - i - 1) * sizeof(mp4_ctts_entry);
    trak->size += atom_size;
    //al_log_debug("--> crop ctts, atom_size=%zd, entries=%d", atom_size, i + 1);

    mp4_reader_set_32value(trak->ctts_atom.reader, offsetof(mp4_ctts_atom, size), atom_size);
    mp4_reader_set_32value(trak->ctts_atom.reader, offsetof(mp4_ctts_atom, entries), i + 1);

    avail = TSIOBufferReaderAvail(trak->ctts_data.reader);
    TSIOBufferCopy(trak->ctts_data.buffer, trak->ctts_data.reader,
            (i + 1) * sizeof(mp4_ctts_entry), 0);
    TSIOBufferReaderConsume(trak->ctts_data.reader, avail);
#if DEBUG_LOG
    al_log_debug("--> crop ctts, reader avail=%zd, data size=%zd",
            TSIOBufferReaderAvail(trak->ctts_data.reader),
            (i + 1) * sizeof(mp4_ctts_entry));
#endif

    return 0;
}

int
Mp4Meta::mp4_update_stsc_atom(Mp4Trak *trak)
{
    int64_t             atom_size, avail;
    uint32_t            i, entries, samples, start_sample;
    uint32_t            chunk, next_chunk, n, id, j;
    mp4_stsc_entry      *first;
    TSIOBufferReader    readerp;

    if (trak->stsc_data.buffer == NULL) {
        al_log_debug("trak->stsc_data.buffer NULL");
        return -1;
    }

    if (trak->sample_to_chunk_entries == 0) {
        al_log_debug("stsc trak->sample_to_chunk_entries = 0");
        return -1;
    }

    start_sample = (uint32_t) trak->start_sample;
    //entries = trak->sample_to_chunk_entries - 1;

    readerp = TSIOBufferReaderClone(trak->stsc_data.reader);

    chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
    samples = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, samples));
    id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));

#if DEBUG_LOG
    al_log_debug("-> chunk=%d, samples=%d, id=%d ", chunk, samples, id );
#endif

    TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));

    for (i = 1; i < trak->sample_to_chunk_entries; i++) {
        next_chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
        n = (next_chunk - chunk) * samples;

        if (start_sample <= n) {
            goto found;
        }

        start_sample -= n;

        chunk = next_chunk;
        samples = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, samples));
        id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));

        TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
    }

    next_chunk = trak->chunks;
    n = (next_chunk - chunk) * samples;
    if (start_sample > n) {
        TSIOBufferReaderFree(readerp);
        return -1;
    }

found:

    TSIOBufferReaderFree(readerp);

    entries = trak->sample_to_chunk_entries - i + 1;
    if (samples == 0)
        return -1;

    readerp = TSIOBufferReaderClone(trak->stsc_data.reader);
    TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry) * (i-1));

    trak->start_chunk = chunk - 1;
    trak->start_chunk += start_sample / samples;
    trak->chunk_samples = start_sample % samples;

    atom_size = sizeof(mp4_stsc_atom) + entries * sizeof(mp4_stsc_entry);

    mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), 1);

    if (trak->chunk_samples && next_chunk - trak->start_chunk == 2) {
        mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, samples),
                               samples - trak->chunk_samples);

    } else if (trak->chunk_samples) {
        first = &trak->stsc_chunk_entry;
        mp4_set_32value(first->chunk, 1);
        mp4_set_32value(first->samples, samples - trak->chunk_samples);
        mp4_set_32value(first->id, id);

        trak->stsc_chunk.buffer = TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_128);
        trak->stsc_chunk.reader = TSIOBufferReaderAlloc(trak->stsc_chunk.buffer);
        TSIOBufferWrite(trak->stsc_chunk.buffer, first, sizeof(mp4_stsc_entry));

        mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), 2);

        entries++;
        atom_size += sizeof(mp4_stsc_entry);
    }

    TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));

    for (j = i; j < trak->sample_to_chunk_entries; j++) {
        chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
        chunk -= trak->start_chunk;
        mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, chunk), chunk);
        TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
    }

    trak->size += atom_size;

#if DEBUG_LOG
    al_log_debug("-> start_chunk=%d, start_chunk_samples=%d",
            trak->start_chunk, trak->chunk_samples);
#endif

    mp4_reader_set_32value(trak->stsc_atom.reader, offsetof(mp4_stsc_atom, size), atom_size);
    mp4_reader_set_32value(trak->stsc_atom.reader, offsetof(mp4_stsc_atom, entries), entries);
    trak->sample_to_chunk_entries = entries;

    TSIOBufferReaderConsume(trak->stsc_data.reader, (i-1) * sizeof(mp4_stsc_entry));
    TSIOBufferReaderFree(readerp);

    // copy stsc_chunk if exist
    if (trak->stsc_chunk.buffer != NULL) {
        avail = TSIOBufferReaderAvail(trak->stsc_data.reader);
        TSIOBufferCopy(trak->stsc_data.buffer, trak->stsc_chunk.reader,
                TSIOBufferReaderAvail(trak->stsc_chunk.reader), 0);
        TSIOBufferCopy(trak->stsc_data.buffer, trak->stsc_data.reader, avail, 0);
        TSIOBufferReaderConsume(trak->stsc_data.reader, avail);
        // destroy stsc_chunk
    }

    if (this->length)
        return mp4_crop_stsc_atom(trak);

    return 0;
}

int
Mp4Meta::mp4_crop_stsc_atom(Mp4Trak *trak)
{
    int64_t             atom_size, avail;
    uint32_t            i, entries, samples, end_sample, prev_samples;
    uint32_t            chunk, id, next_chunk, n, target_chunk, chunk_samples;
    mp4_stsc_entry      *first;
    TSIOBufferReader    readerp;

    end_sample = (uint32_t)(trak->end_sample - trak->start_sample);
    entries = mp4_reader_get_32value(trak->stsc_atom.reader, offsetof(mp4_stsc_atom, entries));
    atom_size = mp4_reader_get_32value(trak->stsc_atom.reader, offsetof(mp4_stsc_atom, size));
#if DEBUG_LOG
    al_log_debug(" ");
    al_log_debug("-----------> crop stsc");
    al_log_debug("-> end_sample=%d, entries=%d", end_sample, entries);
#endif

    readerp = TSIOBufferReaderClone(trak->stsc_data.reader);

    chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));
    samples = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, samples));
    id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));
#if DEBUG_LOG
    al_log_debug("-> chunk=%d, samples=%d, id=%d", chunk, samples, id);
#endif
    prev_samples = 0;


    // first chunk
    /*
    if (samples > end_sample)
    {
        samples = end_sample;
        mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, samples), samples);
    }
    end_sample -= samples;*/

    TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));

    for (i = 1; i < entries; i++) {
        next_chunk = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, chunk));

        //al_log_debug("------> crop stsc, next_chunk=%d, samples=%d", next_chunk, samples);
        n = (next_chunk - chunk) * samples;
        if (end_sample <= n) {
#if DEBUG_LOG
            al_log_debug("---> crop stsc, end found, i=%d, end_sample=%d, chunk=%d, n=%d"
                    ", next_chunk=%d",
                    i, end_sample, chunk, n, next_chunk);
#endif
            goto found;
        }

        end_sample -= n;

        prev_samples = samples;
        chunk = next_chunk;
        samples = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, samples));
        id = mp4_reader_get_32value(readerp, offsetof(mp4_stsc_entry, id));

        TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry));
    }

    next_chunk = trak->chunks + 1;

    n = (next_chunk - chunk) * samples;
    if (end_sample > n) {
        TSIOBufferReaderFree(readerp);
        al_log_debug("--> end time is out mp4 stsc chunks, a bad mp4 file");
        return -1;
    }

found:

    TSIOBufferReaderFree(readerp);

    if (samples == 0)
        return -1;

    readerp = TSIOBufferReaderClone(trak->stsc_data.reader);
    TSIOBufferReaderConsume(readerp, sizeof(mp4_stsc_entry) * (i - 1));

    target_chunk = chunk - 1;
    target_chunk += end_sample / samples;
    chunk_samples = end_sample % samples;

#if DEBUG_LOG
    al_log_debug("--> crop stsc, trak->chunks=%d", trak->chunks);
    al_log_debug("---> crop stsc, target_chunk=%d, chunk_samples=%d", target_chunk, chunk_samples);
#endif

    trak->size -= atom_size;
    atom_size = sizeof(mp4_stsc_atom) + i * sizeof(mp4_stsc_entry);
    entries = i;

    if (end_sample) {
        trak->end_chunk_samples = samples;
    } else {
        trak->end_chunk_samples = prev_samples;
    }

    if (chunk_samples) {
        trak->end_chunk = target_chunk + 1;
        trak->end_chunk_samples = chunk_samples;
    } else {
        trak->end_chunk = target_chunk;
    }

    samples = chunk_samples;
    next_chunk = chunk + 1;

    if (chunk_samples && next_chunk - target_chunk == 2) {
        mp4_reader_set_32value(readerp, offsetof(mp4_stsc_entry, samples),
                samples - trak->chunk_samples);
    } else if (chunk_samples) {
        first = &trak->stsc_end_chunk_entry;
        //mp4_set_32value(first->chunk, trak->end_chunk - trak->start_chunk);
        mp4_set_32value(first->chunk, trak->end_chunk);
        mp4_set_32value(first->samples, samples);
        mp4_set_32value(first->id, id);

        trak->stsc_end_chunk.buffer = TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_128);
        trak->stsc_end_chunk.reader = TSIOBufferReaderAlloc(trak->stsc_end_chunk.buffer);
        TSIOBufferWrite(trak->stsc_end_chunk.buffer, first, sizeof(mp4_stsc_entry));
        avail = TSIOBufferReaderAvail(trak->stsc_end_chunk.reader);
#if DEBUG_LOG
        al_log_debug("--> crop stsc, new end chunk -- chunk=%d, samples=%d, id=%d, avail=%zd",
                mp4_get_32value(first->chunk),
                samples,
                id,
                avail);
#endif
        
        entries++;
        atom_size += sizeof(mp4_stsc_entry);
    }

    trak->size += atom_size;
    mp4_reader_set_32value(trak->stsc_atom.reader, offsetof(mp4_stsc_atom, size), atom_size);
    mp4_reader_set_32value(trak->stsc_atom.reader, offsetof(mp4_stsc_atom, entries), entries);
#if DEBUG_LOG
    al_log_debug("-> end_chunk=%d, end_chunk_samples=%d, chunk_samples=%d, need copy=%d"
            ", atom_size=%zd, entries=%d",
            trak->end_chunk, trak->end_chunk_samples, chunk_samples, i,
            atom_size, entries);
#endif

    avail = TSIOBufferReaderAvail(trak->stsc_data.reader);
    TSIOBufferCopy(trak->stsc_data.buffer, trak->stsc_data.reader,
            sizeof(mp4_stsc_entry) * i, 0);
    TSIOBufferReaderConsume(trak->stsc_data.reader, avail);

    return 0;
}

int
Mp4Meta::mp4_update_stsz_atom(Mp4Trak *trak)
{
    uint32_t            i;
    int64_t             atom_size, avail;
    uint32_t            pass, invalid, entries;
    TSIOBufferReader    readerp;

    if (trak->stsz_data.buffer == NULL) {
        al_log_debug("trak->stsz_data.buffer NULL");
        return 0;
    }

    if (trak->start_sample > trak->sample_sizes_entries) {
        al_log_debug("trak->start_sample > trak->sample_sizes_entries");
        return -1;
    }

    readerp = TSIOBufferReaderClone(trak->stsz_data.reader);
    avail = TSIOBufferReaderAvail(readerp);

    pass = (trak->start_sample) * sizeof(uint32_t);
    entries = trak->sample_sizes_entries - trak->start_sample;
    invalid = pass;

    TSIOBufferReaderConsume(readerp, pass - sizeof(uint32_t)*(trak->chunk_samples));

    for (i = 0; i < trak->chunk_samples; i++) {
        trak->chunk_samples_size += mp4_reader_get_32value(readerp, 0);
        TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
    }

#if DEBUG_LOG
    al_log_debug("-> trak->chunk_samples=%d, chunk_samples_size=%zd",
            trak->chunk_samples, trak->chunk_samples_size);
#endif

    // stsz set end
    if (this->length) {
        if (trak->end_sample > trak->sample_sizes_entries) {
            return -1;
        }

        invalid += (trak->sample_sizes_entries - trak->end_sample) * sizeof(uint32_t);

        entries = trak->end_sample - trak->start_sample;
        TSIOBufferReaderConsume(readerp, sizeof(uint32_t) * (entries - trak->end_chunk_samples));
        for (i = 0; i < trak->end_chunk_samples; i++) {
            trak->end_chunk_samples_size += mp4_reader_get_32value(readerp, 0);
            TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
        }
#if DEBUG_LOG
        al_log_debug("--> update stsz, end_chunk_samples=%d, end_chunk_samples_size=%zd",
                trak->end_chunk_samples, trak->end_chunk_samples_size);
#endif
    }

    atom_size = sizeof(mp4_stsz_atom) + avail - invalid;
    trak->size += atom_size;

    mp4_reader_set_32value(trak->stsz_atom.reader, offsetof(mp4_stsz_atom, size), atom_size);
    mp4_reader_set_32value(trak->stsz_atom.reader, offsetof(mp4_stsz_atom, entries), entries);

    if (this->length) {
        TSIOBufferReaderConsume(trak->stsz_data.reader, pass);
        avail = TSIOBufferReaderAvail(trak->stsz_data.reader);
        TSIOBufferCopy(trak->stsz_data.buffer, trak->stsz_data.reader,
                entries * sizeof(uint32_t), 0);
        TSIOBufferReaderConsume(trak->stsz_data.reader, avail);
    } else {
        TSIOBufferReaderConsume(trak->stsz_data.reader, pass);
    }
    TSIOBufferReaderFree(readerp);

    return 0;
}

int
Mp4Meta::mp4_update_co64_atom(Mp4Trak *trak)
{
    int64_t             atom_size, avail, pass;
    uint32_t            entries;
    TSIOBufferReader    readerp;

    if (trak->co64_data.buffer == NULL) {
        al_log_debug("trak->co64_data.buffer NULL");
        return -1;
    }

    if (trak->start_chunk > trak->chunks) {
        al_log_debug("trak->start_chunk > trak->chunks");
        return -1;
    }

    readerp = trak->co64_data.reader;
    avail = TSIOBufferReaderAvail(readerp);

#if DEBUG_LOG
    al_log_debug("------------> update co64");
#endif

    pass = trak->start_chunk * sizeof(uint64_t);
    entries = trak->chunks - trak->start_chunk;
    atom_size = sizeof(mp4_co64_atom) + avail - pass;
    //trak->size += atom_size;

    TSIOBufferReaderConsume(readerp, pass);
    trak->start_offset = mp4_reader_get_64value(readerp, 0);
    trak->start_offset += trak->chunk_samples_size;
    mp4_reader_set_64value(readerp, 0, trak->start_offset);

    if (this->length) {
#if DEBUG_LOG
        al_log_debug("-> update co64, end_chunk=%d", trak->end_chunk);
#endif
        readerp = TSIOBufferReaderClone(trak->co64_data.reader);
        entries = trak->end_chunk;
        pass = (entries - 1) * sizeof(uint64_t);
        TSIOBufferReaderConsume(readerp, pass);

        trak->end_offset = mp4_reader_get_64value(readerp, 0);
        trak->end_offset += trak->end_chunk_samples_size;
        mp4_reader_set_64value(readerp, 0, trak->end_offset);
        TSIOBufferReaderFree(readerp);
        atom_size = sizeof(mp4_co64_atom) + pass + sizeof(uint64_t);

        avail = TSIOBufferReaderAvail(trak->co64_data.reader);
        TSIOBufferCopy(trak->co64_data.buffer, trak->co64_data.reader,
                pass + sizeof(uint64_t), 0);
        TSIOBufferReaderConsume(trak->co64_data.reader, avail);
    }

    trak->size += atom_size;


    mp4_reader_set_32value(trak->co64_atom.reader, offsetof(mp4_co64_atom, size), atom_size);
    mp4_reader_set_32value(trak->co64_atom.reader, offsetof(mp4_co64_atom, entries), entries);

    return 0;
}

int
Mp4Meta::mp4_update_stco_atom(Mp4Trak *trak)
{
    int64_t             atom_size, avail;
    uint32_t            pass, entries;
    TSIOBufferReader    readerp;

    if (trak->stco_data.buffer == NULL) {
        al_log_debug("trak->stco_data.buffer NULL");
        return -1;
    }

    if (trak->start_chunk > trak->chunks) {
        al_log_debug("trak->start_chunk > trak->chunks");
        return -1;
    }

    readerp = trak->stco_data.reader;
    avail = TSIOBufferReaderAvail(readerp);

    pass = trak->start_chunk * sizeof(uint32_t);
    entries = trak->chunks - trak->start_chunk;
    atom_size = sizeof(mp4_stco_atom) + avail - pass;

    TSIOBufferReaderConsume(readerp, pass);

#if DEBUG_LOG
    al_log_debug("-----------> update stco");
    al_log_debug("start_chunk=%d, end_chunk=%d", trak->start_chunk, trak->end_chunk);
#endif

    trak->start_offset = mp4_reader_get_32value(readerp, 0);
    trak->start_offset += trak->chunk_samples_size;
#if DEBUG_LOG
    al_log_debug("--> update stco, start_offset=%zd, chunk size=%zd",
            trak->start_offset, trak->chunk_samples_size);
#endif
    mp4_reader_set_32value(readerp, 0, trak->start_offset);

    if (this->length) {
        readerp = TSIOBufferReaderClone(trak->stco_data.reader);
        //entries = trak->end_chunk + trak->end_chunk_samples;
        entries = trak->end_chunk;// - trak->start_chunk;
        pass = (entries - 1) * sizeof(uint32_t);
        TSIOBufferReaderConsume(readerp, pass);

        trak->end_offset = mp4_reader_get_32value(readerp, 0);
        trak->end_offset += trak->end_chunk_samples_size;
#if DEBUG_LOG
        al_log_debug("--> update stco, end_offset=%zd, chunk size=%zd",
                trak->end_offset, trak->end_chunk_samples_size);
#endif
        mp4_reader_set_32value(readerp, 0, trak->end_offset);
        TSIOBufferReaderFree(readerp);
        atom_size = sizeof(mp4_stco_atom) + pass + sizeof(uint32_t);

        avail = TSIOBufferReaderAvail(trak->stco_data.reader);
        TSIOBufferCopy(trak->stco_data.buffer, trak->stco_data.reader,
                pass + sizeof(uint32_t), 0);
        TSIOBufferReaderConsume(trak->stco_data.reader, avail);
    }

    trak->size += atom_size;
    mp4_reader_set_32value(trak->stco_atom.reader, offsetof(mp4_stco_atom, size),
                           atom_size);
    mp4_reader_set_32value(trak->stco_atom.reader, offsetof(mp4_stco_atom, entries), entries);

    return 0;
}

int
Mp4Meta::mp4_update_stbl_atom(Mp4Trak *trak)
{
    trak->size += sizeof(mp4_atom_header);
    mp4_reader_set_32value(trak->stbl_atom.reader, 0, trak->size);

    return 0;
}

int
Mp4Meta::mp4_update_minf_atom(Mp4Trak *trak)
{
    trak->size += sizeof(mp4_atom_header) +
                  trak->vmhd_size +
                  trak->smhd_size +
                  trak->dinf_size;

    mp4_reader_set_32value(trak->minf_atom.reader, 0, trak->size);

    return 0;
}

int
Mp4Meta::mp4_update_mdia_atom(Mp4Trak *trak)
{
    trak->size += sizeof(mp4_atom_header);
    mp4_reader_set_32value(trak->mdia_atom.reader, 0, trak->size);

    return 0;
}

int
Mp4Meta::mp4_update_trak_atom(Mp4Trak *trak)
{
    trak->size += sizeof(mp4_atom_header);
    mp4_reader_set_32value(trak->trak_atom.reader, 0, trak->size);

    return 0;
}

int
Mp4Meta::mp4_adjust_co64_atom(Mp4Trak *trak, off_t adjustment)
{
    int64_t             pos, avail, offset;
    TSIOBufferReader    readerp;

    readerp = TSIOBufferReaderClone(trak->co64_data.reader);
    avail = TSIOBufferReaderAvail(readerp);

    for (pos = 0; pos < avail; pos += sizeof(uint64_t)) {
        offset = mp4_reader_get_64value(readerp, 0);
        offset += adjustment;
        mp4_reader_set_64value(readerp, 0, offset);
        TSIOBufferReaderConsume(readerp, sizeof(uint64_t));
    }

    TSIOBufferReaderFree(readerp);

    return 0;
}

int
Mp4Meta::mp4_adjust_stco_atom(Mp4Trak *trak, int32_t adjustment)
{
    int64_t             pos, avail, offset;
    TSIOBufferReader    readerp;

    readerp = TSIOBufferReaderClone(trak->stco_data.reader);
    avail = TSIOBufferReaderAvail(readerp);

    for (pos = 0; pos < avail; pos += sizeof(uint32_t)) {
        offset = mp4_reader_get_32value(readerp, 0);
        offset += adjustment;
        mp4_reader_set_32value(readerp, 0, offset);
        TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
    }

    TSIOBufferReaderFree(readerp);

    return 0;
}

int64_t
Mp4Meta::mp4_update_mdat_atom(int64_t start_offset, int64_t end_offset)
{
    int64_t     atom_data_size;
    int64_t     atom_size;
    int64_t     atom_header_size;
    u_char      *atom_header;

    atom_data_size = end_offset - start_offset;
    this->start_pos = start_offset;

    atom_header = mdat_atom_header;

    if (atom_data_size > 0xffffffff) {
        atom_size = 1;
        atom_header_size = sizeof(mp4_atom_header64);
        mp4_set_64value(atom_header + sizeof(mp4_atom_header),
                        sizeof(mp4_atom_header64) + atom_data_size);

    } else {
        atom_size = sizeof(mp4_atom_header) + atom_data_size;
        atom_header_size = sizeof(mp4_atom_header);
    }

    this->content_length += atom_header_size + atom_data_size;

    mp4_set_32value(atom_header, atom_size);
    mp4_set_atom_name(atom_header, 'm', 'd', 'a', 't');

    mdat_atom.buffer = TSIOBufferSizedCreate(TS_IOBUFFER_SIZE_INDEX_128);
    mdat_atom.reader = TSIOBufferReaderAlloc(mdat_atom.buffer);

    TSIOBufferWrite(mdat_atom.buffer, atom_header, atom_header_size);

    return atom_header_size;
}


uint32_t
Mp4Meta::mp4_find_key_sample(uint32_t start_sample, Mp4Trak *trak)
{
    uint32_t            i;
    uint32_t            sample, prev_sample, entries;
    TSIOBufferReader    readerp;

    if (trak->stss_data.buffer == NULL)
        return start_sample;

    // 以sample计算时间,在返回sample时应该减1
    // change by Dnion
    //prev_sample = 1;
    prev_sample = 0;
    entries = trak->sync_samples_entries;

    readerp = TSIOBufferReaderClone(trak->stss_data.reader);

    for (i = 0; i < entries; i++) {
        sample = (uint32_t)mp4_reader_get_32value(readerp, 0);
        sample -= 1;

        if (sample > start_sample) {
            goto found;
        }

        prev_sample = sample;
        TSIOBufferReaderConsume(readerp, sizeof(uint32_t));
    }

found:

    TSIOBufferReaderFree(readerp);
    return prev_sample;
}

void
Mp4Meta::mp4_update_mvhd_duration()
{
    int64_t             need;
    uint64_t            duration, cut, length_time;
    mp4_mvhd_atom       *mvhd;
    mp4_mvhd64_atom     mvhd64;

    need = TSIOBufferReaderAvail(mvhd_atom.reader);

    if (need > (int64_t)sizeof(mp4_mvhd64_atom))
        need = sizeof(mp4_mvhd64_atom);

    IOBufferReaderCopy(mvhd_atom.reader, &mvhd64, need);
    mvhd = (mp4_mvhd_atom*)&mvhd64;

    /*
    if (this->rs > 0) {
        cut = (uint64_t)(this->rs * this->timescale / 1000);

    } else {
        cut = this->start * this->timescale / 1000;
    }*/
    cut = this->start * this->timescale / 1000;

    if (mvhd->version[0] == 0) {
        duration = mp4_get_32value(mvhd->duration);
    } else {
        duration = mp4_get_64value(mvhd64.duration);
    }

    duration -= cut;
    
    if (this->length) {
        if (this->ls > 0)
        {
            length_time = (uint64_t)this->ls * timescale / 1000;
        }
        else
        {
            length_time = (uint64_t)this->length * timescale / 1000;
        }
        if (duration > length_time)
            duration = length_time;
    }
    
#if DEBUG_LOG
    al_log_debug("-----------> update_mvhd");
    al_log_debug("---> duration=%zd", duration);
#endif

    if (mvhd->version[0] == 0) {
        mp4_reader_set_32value(mvhd_atom.reader, offsetof(mp4_mvhd_atom, duration), duration);
    } else {        // 64-bit duration
        mp4_reader_set_64value(mvhd_atom.reader, offsetof(mp4_mvhd64_atom, duration), duration);
    }
}

void
Mp4Meta::mp4_update_tkhd_duration(Mp4Trak *trak)
{
    int64_t             need;
    uint64_t            start_time, length_time;
    mp4_tkhd_atom       *tkhd_atom;
    mp4_tkhd64_atom     tkhd64_atom;
    uint64_t             duration;

    need = TSIOBufferReaderAvail(trak->tkhd_atom.reader);

    if (need > (int64_t)sizeof(mp4_tkhd64_atom))
        need = sizeof(mp4_tkhd64_atom);

    IOBufferReaderCopy(trak->tkhd_atom.reader, &tkhd64_atom, need);
    tkhd_atom = (mp4_tkhd_atom*)&tkhd64_atom;

    if (this->rs > 0) {
        start_time = (uint64_t)(this->rs * this->timescale / 1000);
    } else {
        start_time = this->start * this->timescale / 1000;
    }

    if (tkhd_atom->version[0] == 0) {
        duration = mp4_get_32value(tkhd_atom->duration);
    } else {
        duration = mp4_get_64value(tkhd_atom->duration);
    }

    if (duration <= start_time) {
        al_log_debug("tkhd duration is less than start time");
        return;
    }

    duration -= start_time;
    if (this->length) {
        if (this->ls > 0)
        {
            length_time = (uint64_t)this->ls * this->timescale / 1000;
        }
        else
            length_time = (uint64_t)this->length * this->timescale / 1000;
        if (duration > length_time) {
            duration = length_time;
        }
    }

#if DEBUG_LOG
    al_log_debug("-----------> update_tkhd");
    al_log_debug("---> duration=%zd", duration);
#endif

    if (tkhd_atom->version[0] == 0) {
        mp4_reader_set_32value(trak->tkhd_atom.reader,offsetof(mp4_tkhd_atom, duration), duration);
    } else {
        mp4_reader_set_64value(trak->tkhd_atom.reader,offsetof(mp4_tkhd64_atom, duration), duration);
    }
}

void
Mp4Meta::mp4_update_mdhd_duration(Mp4Trak *trak)
{
    int64_t             need;
    uint64_t            duration, start_time, length_time;
    mp4_mdhd_atom       *mdhd;
    mp4_mdhd64_atom     mdhd64;

    memset(&mdhd64, 0, sizeof(mp4_mdhd64_atom));

    need = TSIOBufferReaderAvail(trak->mdhd_atom.reader);
    if (need > (int64_t)sizeof(mp4_mdhd64_atom))
        need = sizeof(mp4_mdhd64_atom);

    IOBufferReaderCopy(trak->mdhd_atom.reader, &mdhd64, need);
    mdhd = (mp4_mdhd_atom*)&mdhd64;

    if (this->rs > 0) {
        start_time = (uint64_t)(this->rs * trak->timescale / 1000);
    } else {
        start_time = this->start * trak->timescale / 1000;
    }

    if (mdhd->version[0] == 0) {
        duration = mp4_get_32value(mdhd->duration);
    } else {
        duration = mp4_get_64value(mdhd64.duration);
    }

    if (duration <= start_time) {
#if DEBUG_LOG        
        al_log_debug("error.. %d", __LINE__);
#endif        
        return;
    }

    duration -= start_time;
    if (this->length) {
        if (this->ls > 0)
        {
            length_time = (uint64_t)this->ls * trak->timescale / 1000;
        }
        else
        {
            length_time = (uint64_t)this->length * trak->timescale / 1000;
        }
        if (duration > length_time)
            duration = length_time;
    }

    if (mdhd->version[0] == 0) {
        mp4_reader_set_32value(trak->mdhd_atom.reader, offsetof(mp4_mdhd_atom, duration), duration);
    } else {
        mp4_reader_set_64value(trak->mdhd_atom.reader,offsetof(mp4_mdhd64_atom, duration), duration);
    }
}


static void
mp4_reader_set_32value(TSIOBufferReader readerp, int64_t offset, uint32_t n)
{
    int                 pos;
    int64_t             avail, left;
    TSIOBufferBlock     blk;
    const char          *start;
    u_char              *ptr;

    pos = 0;
    blk = TSIOBufferReaderStart(readerp);

    while (blk) {

        start = TSIOBufferBlockReadStart(blk, readerp, &avail);

        if (avail <= offset) {
            offset -= avail;
        } else {
            left = avail - offset;
            ptr = (u_char*)(const_cast<char*> (start) + offset);

            while (pos < 4 && left > 0) {
                 *ptr++ = (u_char) ((n) >> ((3 - pos) * 8));
                 pos++;
                 left--;
            }

            if (pos >= 4)
                return;

            offset = 0;
        }
        blk = TSIOBufferBlockNext(blk);
    }
}

static void
mp4_reader_set_64value(TSIOBufferReader readerp, int64_t offset, uint64_t n)
{
    int                 pos;
    int64_t             avail, left;
    TSIOBufferBlock     blk;
    const char          *start;
    u_char              *ptr;

    pos = 0;
    blk = TSIOBufferReaderStart(readerp);

    while (blk) {

        start = TSIOBufferBlockReadStart(blk, readerp, &avail);

        if (avail <= offset) {
            offset -= avail;

        } else {
            left = avail - offset;
            ptr = (u_char*)(const_cast<char*> (start) + offset);

            while (pos < 8 && left > 0) {
                 *ptr++ = (u_char) ((n) >> ((7 - pos) * 8));
                 pos++;
                 left--;
            }

            if (pos >= 4)
                return;
            offset = 0;
        }
        blk = TSIOBufferBlockNext(blk);
    }
}

static uint32_t
mp4_reader_get_32value(TSIOBufferReader readerp, int64_t offset)
{
    int                 pos;
    int64_t             avail, left;
    TSIOBufferBlock     blk;
    const char          *start;
    const u_char        *ptr;
    u_char              res[4];

    pos = 0;
    blk = TSIOBufferReaderStart(readerp);

    while (blk) {

        start = TSIOBufferBlockReadStart(blk, readerp, &avail);

        if (avail <= offset) {
            offset -= avail;
        } else {

            left = avail - offset;
            ptr = (u_char*)(start + offset);

            while (pos < 4 && left > 0) {
                res[3-pos] = *ptr++;
                pos++;
                left--;
            }

            if (pos >= 4) {
                return *(uint32_t*)res;
            }

            offset = 0;
        }
        blk = TSIOBufferBlockNext(blk);
    }

    return -1;
}

static uint64_t
mp4_reader_get_64value(TSIOBufferReader readerp, int64_t offset)
{
    int                 pos;
    int64_t             avail, left;
    TSIOBufferBlock     blk;
    const char          *start;
    u_char              *ptr;
    u_char              res[8];

    pos = 0;
    blk = TSIOBufferReaderStart(readerp);

    while (blk) {

        start = TSIOBufferBlockReadStart(blk, readerp, &avail);

        if (avail <= offset) {
            offset -= avail;
        } else {

            left = avail - offset;
            ptr = (u_char*)(start + offset);

            while (pos < 8 && left > 0) {
                res[7-pos] = *ptr++;
                pos++;
                left--;
            }

            if (pos >= 8) {
                return *(uint64_t*)res;
            }

            offset = 0;
        }

        blk = TSIOBufferBlockNext(blk);
    }

    return -1;
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
