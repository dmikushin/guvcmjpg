/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

/*******************************************************************************#
#                                                                               #
#  M/Jpeg decoding and frame capture taken from luvcview                        #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "gviewv4l2core.h"
#include "colorspaces.h"
#include "jpeg_decoder.h"
#include "gview.h"

/*h264 decoder (libavcodec)*/
#ifdef HAVE_FFMPEG_AVCODEC_H
#include <ffmpeg/avcodec.h>
#else
#include <libavcodec/avcodec.h>
#endif 

#define LIBAVCODEC_VER_AT_LEAST(major,minor)  (LIBAVCODEC_VERSION_MAJOR > major || \
                                              (LIBAVCODEC_VERSION_MAJOR == major && \
                                               LIBAVCODEC_VERSION_MINOR >= minor))

#if !LIBAVCODEC_VER_AT_LEAST(54,25)
	#define AV_CODEC_ID_H264 CODEC_ID_H264
#endif

#if !LIBAVCODEC_VER_AT_LEAST(54,25)
	#define AV_CODEC_ID_MJPEG CODEC_ID_MJPEG
#endif

typedef struct
{
	AVCodec* codec;
	AVCodecContext* context;
	AVFrame* picture;
}
codec_data_t;

typedef struct _jpeg_decoder_context_t
{
	codec_data_t codec_data;

	int width;
	int height;
	int pic_size;
	
	uint8_t* tmp_frame; //temp frame buffer	
}
jpeg_decoder_context_t;

static jpeg_decoder_context_t* jpeg_ctx = NULL;

/*
 * init (m)jpeg decoder context
 * args:
 *    width - image width
 *    height - image height
 *
 * asserts:
 *    none
 *
 * returns: error code (0 - E_OK)
 */
int jpeg_init_decoder(int width, int height)
{
#if !LIBAVCODEC_VER_AT_LEAST(53,34)
	avcodec_init();
#endif
	/*
	 * register all the codecs (we can also register only the codec
	 * we wish to have smaller code)
	 */
	avcodec_register_all();
	av_log_set_level(AV_LOG_PANIC);

	if (jpeg_ctx != NULL)
		jpeg_close_decoder();

	jpeg_ctx = calloc(1, sizeof(jpeg_decoder_context_t));
	if (jpeg_ctx == NULL)
	{
		fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (jpeg_init_decoder): %s\n", strerror(errno));
		exit(-1);
	}
	
	codec_data_t* codec_data = &jpeg_ctx->codec_data;
	codec_data->codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
	if (!codec_data->codec)
	{
		fprintf(stderr, "V4L2_CORE: (mjpeg decoder) codec not found\n");
		free(jpeg_ctx);
		jpeg_ctx = NULL;
		return E_NO_CODEC;
	}

#if LIBAVCODEC_VER_AT_LEAST(53,6)
	codec_data->context = avcodec_alloc_context3(codec_data->codec);
	avcodec_get_context_defaults3 (codec_data->context, codec_data->codec);
#else
	codec_data->context = avcodec_alloc_context();
	avcodec_get_context_defaults(codec_data->context);
#endif
	if (codec_data->context == NULL)
	{
		fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (h264_init_decoder): %s\n", strerror(errno));
		exit(-1);
	}

	codec_data->context->pix_fmt = AV_PIX_FMT_YUV422P;
	codec_data->context->width = width;
	codec_data->context->height = height;
	//jpeg_ctx->context->dsp_mask = (FF_MM_MMX | FF_MM_MMXEXT | FF_MM_SSE);

#if LIBAVCODEC_VER_AT_LEAST(53,6)
	if (avcodec_open2(codec_data->context, codec_data->codec, NULL) < 0)
#else
	if (avcodec_open(codec_data->context, codec_data->codec) < 0)
#endif
	{
		fprintf(stderr, "V4L2_CORE: (mjpeg decoder) couldn't open codec\n");
		avcodec_close(codec_data->context);
		free(codec_data->context);
		free(jpeg_ctx);
		jpeg_ctx = NULL;
		return E_NO_CODEC;
	}

#if LIBAVCODEC_VER_AT_LEAST(55,28)
	codec_data->picture = av_frame_alloc();
	av_frame_unref(codec_data->picture);
#else
	codec_data->picture = avcodec_alloc_frame();
	avcodec_get_frame_defaults(codec_data->picture);
#endif

	/*alloc temp buffer*/
	jpeg_ctx->tmp_frame = calloc(width*height*2, sizeof(uint8_t));
	if (jpeg_ctx->tmp_frame == NULL)
	{
		fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (jpeg_init_decoder): %s\n", strerror(errno));
		exit(-1);
	}
	
	jpeg_ctx->pic_size = avpicture_get_size(codec_data->context->pix_fmt, width, height);
	jpeg_ctx->width = width;
	jpeg_ctx->height = height;

	return E_OK;
}

/*
 * decode (m)jpeg frame
 * args:
 *    out_buf - pointer to decoded data
 *    in_buf - pointer to h264 data
 *    size - in_buf size
 *
 * asserts:
 *    jpeg_ctx is not null
 *    in_buf is not null
 *    out_buf is not null
 *
 * returns: decoded data size
 */
int jpeg_decode(uint8_t* out_buf, uint8_t* in_buf, int size)
{
	/*asserts*/
	assert(jpeg_ctx != NULL);
	assert(in_buf != NULL);
	assert(out_buf != NULL);

	AVPacket avpkt;

	av_init_packet(&avpkt);
	 	
	avpkt.size = size;
	avpkt.data = in_buf;
	
	codec_data_t* codec_data = &jpeg_ctx->codec_data;

	int got_picture = 0;
	int len = avcodec_decode_video2(codec_data->context, codec_data->picture, &got_picture, &avpkt);

	if (len < 0)
	{
		fprintf(stderr, "V4L2_CORE: (jpeg decoder) error while decoding frame\n");
		return len;
	}

	if (got_picture)
	{
		avpicture_layout((AVPicture*)codec_data->picture, codec_data->context->pix_fmt, 
			jpeg_ctx->width, jpeg_ctx->height, jpeg_ctx->tmp_frame, jpeg_ctx->pic_size);
		/* libavcodec output is in yuv422p */
#ifdef USE_PLANAR_YUV
        yuv422p_to_yu12(out_buf, jpeg_ctx->tmp_frame, jpeg_ctx->width, jpeg_ctx->height);
#else
		yuv422_to_yuyv(out_buf, jpeg_ctx->tmp_frame, jpeg_ctx->width, jpeg_ctx->height);
#endif		
		return jpeg_ctx->pic_size;
	}

	return 0;
}

/*
 * close (m)jpeg decoder context
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void jpeg_close_decoder()
{
	if (jpeg_ctx == NULL)
		return;
		
	codec_data_t* codec_data = &jpeg_ctx->codec_data;

	avcodec_close(codec_data->context);
	free(codec_data->context);

#if LIBAVCODEC_VER_AT_LEAST(55,28)
	av_frame_free(&codec_data->picture);
#else
	#if LIBAVCODEC_VER_AT_LEAST(54,28)
			avcodec_free_frame(&codec_data->picture);
	#else
			av_freep(&codec_data->picture);
	#endif
#endif

	if (jpeg_ctx->tmp_frame)
		free(jpeg_ctx->tmp_frame);
		
	free(jpeg_ctx);

	jpeg_ctx = NULL;
}

