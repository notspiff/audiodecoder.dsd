/*
Copyright 2009, 2011 Sebastian Gesemann. All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright notice, this list of
      conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice, this list
      of conditions and the following disclaimer in the documentation and/or other materials
      provided with the distribution.
THIS SOFTWARE IS PROVIDED BY SEBASTIAN GESEMANN ''AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SEBASTIAN GESEMANN OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
The views and conclusions contained in the software and documentation are those of the
authors and should not be interpreted as representing official policies, either expressed
or implied, of Sebastian Gesemann.

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

 XBMC interface Copyright 2014 Arne Morten Kvarving
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

#include "xbmc/libXBMC_addon.h"

#include "xbmc/xbmc_audiodec_dll.h"
#include "xbmc/AEChannelData.h"
#include "dsd2pcm.hpp"
#include <iostream>
#include "ByteOrder.hxx"

inline long myround(float x)
{
  return static_cast<long>(x + (x>=0 ? 0.5f : -0.5f));
}

  template<typename T>
struct id { typedef T type; };

  template<typename T>
inline T clip(
    typename id<T>::type min,
    T v,
    typename id<T>::type max)
{
  if (v<min) return min;
  if (v>max) return max;
  return v;
}

inline void write_intel24(unsigned char * ptr, unsigned long word)
{
  ptr[0] =  word        & 0xFF;
  ptr[1] = (word >>  8) & 0xFF;
  ptr[2] = (word >> 16) & 0xFF;
}

extern "C" {

ADDON::CHelper_libXBMC_addon *XBMC           = NULL;

bool registerHelper(void* hdl)
{
  if (!XBMC)
    XBMC = new ADDON::CHelper_libXBMC_addon;

  if (!XBMC->RegisterMe(hdl))
  {
    delete XBMC, XBMC=NULL;
    return false;
  }

  return true;
}

//-- Create -------------------------------------------------------------------
// Called on load. Addon should fully initalize or return error status
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!registerHelper(hdl))
    return ADDON_STATUS_PERMANENT_FAILURE;

  return ADDON_STATUS_OK;
}

//-- Stop ---------------------------------------------------------------------
// This dll must cease all runtime activities
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Stop()
{
}

//-- Destroy ------------------------------------------------------------------
// Do everything before unload of this add-on
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
//  XBMC=NULL;
}

//-- HasSettings --------------------------------------------------------------
// Returns true if this add-on use settings
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
bool ADDON_HasSettings()
{
  return false;
}

//-- GetStatus ---------------------------------------------------------------
// Returns the current Status of this visualisation
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return ADDON_STATUS_OK;
}

//-- GetSettings --------------------------------------------------------------
// Return the settings for XBMC to display
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- FreeSettings --------------------------------------------------------------
// Free the settings struct passed from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------

void ADDON_FreeSettings()
{
}

//-- SetSetting ---------------------------------------------------------------
// Set a specific Setting value (called from XBMC)
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *strSetting, const void* value)
{
  return ADDON_STATUS_OK;
}

//-- Announce -----------------------------------------------------------------
// Receive announcements from XBMC
// !!! Add-on master function !!!
//-----------------------------------------------------------------------------
void ADDON_Announce(const char *flag, const char *sender, const char *message, const void *data)
{
}

struct DSDContext
{
  void* file;
  std::vector<dxd> dxds;
  uint8_t* buffer;
  size_t buffer_size;
  int channels;
  int lsbitfirst;
  int block;
  int bytespersample;
};

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

/**
 * Read and parse a "SND" chunk inside "PROP".
 */
static bool
dsdiff_read_prop_snd(void* file, DsdiffMetaData *metadata, int64_t end_offset)
{
  DsdiffChunkHeader header;
  while ((XBMC->GetFilePosition(file) + sizeof(header)) <= end_offset)
  {
    if (!XBMC->ReadFile(file, &header, sizeof(header)))
      return false;

    int64_t chunk_end_offset = XBMC->GetFilePosition(file) + header.GetSize();
    if (chunk_end_offset > end_offset)
      return false;

    if (header.id.Equals("FS  "))
    {
      uint32_t sample_rate;
      if (!XBMC->ReadFile(file, &sample_rate, sizeof(sample_rate)))
        return false;

      metadata->sample_rate = FromBE32(sample_rate);
    } 
    else if (header.id.Equals("CHNL"))
    {
      uint16_t channels;
      if (header.GetSize() < sizeof(channels) ||
          !XBMC->ReadFile(file, &channels, sizeof(channels)) ||
          XBMC->SeekFile(file, chunk_end_offset, SEEK_SET) != chunk_end_offset)
        return false;

      metadata->channels = FromBE16(channels);
    } 
    else if (header.id.Equals("CMPR"))
    {
      DsdId type;
      if (header.GetSize() < sizeof(type) ||
          !XBMC->ReadFile(file, &type, sizeof(type)) ||
          XBMC->SeekFile(file, chunk_end_offset, SEEK_SET) != chunk_end_offset)
        return false;

      if (!type.Equals("DSD "))
        /* only uncompressed DSD audio data
           is implemented */
        return false;
    } 
    else
    {
      /* ignore unknown chunk */

      if (XBMC->SeekFile(file, chunk_end_offset, SEEK_SET) != chunk_end_offset)
        return false;
    }
  }

  return XBMC->GetFilePosition(file) == end_offset;
}

