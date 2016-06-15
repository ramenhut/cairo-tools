
/*
// Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.
//
// evx_inspect.cpp
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

#if defined(EVX_PLATFORM_WINDOWS)
#include "time.h"
#include "gl/gl.h"
#include "glut/glut.h"
#elif defined(EVX_PLATFORM_MACOSX)
#include <sys/time.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <GLUT/glut.h>
#endif

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


typedef struct EVX_VIDEO_STATE
{
    bool state;
    
    uint32 frame_rate;
    uint32 frame_count;
    int32 frame_rate_mul;
    
} EVX_VIDEO_STATE;

image g_frame_image;
evx1_encoder *g_encoder;
bit_stream g_cairo_stream;

EVX_MEDIA_FILE_HEADER g_header = {0};
EVX_VIDEO_STATE g_video_state = {0};
EVX_PEEK_STATE g_current_peek_state = EVX_PEEK_SOURCE;

uint32 g_total_encoded_bytes = 0;
uint32 g_total_source_bytes_read = 0;
uint32 g_frame_texture = EVX_MAX_UINT32;
uint8 *g_temp_buffer = NULL;

uint64 _get_system_time_ms()
{
#if defined(EVX_PLATFORM_WINDOWS)
    return double(clock()) / CLOCKS_PER_SEC * 1000;
#elif defined(EVX_PLATFORM_MACOSX)
    timeval time;
    gettimeofday(&time, NULL);
    return (time.tv_sec * 1000) + (time.tv_usec / 1000);
#endif
}

uint32 _get_elapsed_time_ms(uint64 from_time)
{
    return (_get_system_time_ms() - from_time);
}

float _get_rate_multiplier()
{
    if (g_video_state.frame_rate_mul >= 0)
    {
        return g_video_state.frame_rate_mul + 1;
    }
    else
    {
        return 1.0f / (1 << evx::abs(g_video_state.frame_rate_mul));
    }
}

bool _should_update_video_frame()
{
    float rate_multiplier = _get_rate_multiplier();
    rate_multiplier = max(_get_rate_multiplier(), 0.1f);
    
    static uint64 g_last_time = _get_system_time_ms();
    uint32 required_ms = ((float) g_video_state.frame_rate / rate_multiplier);
    uint32 elapsed = _get_elapsed_time_ms(g_last_time);
    
    if (elapsed < required_ms)
    {
        return false;
    }

    g_last_time = _get_system_time_ms();
    
    return true;
}

char *_peek_name_from_index(uint8 index)
{
    switch (index)
    {
        case EVX_PEEK_SOURCE: return "EVX Inspect: Source";
        case EVX_PEEK_PREDICTION: return "EVX Inspect: Prediction";
        case EVX_PEEK_BLOCK_TABLE: return "EVX Inspect: Block table";
        case EVX_PEEK_QUANT_TABLE: return "EVX Inspect: Quantization table";
        case EVX_PEEK_SPMP_TABLE: return "EVX Inspect: Sub-pixel motion table";
        case EVX_PEEK_BLOCK_VARIANCE: return "EVX Inspect: Variance";   
        case EVX_PEEK_DESTINATION: return "EVX Inspect: Destination";       
    };

    return NULL;
}

void handle_key_press(unsigned char key, int x, int y) 
{
    if (key >= '1' && key <= '6')
    {
        g_current_peek_state = (EVX_PEEK_STATE) (key - '1');
        if (EVX_PEEK_PREDICTION == g_current_peek_state)
            g_current_peek_state = EVX_PEEK_DESTINATION;

        const char *peek_name = _peek_name_from_index(g_current_peek_state);
        evx_msg("Set %s", peek_name);
        glutSetWindowTitle(peek_name);
        return;
    }

    switch (key)
    {
        case 27: exit(0); return;
        case '=':
        case '+': g_video_state.frame_rate_mul++; break;
        case '_':
        case '-': g_video_state.frame_rate_mul--; break;
        case 'p': g_video_state.state = !g_video_state.state; break;
        default: return;
    };

    g_video_state.frame_rate_mul = min(g_video_state.frame_rate_mul, 9);
    g_video_state.frame_rate_mul = max(g_video_state.frame_rate_mul, -7);

    evx_msg("Playback state: %s, rate %.2fx",
        (g_video_state.state ? "paused" : "playing"), _get_rate_multiplier());
}

uint32 _get_file_size(FILE *f)
{
    uint32 file_size = 0;
    uint32 seek_pos = ftell(f);

    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, seek_pos, SEEK_SET);

    return file_size;
}

void _report_bit_rate()
{
    if (0 == (g_video_state.frame_count % g_video_state.frame_rate))
    {
        evx_msg("Source bitrate: %.2f Mbps, Cairo bitrate: %.2f Mbps", 
            (float) g_total_source_bytes_read / 1000000.0f, 
            (float) g_total_encoded_bytes / 1000000.0f);

        g_total_source_bytes_read = 0;
        g_total_encoded_bytes = 0;
    }
}

void _read_next_frame(image *output)
{
    if (!g_video_state.state)
    {
        // If we've completed all frames in the file, do nothing.
        if (g_header.frame_count && (g_video_state.frame_count >= g_header.frame_count))
        {
            return;
        }

        // If insufficient time has passed, do nothing.
        if (!_should_update_video_frame())
        {
            return;
        }

        int32 g_read_frame_size = 0;

        if (ffmpeg_refresh(&g_read_frame_size) < 0)
        {
            return;
        }

        ffmpeg_copy_current_frame(output->query_data(), output->query_row_pitch());

        g_total_source_bytes_read += g_read_frame_size;

        g_encoder->encode(output->query_data(), output->query_width(), output->query_height(), &g_cairo_stream);

        g_video_state.frame_count++;

        g_total_encoded_bytes += sizeof(EVX_MEDIA_FRAME_HEADER) + g_cairo_stream.query_byte_occupancy();

        if (0 == (g_video_state.frame_count % 10))
        {
            evx_msg("Processing frame %i", g_video_state.frame_count);
        }

        _report_bit_rate();
    }
   
    // Peek the appropriate frame and render it to our output.
    g_encoder->peek(g_current_peek_state, output->query_data());
    g_cairo_stream.empty();
}

void _prepare_frame_texture()
{
    if (EVX_MAX_UINT32 == g_frame_texture)
    {
        glGenTextures(1, &g_frame_texture);
        glBindTexture(GL_TEXTURE_2D, g_frame_texture);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g_header.frame_width, g_header.frame_height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, g_frame_image.query_data());
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_header.frame_width, g_header.frame_height, 
                        GL_RGB, GL_UNSIGNED_BYTE, g_frame_image.query_data());
    }
}

void render_scene()
{
    _read_next_frame(&g_frame_image);
    _prepare_frame_texture();

    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();

    if (g_frame_texture != EVX_MAX_UINT32)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_frame_texture);
    }

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
	glVertex3f(-1.0f,-1.0f, 0.0f);
    glTexCoord2f(0, 1);
	glVertex3f(-1.0f, 1.0f, 0.0);
    glTexCoord2f(1, 1);
	glVertex3f( 1.0f, 1.0f, 0.0);
    glTexCoord2f(1, 0);
    glVertex3f( 1.0f,-1.0f, 0.0);
	glEnd();

    glutSwapBuffers();
}

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
    int32 content_width = 0;
    int32 content_height = 0;
    int32 content_format = 0;

    evx_msg("Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.");

    if (3 != argc)
    {
        // No need to get fancy.
        evx_msg("Required syntax: inspect <input_filename> initial_quality");
        return 0;
    }

    ffmpeg_initialize();
    
    if (0 != ffmpeg_play_file(argv[1], (int*) &content_format, (int*) &content_width, (int*) &content_height))
    {
        evx_msg("Failed to open content file %s", argv[1]);
        ffmpeg_deinitialize();
        return 0;
    }

    _prepare_evx_header(&g_header, content_width, content_height);
   
    g_temp_buffer = new uint8[4 * EVX_MB];
    g_video_state.frame_rate = 1000 / g_header.frame_rate;

    create_image(EVX_IMAGE_FORMAT_R8G8B8, g_header.frame_width, g_header.frame_height, &g_frame_image);
    g_cairo_stream.resize_capacity((4*EVX_MB) << 3);
    create_encoder(&g_encoder);
    g_encoder->set_quality(atoi(argv[2]));

    glutInit(&argc, argv);
    glutInitWindowSize(g_header.frame_width, g_header.frame_height);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutCreateWindow("EVX Inspector");
    glutDisplayFunc(&render_scene);
    glutIdleFunc(&render_scene);
    glutKeyboardFunc(&handle_key_press);
    glutMainLoop();

    destroy_image(&g_frame_image);
    destroy_encoder(g_encoder);

    delete [] g_temp_buffer;
    ffmpeg_deinitialize();
    
    return 0;
}