
/*
// Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.
//
// evx_ffmpeg.cpp
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

extern "C" {

#include "ffmpeg/libavformat/avformat.h"
#include "ffmpeg/libswscale/swscale.h"

#undef exit

AVFormatContext *g_format_context = NULL;
AVCodecContext *g_codec_context = NULL;
AVCodec *g_codec = NULL;
AVFrame *g_frame = NULL;   
AVFrame *g_frame_2 = NULL;
unsigned char *g_raw_buffer = NULL;
unsigned char *g_raw_buffer_0 = NULL;
int g_buffer_size = 0;
int g_current_stream_index = -1;
static struct SwsContext * g_scale_context = 0;

int ffmpeg_initialize()
{
    av_register_all();
    return 0;
}

int ffmpeg_reset()
{
    g_buffer_size = 0;
    g_current_stream_index = -1;

    if (g_scale_context) sws_freeContext(g_scale_context);
    if (g_raw_buffer_0) av_free(g_raw_buffer_0);
    if (g_raw_buffer) av_free(g_raw_buffer);
    if (g_frame_2) av_free(g_frame_2);
    if (g_frame) av_free(g_frame);
    if (g_codec_context) avcodec_close(g_codec_context);
    if (g_format_context) av_close_input_file(g_format_context);

    return 0;
}

int ffmpeg_deinitialize()
{
    return ffmpeg_reset();
}

int64_t ffmpeg_get_frame_count()
{
    if (g_current_stream_index >= 0)
    {
        return g_format_context->streams[g_current_stream_index]->nb_frames;
    }
    
    return 0;
}

float ffmpeg_get_frame_rate()
{
    if (g_current_stream_index >= 0)
    {
        float num = (float) g_format_context->streams[g_current_stream_index]->r_frame_rate.num;
        float denom = (float) g_format_context->streams[g_current_stream_index]->r_frame_rate.den;
        return (float) num / denom;
    }
    
    return 0.0f;
}

int ffmpeg_play_file(char * filename, int *format, int *width, int *height)
{
    unsigned int i = 0;
    g_current_stream_index = -1;

    if (av_open_input_file(&g_format_context, filename, NULL, 0, NULL) != 0)
    {
        printf("[FF] Failed to open file %s\n", filename);
        return -1;
    }

    if (av_find_stream_info(g_format_context) < 0)
    {
        printf("[FF] Failed to pull stream information from file %s\n", filename);
        ffmpeg_deinitialize();
        return -1;
    }

    dump_format(g_format_context, 0, filename, 0);

    // Now we have our list of streams, find the video and determine codec.
    for (i = 0; i < g_format_context->nb_streams; i++)
    {
        if (g_format_context->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO)
        {
            g_current_stream_index = i;
            break;
        }
    }

    if (g_current_stream_index == -1)
    {
        printf("[FF] Could not find a video stream in file %s\n", filename);
        ffmpeg_deinitialize();
        return -1;
    }
    else
    {
        printf("[FF] Detected video stream at index %i in file %s\n", g_current_stream_index, filename);
    }
    
    // Alias the codec pointer for easier access.
    g_codec_context = g_format_context->streams[g_current_stream_index]->codec;

    // Actually load our codec using the codec context information.
    g_codec = avcodec_find_decoder(g_codec_context->codec_id);

    if (g_codec == NULL)
    {
        printf("[FF] Failed to enumerate a proper decoder for file %s\n", filename);
        ffmpeg_deinitialize();
        return -1;
    }

    if (avcodec_open(g_codec_context, g_codec) < 0 || !g_codec_context)
    {
        printf("[FF] Failed to open the codec for file %s\n", filename);
        ffmpeg_deinitialize();
        return -1;
    }
        
    // Now we allocate buffer space for our video frames.
    g_frame = avcodec_alloc_frame();

    if (!g_frame)
    {
        printf("[FF] Failed to allocate a frame buffer\n");
        ffmpeg_deinitialize();
        return -1;
    }

    g_frame_2 = avcodec_alloc_frame();
    
    if (!g_frame_2)
    {
        printf("[FF] Failed to allocate a frame2 buffer\n");
        ffmpeg_deinitialize();
        return -1;
    }

    // Now allocate our raw buffer to hold video frames.
    g_buffer_size = avpicture_get_size(PIX_FMT_RGB24, g_codec_context->width, g_codec_context->height);

    if (g_buffer_size == 0)
    {
        printf("[FF] Error: buffer sizes of zero are not allowed\n");
        ffmpeg_deinitialize();
        return -1;
    }

    g_raw_buffer = (unsigned char *) av_malloc(g_buffer_size * sizeof(unsigned char));
    g_raw_buffer_0 = (unsigned char *) av_malloc(g_buffer_size * sizeof(unsigned char));

    if (!g_raw_buffer || !g_raw_buffer_0)
    {
        printf("[FF] Error allocating space for raw image buffers\n");
        ffmpeg_deinitialize();
        return -1;
    }

    printf("[FF] Buffer size %i requested\n", g_buffer_size);

    avpicture_fill( (AVPicture*) g_frame_2, g_raw_buffer, PIX_FMT_RGB24, g_codec_context->width, g_codec_context->height);
    avpicture_fill( (AVPicture*) g_frame, g_raw_buffer_0, PIX_FMT_YUV420P, g_codec_context->width, g_codec_context->height);

    (*format) = (int) g_codec_context->pix_fmt;
    (*width) = (int) g_codec_context->width;
    (*height) = (int) g_codec_context->height;

    g_scale_context = sws_getContext(g_codec_context->width, g_codec_context->height, g_codec_context->pix_fmt, g_codec_context->width, 
                                     g_codec_context->height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

    if (!g_scale_context)
    {
        printf("[FF] Error getting scale context!\n");
        ffmpeg_deinitialize();
        return -1;
    }

    return 0;
}

int ffmpeg_copy_current_frame(unsigned char *dest, int row_pitch)
{
    int r = 0;
    for (r = 0; r < g_codec_context->height; r++)
        memcpy(dest + (row_pitch * (g_codec_context->height - r - 1)), 
              ((AVPicture*) g_frame_2)->data[0] + r * g_frame_2->linesize[0], 
              g_codec_context->width * 3);

    return 0;
}

int ffmpeg_refresh(int *encoded_frame_size)
{
    int i = 0;
    int r = 0;
    int stride = 0;
    int icompleted = 0;
    AVPacket packet;

    // Verify that we have not been reset since the last frame.
    if (g_current_stream_index < 0 || g_buffer_size == 0)
    {
        return -1;
    }

    // Actually pull the frames and perform conversion.
    while (av_read_frame(g_format_context, &packet) >= 0) 
    {
        if (packet.stream_index == g_current_stream_index)
        {
            if (encoded_frame_size) 
            {
                *encoded_frame_size = packet.size;
            }

            while (!icompleted)
            {
                avcodec_decode_video(g_codec_context, g_frame, &icompleted, packet.data, packet.size);
            }

            if (icompleted)
            {          
				int i = 0;
                int ret = sws_scale(g_scale_context, g_frame->data, g_frame->linesize, 0, 
                                    g_codec_context->height, g_frame_2->data, g_frame_2->linesize );

                av_free_packet(&packet);
                return 0;
            }    
        }

        av_free_packet(&packet);
    }   

    return -1;
}

} // extern "C"