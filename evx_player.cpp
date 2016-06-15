
/*
// Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.
//
// evx_player.cpp
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

typedef struct EVX_VIDEO_STATE
{
    bool state;
    
    uint32 frame_rate;
    uint32 frame_count;
    int32 frame_rate_mul;
    
} EVX_VIDEO_STATE;

image g_frame_image;
evx1_decoder *g_decoder;
bit_stream g_cairo_stream;

EVX_MEDIA_FILE_HEADER g_header = {0};
EVX_VIDEO_STATE g_video_state = {0};

uint32 g_recent_bits_read = 0;
uint32 g_source_file_size = 0;
uint32 g_frame_texture = EVX_MAX_UINT32;

FILE *g_source_file = NULL;
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

void handle_key_press(unsigned char key, int x, int y) 
{
    switch (key)
    {
        case 27: exit(0); return;
        case '=':
        case '+': g_video_state.frame_rate_mul++; break;
        case '_':
        case '-': g_video_state.frame_rate_mul--; break;
        case 'p': g_video_state.state = !g_video_state.state; break;
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
        evx_msg("Average bitrate: %.2f Mbps", (float) g_recent_bits_read / 1000000.0f);
        g_recent_bits_read = 0;
    }
}

void _read_next_frame(image *output)
{
    EVX_MEDIA_FRAME_HEADER frame_header;

    // If we've completed all frames in the file, do nothing.
    if (g_header.frame_count && (g_video_state.frame_count >= g_header.frame_count))
    {
        return;
    }

    // If there is nothing left to read in the file, do nothing.
    if (ftell(g_source_file) >= g_source_file_size)
    {
        return;
    }

    // If insufficient time has passed, do nothing.
    if (!_should_update_video_frame())
    {
        return;
    }

    // Pull the next frame from the file and decode it.
    fread(&frame_header, sizeof(frame_header), 1, g_source_file);
    fread(g_temp_buffer, frame_header.frame_size, 1, g_source_file);

    g_cairo_stream.empty();
    g_cairo_stream.write_bytes(g_temp_buffer, frame_header.frame_size);
    g_decoder->decode(&g_cairo_stream, output->query_data());
    g_recent_bits_read += frame_header.header_size + frame_header.frame_size;
    g_video_state.frame_count++;

    _report_bit_rate();
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

void _render_progress_bar()
{
    float percentage = (float) ftell(g_source_file) / g_source_file_size;
    percentage = min(percentage, 1.0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
 
    glBegin(GL_QUADS);
    glColor4f(1, 1, 1, 1);
	glVertex3f(-1.0f,-1.0f, 0.0f);

    glColor4f(0.5, 0.5, 0.5, 0.5);
	glVertex3f(-1.0f,-0.985f,0.0);

    glColor4f(0.5, 0.5, 0.5, 0.5);
	glVertex3f(-1.0f + 2.0 * percentage,-0.985f,0.0);

    glColor4f(1, 1, 1, 1);
    glVertex3f(-1.0f + 2.0 * percentage,-1.0f, 0.0);
	glEnd();

    glDisable(GL_BLEND);
}

void render_scene()
{
    if (!g_video_state.state)
    {
        _read_next_frame(&g_frame_image);
        _prepare_frame_texture();
    }

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

    _render_progress_bar();

    glutSwapBuffers();
}

void _print_file_header(const EVX_MEDIA_FILE_HEADER &header)
{
    evx_msg("Printing file header:");
    evx_msg("size = %i", header.header_size);
    evx_msg("version = %i", header.version);
    evx_msg("width = %i", header.frame_width);
    evx_msg("height = %i", header.frame_height);
    evx_msg("frame count = %llu", header.frame_count);
    evx_msg("rate = %f", header.frame_rate);
}

int32 _check_header_format(const EVX_MEDIA_FILE_HEADER &header)
{
    if (1 != header.version ||
        0 == header.frame_rate ||
        sizeof(header) != header.header_size)
    {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    evx_msg("Copyright (c) 2010-2014 Joe Bertolami. All Right Reserved.");

    if (2 != argc)
    {
        evx_msg("Required syntax: player <video filename>");
        return 0;
    }

    g_source_file = fopen(argv[1], "rb");

    if (!g_source_file)
    {
        evx_msg("Error opening source file %s", argv[1]);
        return 0;
    }

    g_source_file_size = _get_file_size(g_source_file);
    
    fread(&g_header, sizeof(g_header), 1, g_source_file);

    if (_check_header_format(g_header) < 0)
    {
        fclose(g_source_file);
        return 0;
    }

    _print_file_header(g_header);

    g_temp_buffer = new uint8[4 * EVX_MB];
    g_cairo_stream.resize_capacity((4*EVX_MB) << 3);
    g_video_state.frame_rate = 1000 / g_header.frame_rate;

    create_image(EVX_IMAGE_FORMAT_R8G8B8, g_header.frame_width, g_header.frame_height, &g_frame_image);
    create_decoder(&g_decoder);

    glutInit(&argc, argv);
    glutInitWindowSize(g_header.frame_width, g_header.frame_height);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
    glutCreateWindow("EVX Reference Player");
    glutDisplayFunc(&render_scene);
    glutIdleFunc(&render_scene);
    glutKeyboardFunc(&handle_key_press);
    glutMainLoop();

    destroy_image(&g_frame_image);
    destroy_decoder(g_decoder);
    fclose(g_source_file);

    delete [] g_temp_buffer;

    return 0; 
}