/**
 * Read and parse a "PROP" chunk.
 */
static bool dsdiff_read_prop(void* file, DsdiffMetaData *metadata,
                             const DsdiffChunkHeader *prop_header)
{
  uint64_t prop_size = prop_header->GetSize();
  int64_t end_offset = XBMC->GetFilePosition(file) + prop_size;

  DsdId prop_id;
  if (prop_size < sizeof(prop_id) ||
      !XBMC->ReadFile(file, &prop_id, sizeof(prop_id)))
    return false;

  if (prop_id.Equals("SND "))
    return dsdiff_read_prop_snd(file, metadata, end_offset);
  else
    /* ignore unknown PROP chunk */
    return XBMC->SeekFile(file, end_offset, SEEK_SET) == end_offset;
}

static bool dsdiff_read_metadata(void* file, DsdiffMetaData *metadata,
                                 DsdiffChunkHeader *chunk_header)
{
  DsdiffHeader header;
  if (!XBMC->ReadFile(file, &header, sizeof(header)) ||
      !header.id.Equals("FRM8") ||
      !header.format.Equals("DSD "))
    return false;

  while (true)
  {
    if (!XBMC->ReadFile(file, chunk_header, sizeof(*chunk_header)))
      return false;

    if (chunk_header->id.Equals("PROP"))
    {
      if (!dsdiff_read_prop(file, metadata, chunk_header))
        return false;
    } 
    else if (chunk_header->id.Equals("DSD "))
    {
      const uint64_t chunk_size = chunk_header->GetSize();
      metadata->chunk_size = chunk_size;
      return true;
    } 
    else
    {
      /* ignore unknown chunk */
      const uint64_t chunk_size = chunk_header->GetSize();
      int64_t chunk_end_offset = XBMC->GetFilePosition(file) + chunk_size;

      if (XBMC->SeekFile(file, chunk_end_offset, SEEK_SET) != chunk_end_offset)
        return false;
    }
  }
}

static void dsdiff_handle_native_tag(void* file, int64_t tagoffset, char* tag)
{
  if (XBMC->SeekFile(file,tagoffset,SEEK_SET) != tagoffset)
    return;

  struct dsdiff_native_tag metatag;

  if (!XBMC->ReadFile(file, &metatag, sizeof(metatag)))
    return;

  uint32_t length = FromBE32(metatag.size);

  /* Check and limit size of the tag to prevent a stack overflow */
  if (length == 0 || length > 60)
    return;

  if (!XBMC->ReadFile(file, tag, length))
    return;

  tag[length] = '\0';
  return;
}

bool dsdiff_read_metadata_extra(void* file, DsdiffMetaData *metadata,
                                DsdiffChunkHeader *chunk_header)
{
  /* skip from DSD data to next chunk header */
  if (XBMC->SeekFile(file, metadata->chunk_size, SEEK_CUR) <= 0)
    return false;
  if (!XBMC->ReadFile(file, chunk_header, sizeof(*chunk_header)))
    return false;

  /* Now process all the remaining chunk headers in the stream
     and record their position and size */

  int64_t size = XBMC->GetFileLength(file);
  while (XBMC->GetFilePosition(file) < size)
  {
    uint64_t chunk_size = chunk_header->GetSize();

    /* DIIN chunk, is directly followed by other chunks  */
    if (chunk_header->id.Equals("DIIN"))
      chunk_size = 0;

    /* DIAR chunk - DSDIFF native tag for Artist */
    if (chunk_header->id.Equals("DIAR"))
    {
      chunk_size = chunk_header->GetSize();
      metadata->diar_offset = XBMC->GetFilePosition(file);
    }

    /* DITI chunk - DSDIFF native tag for Title */
    if (chunk_header->id.Equals("DITI"))
    {
      chunk_size = chunk_header->GetSize();
      metadata->diti_offset = XBMC->GetFilePosition(file);
    }
    if (chunk_size != 0)
    {
      if (XBMC->SeekFile(file, chunk_size, SEEK_CUR) <= 0)
        break;
    }

    if (XBMC->GetFilePosition(file) < size)
    {
      if (!XBMC->ReadFile(file, chunk_header, sizeof(*chunk_header)))
        return false;
    }
  }
  /* done processing chunk headers, process tags if any */

  if (metadata->diar_offset != 0)
    dsdiff_handle_native_tag(file, metadata->diar_offset, metadata->diar);

  if (metadata->diti_offset != 0)
    dsdiff_handle_native_tag(file, metadata->diti_offset, metadata->diti);

  return true;
}

