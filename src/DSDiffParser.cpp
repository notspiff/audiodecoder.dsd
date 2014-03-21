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

#include "DSDiffParser.h"
#include "xbmc/libXBMC_addon.h"

extern ADDON::CHelper_libXBMC_addon* XBMC;

/**
 * Read and parse a "SND" chunk inside "PROP".
 */
static bool dsdiff_read_prop_snd(void* file, DsdiffMetaData *metadata,
                                 int64_t end_offset)
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

bool dsdiff_read_metadata(void* file, DsdiffMetaData *metadata,
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

