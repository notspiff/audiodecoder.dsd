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

#include "ByteOrder.hxx"
#include <string.h>

struct DsdId {
  char value[4];

  bool Equals(const char *s) const
  {
    return memcmp(value, s, sizeof(value)) == 0;
  }
};

class DffDsdUint64
{
  uint32_t hi;
  uint32_t lo;
public:
  uint64_t Read() const
  {
    return (uint64_t(FromBE32(hi)) << 32) |
            uint64_t(FromBE32(lo));
  }
};

struct DsdiffHeader {
  DsdId id;
  DffDsdUint64 size;
  DsdId format;
};

struct DsdiffChunkHeader {
  DsdId id;
  DffDsdUint64 size;

  /**
   * Read the "size" attribute from the specified header, converting it
   * to the host byte order if needed.
   */
    uint64_t GetSize() const {
      return size.Read();
    }
};

/** struct for DSDIFF native Artist and Title tags */
struct dsdiff_native_tag {
  uint32_t size;
};

struct DsdiffMetaData {
  unsigned sample_rate, channels;
  bool bitreverse;
  uint64_t chunk_size;
  /** offset for artist tag */
  int64_t diar_offset;
  /** offset for title tag */
  int64_t diti_offset;
  char diar[61];
  char diti[61];

  DsdiffMetaData()
  {
    diar[0] = diti[0] = '\0';
  }
};

bool dsdiff_read_metadata(void* file, DsdiffMetaData *metadata,
                          DsdiffChunkHeader *chunk_header);

bool dsdiff_read_metadata_extra(void* file, DsdiffMetaData *metadata,
                                DsdiffChunkHeader *chunk_header);
