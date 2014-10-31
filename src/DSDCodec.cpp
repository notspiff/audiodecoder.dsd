/*
 * Copyright (C) 2014 Arne Morten Kvarving
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

#include "kodi/libXBMC_addon.h"

#include "kodi/kodi_audiodec_dll.h"
#include "kodi/AEChannelData.h"
#include "dsd2pcm.hpp"
#include <iostream>

#include "DSDiffParser.h"
#include "DSFParser.h"

ADDON::CHelper_libXBMC_addon* XBMC = NULL;

extern "C" {

bool registerHelper(void* hdl)
{
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
  XBMC=NULL;
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

enum DSDType {
  DSDIFF,
  DSF
};

struct DSDContext
{
  void* file;
  std::vector<dxd> dxds;
  uint8_t* buffer;
  size_t buffer_size;
  int channels;
  int lsbitfirst;
  int block;
  DSDType type;
  bool bitreverse;
  int64_t datastart;
  int samplerate;
};

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

  static enum AEChannel map[3][9] = {
    {AE_CH_FL, AE_CH_FR, AE_CH_NULL},
    {AE_CH_FL, AE_CH_FR, AE_CH_FC, AE_CH_LFE, AE_CH_BL, AE_CH_BR, AE_CH_NULL}, // 6 channels
    {AE_CH_FL, AE_CH_FR, AE_CH_FC, AE_CH_LFE, AE_CH_BL, AE_CH_BR, AE_CH_SL, AE_CH_SR, AE_CH_NULL} // 8 channels
  };
                        
  DsdiffMetaData metadata;
  DsdiffChunkHeader chunk_header;
  if (!dsdiff_read_metadata(result->file, &metadata, &chunk_header))
  {
    XBMC->SeekFile(result->file, 0, SEEK_SET);
    DsfMetaData metadata;
    if (!dsf_read_metadata(result->file, &metadata))
    {
      XBMC->CloseFile(result->file);
      delete result;
      return NULL;
    }
    result->type = DSF;
    result->channels = metadata.channels;
    result->bitreverse = metadata.bitreverse;
    result->lsbitfirst = 0;
    result->block = 4096;
    result->samplerate = *samplerate = metadata.sample_rate/8;
    *totaltime = metadata.chunk_size/(metadata.channels*result->samplerate)*1000;
    result->datastart = XBMC->GetFilePosition(result->file);
  }
  else
  {
    result->type = DSDIFF;
    result->datastart = XBMC->GetFilePosition(result->file);
    dsdiff_read_metadata_extra(result->file, &metadata, &chunk_header);
    XBMC->SeekFile(result->file, result->datastart, SEEK_SET);
    result->channels = metadata.channels;
    result->lsbitfirst = 0;
    result->block = 16384;
    result->samplerate = *samplerate = metadata.sample_rate/8;
    *totaltime = metadata.chunk_size/(metadata.channels*result->samplerate)*1000;
  }
  *channels = result->channels;
  *format = AE_FMT_S24LE3;
  *channelinfo = map[(result->channels==2?0:(result->channels==6?1:2))];
  *bitspersample = 24;
  result->dxds.resize(result->channels);
  result->buffer = new uint8_t[result->block*result->channels*3];
  result->buffer_size = 0;

  return result;
}

int ReadPCM(void* context, uint8_t* pBuffer, int size, int *actualsize)
{
  *actualsize=0;

  DSDContext* ctx = (DSDContext*)context;

  uint8_t in[ctx->block*ctx->channels];

  if (!ctx->buffer_size)
  {
    if (!XBMC->ReadFile(ctx->file, in, ctx->block*ctx->channels))
      return 1;
    if (ctx->type == DSF)
    {
      uint8_t scratch[ctx->block*ctx->channels];
      dsf_to_pcm_order(in, scratch, ctx->block*ctx->channels, ctx->bitreverse);
    }

    float float_data[ctx->block];
    for (size_t i=0;i<ctx->channels;++i)
    {
      ctx->dxds[i].translate(ctx->block,in+i,ctx->channels,ctx->lsbitfirst,float_data,1);
      uint8_t* out = ctx->buffer+i*3;
      for (size_t s=0;s<ctx->block;++s)
      {
        float r = float_data[s]*8388608;
        long smp = clip(-8388608,myround(r),8388607);
        write_intel24(out,smp);
        out += ctx->channels*3;
      }
    }
    ctx->buffer_size = ctx->channels*ctx->block*3;
  }

  size_t tocopy = std::min((size_t)size, ctx->buffer_size);
  memcpy(pBuffer, ctx->buffer+ctx->block*ctx->channels*3-ctx->buffer_size, tocopy);
  ctx->buffer_size -= tocopy;
  *actualsize = tocopy;
  return 0;
}

int64_t Seek(void* context, int64_t time)
{
  DSDContext* ctx = (DSDContext*)context;
  int64_t offs = time*ctx->samplerate*ctx->channels/1000;

  if (XBMC->SeekFile(ctx->file, ctx->datastart+offs, SEEK_SET) != ctx->datastart+offs)
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
  return true;
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
