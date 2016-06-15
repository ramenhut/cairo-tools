
/*
// Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.
//
// evx_convert.cpp
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

#include "cairo/base.h"
#include "cairo/evx1.h"
#include "cairo/image.h"
#include "evx_format.h"

extern "C"
{
    int ffmpeg_initialize();
    int ffmpeg_deinitialize();
    int ffmpeg_reset();

    int ffmpeg_play_file(char * filename, int *format, int *width, int *height);
    int ffmpeg_copy_current_frame(unsigned char *dest, int row_pitch);
    int ffmpeg_refresh(int *encoded_frame_size);

    int ffmpeg_get_frame_count();
    float ffmpeg_get_frame_rate();

} // extern "C"

void _print_file_header(const EVX_MEDIA_FILE_HEADER &header)
{
    evx_msg("Printing file header:");
    evx_msg("size = %i", header.header_size);
    evx_msg("version = %i", header.version);
    evx_msg("width = %i", header.frame_width);
    evx_msg("height = %i", header.frame_height);
    evx_msg("frame count = %i", header.frame_count);
    evx_msg("rate = %f", header.frame_rate);
}

void _prepare_evx_header(EVX_MEDIA_FILE_HEADER *header, uint32 width, uint32 height)
{
    header->magic[0] = 'E';
    header->magic[1] = 'V';
    header->magic[2] = 'X';
    header->magic[3] = '1';
    header->header_size = sizeof(EVX_MEDIA_FILE_HEADER);
    header->version = 1;
    header->frame_width = width;
    header->frame_height = height;
    header->frame_count = ffmpeg_get_frame_count();
    header->frame_rate = ffmpeg_get_frame_rate();

    _print_file_header(*header);
}

int main(int argc, char **argv)
{
    FILE *dest = NULL;
    image frame_image;
    evx1_encoder *encoder;
    bit_stream cairo_stream;
    int32 frame_index = 0;
    int32 encoded_size = 0;
    int32 content_width = 0;
    int32 content_height = 0;
    int32 content_format = 0;
    EVX_MEDIA_FILE_HEADER header;

    evx_msg("Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.");

    if (4 != argc)
    {
        // No need to get fancy.
        evx_msg("Required syntax: convert <input_filename> quality <output_filename>");
        return 0;
    }

    dest = fopen(argv[3], "wb");

    if (!dest)
    {
        evx_msg("Error opening dest file %s", argv[3]);
        return 0;
    }

    ffmpeg_initialize();
    
    if (0 != ffmpeg_play_file(argv[1], (int*) &content_format, (int*) &content_width, (int*) &content_height))
    {
        evx_msg("Failed to open content file %s", argv[1]);
        ffmpeg_deinitialize();
        fclose(dest);
        return 0;
    }

    create_encoder(&encoder);
    encoder->set_quality(atoi(argv[2]));

    create_image(EVX_IMAGE_FORMAT_R8G8B8, content_width, content_height, &frame_image);
    cairo_stream.resize_capacity((4*EVX_MB) << 3);

    _prepare_evx_header(&header, content_width, content_height);
    fwrite(&header, sizeof(header), 1, dest);

    while (ffmpeg_refresh(&encoded_size) >= 0)
    {
        ffmpeg_copy_current_frame(frame_image.query_data(), frame_image.query_row_pitch());

        // encode using cairo and then flush the frame to disk.
        encoder->encode(frame_image.query_data(), frame_image.query_width(), frame_image.query_height(), &cairo_stream);

        EVX_MEDIA_FRAME_HEADER frame_header;
        frame_header.magic[0] = 'E';
        frame_header.magic[0] = 'V';
        frame_header.magic[0] = 'F';
        frame_header.magic[0] = 'H';
        frame_header.frame_index = frame_index++;
        frame_header.frame_size = cairo_stream.query_byte_occupancy();
        frame_header.header_size = sizeof(frame_header);
        
        fwrite(&frame_header, sizeof(frame_header), 1, dest);
        fwrite(cairo_stream.query_data(), cairo_stream.query_byte_occupancy(), 1, dest);
        cairo_stream.empty();

        if (0 == (frame_index % 10))
        {
            evx_msg("Processing frame %i", frame_index);
        }
    }

    destroy_encoder(encoder);
    ffmpeg_deinitialize();
    fclose(dest);

    return 0;
}