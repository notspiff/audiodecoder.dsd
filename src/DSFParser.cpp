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

#include "DSFParser.h"
#include "bit_reverse.h"
#include "libXBMC_addon.h"

extern ADDON::CHelper_libXBMC_addon* XBMC;

/**
 * Read and parse all needed metadata chunks for DSF files.
 */
bool dsf_read_metadata(void* file, DsfMetaData *metadata)
{
  DsfHeader dsf_header;
  if (!XBMC->ReadFile(file, &dsf_header, sizeof(dsf_header)) ||
      !dsf_header.id.Equals("DSD "))
    return false;

  const uint64_t chunk_size = dsf_header.size.Read();
  if (sizeof(dsf_header) != chunk_size)
    return false;

  /* read the 'fmt ' chunk of the DSF file */
  DsfFmtChunk dsf_fmt_chunk;
  if (!XBMC->ReadFile(file, &dsf_fmt_chunk, sizeof(dsf_fmt_chunk)) ||
      !dsf_fmt_chunk.id.Equals("fmt "))
    return false;

  const uint64_t fmt_chunk_size = dsf_fmt_chunk.size.Read();
  if (fmt_chunk_size != sizeof(dsf_fmt_chunk))
    return false;

  uint32_t samplefreq = FromLE32(dsf_fmt_chunk.sample_freq);

  /* for now, only support version 1 of the standard, DSD raw stereo
     files with a sample freq of 2822400 or 5644800 Hz */

  if (dsf_fmt_chunk.version != 1 || dsf_fmt_chunk.formatid != 0
      || dsf_fmt_chunk.channeltype != 2
      || dsf_fmt_chunk.channelnum != 2
      || (samplefreq != 2822400 && samplefreq != 5644800))
    return false;

  uint32_t chblksize = FromLE32(dsf_fmt_chunk.block_size);
  /* according to the spec block size should always be 4096 */
  if (chblksize != 4096)
    return false;

  /* read the 'data' chunk of the DSF file */
  DsfDataChunk data_chunk;
  if (!XBMC->ReadFile(file, &data_chunk, sizeof(data_chunk)) ||
      !data_chunk.id.Equals("data"))
    return false;

  /* data size of DSF files are padded to multiple of 4096,
     we use the actual data size as chunk size */

  uint64_t data_size = data_chunk.size.Read();
  if (data_size < sizeof(data_chunk))
    return false;

  data_size -= sizeof(data_chunk);

  /* data_size cannot be bigger or equal to total file size */
  const uint64_t size = (uint64_t)XBMC->GetFileLength(file);
  if (data_size >= size)
    return false;

  /* use the sample count from the DSF header as the upper
     bound, because some DSF files contain junk at the end of
     the "data" chunk */
  const uint64_t samplecnt = dsf_fmt_chunk.scnt.Read();
  const uint64_t playable_size = samplecnt * 2 / 8;
  if (data_size > playable_size)
    data_size = playable_size;

  metadata->chunk_size = data_size;
  metadata->channels = (unsigned) dsf_fmt_chunk.channelnum;
  metadata->sample_rate = samplefreq;

  /* check bits per sample format, determine if bitreverse is needed */
  metadata->bitreverse = dsf_fmt_chunk.bitssample == 1;

  return true;
}

static void
bit_reverse_buffer(uint8_t *p, uint8_t *end)
{
  for (; p < end; ++p)
    *p = bit_reverse(*p);
}

/**
 * DSF data is build up of alternating 4096 blocks of DSD samples for left and
 * right. Convert the buffer holding 1 block of 4096 DSD left samples and 1
 * block of 4096 DSD right samples to 8k of samples in normal PCM left/right
 * order.
 */
void dsf_to_pcm_order(uint8_t *dest, uint8_t *scratch,
                      size_t nrbytes, bool bitreverse)
{
  if (bitreverse)
    bit_reverse_buffer(dest, dest+nrbytes);

  for (unsigned i = 0, j = 0; i < (unsigned)nrbytes; i += 2)
  {
    scratch[i] = *(dest+j);
    j++;
  }

  for (unsigned i = 1, j = 0; i < (unsigned) nrbytes; i += 2)
  {
    scratch[i] = *(dest+4096+j);
    j++;
  }

  for (unsigned i = 0; i < (unsigned)nrbytes; i++)
  {
    *dest = scratch[i];
    dest++;
  }
}
