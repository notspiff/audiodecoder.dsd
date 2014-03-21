/*
 * Copyright (C) 2003-2014 The Music Player Daemon Project
 * http://www.musicpd.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#pragma once

#include "DSDiffParser.h"

struct DsfMetaData {
  unsigned sample_rate, channels;
  bool bitreverse;
  uint64_t chunk_size;
};

class DsdUint64 {
  uint32_t lo;
  uint32_t hi;

  public:
  uint64_t Read() const
  {
    return (uint64_t(FromLE32(hi)) << 32) |
            uint64_t(FromLE32(lo));
  }
};

struct DsfHeader {
  /** DSF header id: "DSD " */
  DsdId id;
  /** DSD chunk size, including id = 28 */
  DsdUint64 size;
  /** total file size */
  DsdUint64 fsize;
  /** pointer to id3v2 metadata, should be at the end of the file */
  DsdUint64 pmeta;
};

/** DSF file fmt chunk */
struct DsfFmtChunk {
  /** id: "fmt " */
  DsdId id;
  /** fmt chunk size, including id, normally 52 */
  DsdUint64 size;
  /** version of this format = 1 */
  uint32_t version;
  /** 0: DSD raw */
  uint32_t formatid;
  /** channel type, 1 = mono, 2 = stereo, 3 = 3 channels, etc */
  uint32_t channeltype;
  /** Channel number, 1 = mono, 2 = stereo, ... 6 = 6 channels */
  uint32_t channelnum;
  /** sample frequency: 2822400, 5644800 */
  uint32_t sample_freq;
  /** bits per sample 1 or 8 */
  uint32_t bitssample;
  /** Sample count per channel in bytes */
  DsdUint64 scnt;
  /** block size per channel = 4096 */
  uint32_t block_size;
  /** reserved, should be all zero */
  uint32_t reserved;
};

struct DsfDataChunk {
  DsdId id;
  /** "data" chunk size, includes header (id+size) */
  DsdUint64 size;
};

bool dsf_read_metadata(void* file, DsfMetaData *metadata);
void dsf_to_pcm_order(uint8_t *dest, uint8_t *scratch,
                      size_t nrbytes, bool bitreverse);
