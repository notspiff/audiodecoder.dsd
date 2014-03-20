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

XBMC interface Copyright 2014 Arne Morten Kvarving
*/

#include "xbmc/libXBMC_addon.h"

#include "xbmc/xbmc_audiodec_dll.h"
#include "xbmc/AEChannelData.h"
#include "dsd2pcm.hpp"
#include <iostream>

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

  result->channels = 2; // TODO!
  result->lsbitfirst = 0;
  result->block = 16384;
  result->bytespersample=3;
  *channels = result->channels;
  *samplerate = 352000;
  *bitspersample = result->bytespersample*8;
  *totaltime = XBMC->GetFileLength(result->file)/(352*result->channels);

  result->dxds.resize(result->channels);
  result->buffer = new uint8_t[result->block*result->channels*result->bytespersample];
  result->buffer_size = 0;

  *format = AE_FMT_S24LE3;
  static enum AEChannel map[3] = {AE_CH_FL, AE_CH_FR, AE_CH_NULL};
  *channelinfo = map;

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
  delete[] ctx->buffer;
  delete ctx;
  return true;
}

bool ReadTag(const char* strFile, char* title, char* artist,
             int* length)
{
  return false;
}

int TrackCount(const char* strFile)
{
  return 1;
}
}