void* Init(const char* strFile, unsigned int filecache, int* channels,
           int* samplerate, int* bitspersample, int64_t* totaltime,
           int* bitrate, AEDataFormat* format, const AEChannel** channelinfo)
{
  if (!XBMC)
    return NULL;

  DSDContext* result = new DSDContext;

  result->file = XBMC->OpenFile(strFile, 0);
  if (!result->file)
  {
    delete result;
    return NULL;
  }

  DsdiffMetaData metadata;
  DsdiffChunkHeader chunk_header;
  if (!dsdiff_read_metadata(result->file, &metadata, &chunk_header))
  {
    XBMC->CloseFile(result->file);
    delete result;
    return NULL;
  }
  dsdiff_read_metadata_extra(result->file, &metadata, &chunk_header);

  result->channels = metadata.channels;
  result->lsbitfirst = 0;
  result->block = 16384;
  result->bytespersample=3;
  *channels = result->channels;
  *samplerate = 352000; // decoder currently always decimates to 352000
  *bitspersample = result->bytespersample*8;
  *totaltime = metadata.chunk_size/metadata.channels*8/metadata.sample_rate*1000;

  result->dxds.resize(result->channels);
  result->buffer = new uint8_t[result->block*result->channels*result->bytespersample];
  result->buffer_size = 0;

  *format = AE_FMT_S24LE3;
  static enum AEChannel map[3][9] = {
    {AE_CH_FL, AE_CH_FR, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FR, AE_CH_FC, AE_CH_LFE, AE_CH_BL, AE_CH_BR, AE_CH_NULL}, // 6 channels
    {AE_CH_FL, AE_CH_FR, AE_CH_FC, AE_CH_LFE, AE_CH_BL, AE_CH_BR, AE_CH_SL, AE_CH_SR, AE_CH_NULL} // 8 channels
  };
                        
  *channelinfo = map[(metadata.channels==2?0:(metadata.channels==6?1:2))];

  return result;
}

int ReadPCM(void* context, uint8_t* pBuffer, int size, int *actualsize)
{
  *actualsize=0;

  DSDContext* ctx = (DSDContext*)context;

  uint8_t in[ctx->block*ctx->channels];
  float float_data[ctx->block];

  if (!ctx->buffer_size) {
    if (!XBMC->ReadFile(ctx->file, in, ctx->block*ctx->channels))
      return 1;
    for (size_t i=0;i<ctx->channels;++i)
    {
      ctx->dxds[i].translate(ctx->block,in+i,ctx->channels,ctx->lsbitfirst,float_data,1);
      uint8_t* out = ctx->buffer+i*ctx->bytespersample;
      for (size_t s=0;s<ctx->block;++s)
      {
        float r = float_data[s]*8388608;
        long smp = clip(-8388608,myround(r),8388607);
        write_intel24(out,smp);
        out += ctx->channels*ctx->bytespersample;
      }
    }
    ctx->buffer_size = ctx->channels*ctx->block*ctx->bytespersample;
  }

  size_t tocopy = std::min((size_t)size, ctx->buffer_size);
  memcpy(pBuffer, ctx->buffer+ctx->block*ctx->channels*ctx->bytespersample-ctx->buffer_size, tocopy);
  ctx->buffer_size -= tocopy;
  *actualsize = tocopy;
  return 0;
}

int64_t Seek(void* context, int64_t time)
{
  DSDContext* ctx = (DSDContext*)context;
  int64_t offs = time*352*ctx->channels;

  if (XBMC->SeekFile(ctx->file, offs, SEEK_SET) != offs)
    return -1;

  ctx->buffer_size = 0;

  return time;
}

bool DeInit(void* context)
{
  DSDContext* ctx = (DSDContext*)context;
  XBMC->CloseFile(ctx->file);
  delete[] ctx->buffer;
  delete ctx;
  return true;
}

bool ReadTag(const char* strFile, char* title, char* artist,
             int* length)
{
  void* file = XBMC->OpenFile(strFile, 0);
  if (!file)
    return false;

  DsdiffMetaData metadata;
  DsdiffChunkHeader chunk_header;
  if (!dsdiff_read_metadata(file, &metadata, &chunk_header))
  {
    XBMC->CloseFile(file);
    return false;
  }
  dsdiff_read_metadata_extra(file, &metadata, &chunk_header);
  strcpy(title,metadata.diar);
  strcpy(artist,metadata.diti);
  *length = metadata.chunk_size/metadata.channels*8/metadata.sample_rate;
  XBMC->CloseFile(file);
  return true;
}

int TrackCount(const char* strFile)
{
  return 1;
}
}
