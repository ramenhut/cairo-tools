
/*
// Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.
//
// evx_format.h
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional Information:
//
//   For more information, visit http://www.bertolami.com.
*/

#ifndef __EVX_FORMAT_H__
#define __EVX_FORMAT_H__

#include "cairo/base.h"

using namespace evx;

#pragma pack( push )
#pragma pack( 2 )

typedef struct EVX_MEDIA_FILE_HEADER
{
    uint8 magic[4];             // must be 'EVX1'
    uint32 header_size;         // must be sizeof(EVX_MEDIA_FILE_HEADER)
    uint8 version;  

    uint32 frame_width;
    uint32 frame_height;
    uint64 frame_count;         // only a hint, may be zero.
    float frame_rate;

    uint8 reserved[3];

} EVX_MEDIA_FILE_HEADER;

typedef struct EVX_MEDIA_FRAME_HEADER
{
    uint8 magic[4];              // must be 'EVFH'
    uint32 header_size;          // must be sizeof(EVX_MEDIA_FILE_HEADER)
    uint64 frame_index;         
    uint32 frame_size;           // size of payload, not including the header

} EVX_MEDIA_FRAME_HEADER;

#pragma pack(pop)

#endif // __EVX_FORMAT_H__