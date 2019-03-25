/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#           Flemming Frandsen <dren.dk@gmail.com>                               #
#                             Add VU meter OSD                                  #
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
#  Encoder library                                                                #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <inttypes.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <assert.h>
/* support for internationalization - i18n */
#include <locale.h>
#include <libintl.h>

#include "gviewencoder.h"
#include "encoder.h"
#include "stream_io.h"
#include "gview.h"
#include "config.h"

#if LIBAVUTIL_VER_AT_LEAST(52,2)
#include <libavutil/channel_layout.h>
#endif

/*video buffer data mutex*/
static __MUTEX_TYPE mutex = __STATIC_MUTEX_INIT;
#define __PMUTEX &mutex

static int valid_video_codecs = 0;
static int valid_audio_codecs = 0;

static int64_t last_video_pts = 0;
static int64_t last_audio_pts = 0;
static int64_t reference_pts  = 0;

static int video_frame_max_size = 0;

static int video_ring_buffer_size = 0;
static video_buffer_t *video_ring_buffer = NULL;
static int video_read_index = 0;
static int video_write_index = 0;
static int video_scheduler = 0;

/*
 * set verbosity
 * args:
 *   value - verbosity value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void encoder_set_verbosity(int value)
{
	verbosity = value;
}

/*
 * convert yuyv to yuv420p
 * args:
 *    encoder_ctx - pointer to encoder context
 *    inp - input data (yuyv)
 *
 * asserts:
 *    encoder_ctx is not null
 *    encoder_ctx->enc_video_ctx is not null
 *     encoder_ctx->enc_video_ctx->tmpbuf is not null
 *
 * returns: none
 */
static void yuv422to420p(encoder_context_t *encoder_ctx, uint8_t *inp)
{
	/*assertions*/
	assert(encoder_ctx != NULL);
	assert(encoder_ctx->enc_video_ctx != NULL);
	assert( encoder_ctx->enc_video_ctx->tmpbuf != NULL);

	encoder_codec_data_t *video_codec_data = (encoder_codec_data_t *) encoder_ctx->enc_video_ctx->codec_data;

	assert(video_codec_data);

	int i,j;
	int linesize= encoder_ctx->video_width * 2;
	int size = encoder_ctx->video_width * encoder_ctx->video_height;

	uint8_t *y;
	uint8_t *y1;
	uint8_t *u;
	uint8_t *v;
	y = encoder_ctx->enc_video_ctx->tmpbuf;
	y1 = encoder_ctx->enc_video_ctx->tmpbuf + encoder_ctx->video_width;
	u = encoder_ctx->enc_video_ctx->tmpbuf + size;
	v = u + size/4;

	for(j = 0; j < (encoder_ctx->video_height - 1); j += 2)
	{
		for(i = 0; i < (linesize - 3); i += 4)
		{
			*y++ = inp[i+j*linesize];
			*y++ = inp[i+2+j*linesize];
			*y1++ = inp[i+(j+1)*linesize];
			*y1++ = inp[i+2+(j+1)*linesize];
			*u++ = (inp[i+1+j*linesize] + inp[i+1+(j+1)*linesize])>>1; // div by 2
			*v++ = (inp[i+3+j*linesize] + inp[i+3+(j+1)*linesize])>>1;
		}
		y += encoder_ctx->video_width;
		y1 += encoder_ctx->video_width;//2 lines
	}

	prepare_video_frame(video_codec_data, encoder_ctx->enc_video_ctx->tmpbuf, encoder_ctx->video_width, encoder_ctx->video_height);
}

/*
 * allocate video ring buffer
 * args:
 *   video_width - video frame width (in pixels)
 *   video_height - video frame height (in pixels)
 *   fps_den - frames per sec (denominator)
 *   fps_num - frames per sec (numerator)
 *   codec_ind - video codec index (0 -raw)
 *
 * asserts:
 *   none
 *
 * returns: none
 */
static void encoder_alloc_video_ring_buffer(
	int video_width,
	int video_height,
	int fps_den,
	int fps_num,
	int codec_ind)
{
	video_ring_buffer_size = (fps_den * 3) / (fps_num * 2); /* 1.5 sec */
	if(video_ring_buffer_size < 20)
		video_ring_buffer_size = 20; /*at least 20 frames buffer*/
	video_ring_buffer = calloc(video_ring_buffer_size, sizeof(video_buffer_t));
	if(video_ring_buffer == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_alloc_video_ring_buffer): %s\n", strerror(errno));
		exit(-1);
	}

	if(codec_ind > 0)
	{
#ifdef USE_PLANAR_YUV
	video_frame_max_size = (video_width * video_height * 3) / 2;
#else
	/*Max: (yuyv) 2 bytes per pixel*/
	video_frame_max_size = video_width * video_height * 2;
#endif
	}
	else
		video_frame_max_size = video_width * video_height * 3; //RGB formats
		
	int i = 0;
	for(i = 0; i < video_ring_buffer_size; ++i)
	{
		video_ring_buffer[i].frame = calloc(video_frame_max_size, sizeof(uint8_t));
		if(video_ring_buffer[i].frame == NULL)
		{
			fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_alloc_video_ring_buffer): %s\n", strerror(errno));
			exit(-1);
		}
		video_ring_buffer[i].flag = VIDEO_BUFF_FREE;
	}
}

/*
 * clean video ring buffer
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
static void encoder_clean_video_ring_buffer()
{
	if(!video_ring_buffer)
		return;

	int i = 0;
	for(i = 0; i < video_ring_buffer_size; ++i)
	{
		/*Max: (yuyv) 2 bytes per pixel*/
		free(video_ring_buffer[i].frame);
	}
	free(video_ring_buffer);
	video_ring_buffer = NULL;
}

/*
 * video encoder initialization for raw input
 *  (don't set a codec but set the proper codec 4cc)
 * args:
 *   enc_video_ctx - pointer to video context
 *   video_defaults - pointer to video codec default data
 *   video_width - video frame width
 *   video_height - video frame height
 *
 * asserts:
 *   encoder_ctx is not null
 *   encoder_ctx->enc_video_ctx is not null
 *
 * returns: none
 */
static void encoder_set_raw_video_input(
    encoder_context_t *encoder_ctx,
    video_codec_t *video_defaults
    )
{
	//assertions
	assert(encoder_ctx != NULL);
	assert(encoder_ctx->enc_video_ctx != NULL);

	encoder_ctx->video_codec_ind = 0;

	switch(encoder_ctx->input_format)
	{
		case V4L2_PIX_FMT_MJPEG:
			strncpy(video_defaults->compressor, "MJPG", 5);
			video_defaults->mkv_4cc = v4l2_fourcc('M','J','P','G');
			strncpy(video_defaults->mkv_codec, "V_MS/VFW/FOURCC", 25);
			encoder_ctx->enc_video_ctx->outbuf_size = encoder_ctx->video_width * encoder_ctx->video_height / 2;
			encoder_ctx->enc_video_ctx->outbuf = calloc(encoder_ctx->enc_video_ctx->outbuf_size, sizeof(uint8_t));
			if(encoder_ctx->enc_video_ctx->outbuf == NULL)
			{
				fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
				exit(-1);
			}
			break;

		case V4L2_PIX_FMT_H264:
			strncpy(video_defaults->compressor, "H264", 5);
			video_defaults->mkv_4cc = v4l2_fourcc('H','2','6','4');
			strncpy(video_defaults->mkv_codec, "V_MPEG4/ISO/AVC", 25);
			encoder_ctx->enc_video_ctx->outbuf_size = encoder_ctx->video_width * encoder_ctx->video_height / 2;
			encoder_ctx->enc_video_ctx->outbuf = calloc(encoder_ctx->enc_video_ctx->outbuf_size, sizeof(uint8_t));
			if(encoder_ctx->enc_video_ctx->outbuf == NULL)
			{
				fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
				exit(-1);
			}
			break;

		default:
		{
			char fourcc[5];
			fourcc[0]= (char) encoder_ctx->input_format & 0xFF;
			fourcc[1]= (char) encoder_ctx->input_format & 0xFF00 >> 8;
			fourcc[2]= (char) encoder_ctx->input_format & 0xFF0000 >> 16;
			fourcc[3]= (char) encoder_ctx->input_format & 0xFF000000 >> 24;
			fourcc[4]='\0';

			strncpy(video_defaults->compressor, fourcc, 5);
			video_defaults->mkv_4cc = encoder_ctx->input_format; //v4l2_fourcc('Y','U','Y','2')
			strncpy(video_defaults->mkv_codec, "V_MS/VFW/FOURCC", 25);
			encoder_ctx->enc_video_ctx->outbuf_size = encoder_ctx->video_width * encoder_ctx->video_height * 3; //max of 3 bytes per pixel
			encoder_ctx->enc_video_ctx->outbuf = calloc(encoder_ctx->enc_video_ctx->outbuf_size, sizeof(uint8_t));
			if(encoder_ctx->enc_video_ctx->outbuf == NULL)
			{
				fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
				exit(-1);
			}
			break;
		}
	}
}

/*
 * video encoder initialization
 * args:
 *   encoder_ctx - pointer to encoder context
 *
 * asserts:
 *   encoder_ctx is not null
 *
 * returns: pointer to encoder video context (NULL on none)
 */
static encoder_video_context_t *encoder_video_init(encoder_context_t *encoder_ctx)
{
	//assertions
	assert(encoder_ctx != NULL);

	if(encoder_ctx->video_codec_ind < 0)
	{
		if(verbosity > 0)
			printf("ENCODER: no video codec set - using raw (direct input)\n");

		encoder_ctx->video_codec_ind = 0;
	}

	video_codec_t *video_defaults = encoder_get_video_codec_defaults(encoder_ctx->video_codec_ind);

	if(!video_defaults)
	{
		fprintf(stderr, "ENCODER: defaults for video codec index %i not found: using raw (direct input)\n",
			encoder_ctx->video_codec_ind);
		encoder_ctx->video_codec_ind = 0;
		video_defaults = encoder_get_video_codec_defaults(encoder_ctx->video_codec_ind);
		if(!video_defaults)
		{
			/*should never happen*/
			fprintf(stderr, "ENCODER: defaults for raw video not found\n");
			return NULL;
		}
	}

	encoder_video_context_t *enc_video_ctx = calloc(1, sizeof(encoder_video_context_t));
	if(enc_video_ctx == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
		exit(-1);
	}

	/* make sure enc_video_ctx is set in encoder_ctx */
	encoder_ctx->enc_video_ctx = enc_video_ctx;

	if(encoder_ctx->video_codec_ind == 0)
	{
		encoder_set_raw_video_input(encoder_ctx, video_defaults);
		return (enc_video_ctx);
	}

	/*
	 * alloc the video codec data 
	 */
	encoder_codec_data_t *video_codec_data = calloc(1, sizeof(encoder_codec_data_t));
	if(video_codec_data == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
		exit(-1);
	}
	/*
	 * find the video encoder
	 *   try specific codec (by name)
	 */
	video_codec_data->codec = avcodec_find_encoder_by_name(video_defaults->codec_name);
	/*if it fails try any codec with matching AV_CODEC_ID*/
	if(!video_codec_data->codec)
		video_codec_data->codec = avcodec_find_encoder(video_defaults->codec_id);

	if(!video_codec_data->codec)
	{
		/*we will use raw data so free the codec data*/
		free(video_codec_data);
		fprintf(stderr, "ENCODER: libav video codec (%i) not found - using raw input\n",video_defaults->codec_id);
		video_defaults = encoder_get_video_codec_defaults(0);
		encoder_set_raw_video_input(encoder_ctx, video_defaults);
		return (enc_video_ctx);
	}

#if LIBAVCODEC_VER_AT_LEAST(53,6)

	video_codec_data->codec_context = avcodec_alloc_context3(video_codec_data->codec);

	avcodec_get_context_defaults3 (
			video_codec_data->codec_context,
			video_codec_data->codec);
#else
	video_codec_data->codec_context = avcodec_alloc_context();
#endif

	if(video_codec_data->codec_context == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
		exit(-1);
	}

	/*set codec defaults*/
	video_codec_data->codec_context->bit_rate = video_defaults->bit_rate;
	video_codec_data->codec_context->width = encoder_ctx->video_width;
	video_codec_data->codec_context->height = encoder_ctx->video_height;

	video_codec_data->codec_context->flags |= video_defaults->flags;
	if (video_defaults->num_threads > 0)
		video_codec_data->codec_context->thread_count = video_defaults->num_threads;
	/*
	 * mb_decision:
	 * 0 (FF_MB_DECISION_SIMPLE) Use mbcmp (default).
	 * 1 (FF_MB_DECISION_BITS)   Select the MB mode which needs the fewest bits (=vhq).
	 * 2 (FF_MB_DECISION_RD)     Select the MB mode which has the best rate distortion.
	 */
	video_codec_data->codec_context->mb_decision = video_defaults->mb_decision;
	/*use trellis quantization*/
	video_codec_data->codec_context->trellis = video_defaults->trellis;

	/*motion estimation method (epzs)*/
	video_codec_data->codec_context->me_method = video_defaults->me_method;

	video_codec_data->codec_context->dia_size = video_defaults->dia;
	video_codec_data->codec_context->pre_dia_size = video_defaults->pre_dia;
	video_codec_data->codec_context->pre_me = video_defaults->pre_me;
	video_codec_data->codec_context->me_pre_cmp = video_defaults->me_pre_cmp;
	video_codec_data->codec_context->me_cmp = video_defaults->me_cmp;
	video_codec_data->codec_context->me_sub_cmp = video_defaults->me_sub_cmp;
	video_codec_data->codec_context->me_subpel_quality = video_defaults->subq; //NEW
	video_codec_data->codec_context->refs = video_defaults->framerefs;         //NEW
	video_codec_data->codec_context->last_predictor_count = video_defaults->last_pred;

	video_codec_data->codec_context->mpeg_quant = video_defaults->mpeg_quant; //h.263
	video_codec_data->codec_context->qmin = video_defaults->qmin;             // best detail allowed - worst compression
	video_codec_data->codec_context->qmax = video_defaults->qmax;             // worst detail allowed - best compression
	video_codec_data->codec_context->max_qdiff = video_defaults->max_qdiff;
	video_codec_data->codec_context->max_b_frames = video_defaults->max_b_frames;

	video_codec_data->codec_context->qcompress = video_defaults->qcompress;
	video_codec_data->codec_context->qblur = video_defaults->qblur;
	video_codec_data->codec_context->strict_std_compliance = FF_COMPLIANCE_NORMAL;
	video_codec_data->codec_context->codec_id = video_defaults->codec_id;

#if !LIBAVCODEC_VER_AT_LEAST(53,0)
#define AVMEDIA_TYPE_VIDEO CODEC_TYPE_VIDEO
#endif
	video_codec_data->codec_context->codec_type = AVMEDIA_TYPE_VIDEO;

	video_codec_data->codec_context->pix_fmt =  video_defaults->pix_fmt; //only yuv420p available for mpeg
	if(video_defaults->fps)
		video_codec_data->codec_context->time_base = (AVRational){1, video_defaults->fps}; //use codec properties fps
	else if (encoder_ctx->fps_den >= 5)
		video_codec_data->codec_context->time_base = (AVRational){encoder_ctx->fps_num, encoder_ctx->fps_den}; //default fps (for gspca this is 1/1)
	else
		video_codec_data->codec_context->time_base = (AVRational){1,15}; //fallback to 15 fps (e.g gspca)

    if(video_defaults->gop_size > 0)
        video_codec_data->codec_context->gop_size = video_defaults->gop_size;
    else
        video_codec_data->codec_context->gop_size = video_codec_data->codec_context->time_base.den;

	if(video_defaults->codec_id == AV_CODEC_ID_H264)
	{
	   video_codec_data->codec_context->me_range = 16;
	    //the first compressed frame will be empty (1 frame out of sync)
	    //but avoids x264 warning on lookaheadless mb-tree
#if LIBAVCODEC_VER_AT_LEAST(53,6)
	    av_dict_set(&video_codec_data->private_options, "rc_lookahead", "1", 0);
#else
	    video_codec_data->codec_context->rc_lookahead=1;
#endif
	}

	/* open codec*/
#if LIBAVCODEC_VER_AT_LEAST(53,6)
	if (avcodec_open2(
		video_codec_data->codec_context,
		video_codec_data->codec,
		&video_codec_data->private_options) < 0)
#else
	if (avcodec_open(
		video_codec_data->codec_context,
		video_codec_data->codec) < 0)
#endif
	{
		fprintf(stderr, "ENCODER: could not open video codec (%s) - using raw input\n", video_defaults->codec_name);
		free(video_codec_data->codec_context);
		video_codec_data->codec_context = NULL;
		video_codec_data->codec = 0;
		/*we will use raw data so free the codec data*/
		free(video_codec_data);
		video_defaults = encoder_get_video_codec_defaults(0);
		encoder_set_raw_video_input(encoder_ctx, video_defaults);
		return (enc_video_ctx);
	}

#if LIBAVCODEC_VER_AT_LEAST(55,28)
	video_codec_data->frame = av_frame_alloc();
#else
	video_codec_data->frame = avcodec_alloc_frame();
#endif
	if(video_codec_data->frame == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
		exit(-1);
	}
	video_codec_data->frame->pts = 0;

	/*set the codec data in codec context*/
	enc_video_ctx->codec_data = (void *) video_codec_data;

#ifdef USE_PLANAR_YUV
	enc_video_ctx->tmpbuf = NULL; //no need to temp buffer input already in yu12 (yuv420p)
#else
	//alloc tmpbuff (yuv420p)
	enc_video_ctx->tmpbuf = calloc((encoder_ctx->video_width * encoder_ctx->video_height * 3)/2, sizeof(uint8_t));
	if(enc_video_ctx->tmpbuf == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
		exit(-1);
	}
#endif
	//alloc outbuf
	enc_video_ctx->outbuf_size = 240000;//1792
	enc_video_ctx->outbuf = calloc(enc_video_ctx->outbuf_size, sizeof(uint8_t));
	if(enc_video_ctx->outbuf == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_video_init): %s\n", strerror(errno));
		exit(-1);
	}

	enc_video_ctx->delayed_frames = 0;
	enc_video_ctx->index_of_df = -1;

	enc_video_ctx->flushed_buffers = 0;
	enc_video_ctx->flush_delayed_frames = 0;
	enc_video_ctx->flush_done = 0;

	return (enc_video_ctx);
}

/*
 * get an estimated write loop sleep time to avoid a ring buffer overrun
 * args:
 *   mode: scheduler mode:
 *      0 - linear funtion; 1 - exponencial funtion
 *   thresh: ring buffer threshold in wich scheduler becomes active:
 *      [0.2 (20%) - 0.9 (90%)]
 *   max_time - maximum scheduler time (in ms)
 *
 * asserts:
 *   none
 *
 * returns: estimate sleep time (nanosec)
 */
uint32_t encoder_buff_scheduler(int mode, double thresh, int max_time)
{
	int diff_ind = 0;
	uint32_t sched_time = 0; /*in milisec*/

	__LOCK_MUTEX( __PMUTEX );
	/* try to balance buffer overrun in read/write operations */
	if(video_write_index >= video_read_index)
		diff_ind = video_write_index - video_read_index;
	else
		diff_ind = (video_ring_buffer_size - video_read_index) + video_write_index;
	__UNLOCK_MUTEX( __PMUTEX );

	/*clip ring buffer threshold*/
	if(thresh < 0.2)
		thresh = 0.2; /*20% full*/
	if(thresh > 0.9)
		thresh = 0.9; /*90% full*/

	int th = (int) lround((double) video_ring_buffer_size * thresh);

	if (diff_ind >= th)
	{
		switch(mode)
		{
			case ENCODER_SCHED_LIN: /*linear function*/
				sched_time = (uint32_t) lround((double) (diff_ind - th) * (max_time/(video_ring_buffer_size - th)));
				break;

			case ENCODER_SCHED_EXP: /*exponencial*/
			{
				double exp = (double) log10(max_time)/log10(video_ring_buffer_size - th);
				if(exp > 0)
					sched_time = (uint32_t) lround( pow(diff_ind - th, exp));
				else /*use linear function*/
					sched_time = (uint32_t) lround((double) (diff_ind - th) * (max_time/(video_ring_buffer_size - th)));
				break;
			}

		}
	}

	if(verbosity > 2)
		printf("ENCODER: scheduler %i ms (index delta %i)\n", sched_time, diff_ind);

	/*clip*/
	if(sched_time < 0) sched_time = 0; /*clip to positive values just in case*/
	if(sched_time > 1000)
		sched_time = 1000; /*1 sec max*/

	return (sched_time * 1E6); /*return in nanosec*/
}

/*
 * encoder initaliztion
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void encoder_init()
{

#if !LIBAVCODEC_VER_AT_LEAST(53,34)
	avcodec_init();
#endif
	/* register all the codecs (you can also register only the codec
	 * you wish to have smaller code)
	 */
	avcodec_register_all();

	valid_video_codecs = encoder_set_valid_video_codec_list ();
}

/*
 * get valid video codec count
 * args:
 *   none
 *
 * asserts:
 *    none
 *
 * returns: number of valid video codecs
 */
int encoder_get_valid_video_codecs()
{
	return valid_video_codecs;
}

/*
 * initialize and get the encoder context
 * args:
 *   input_format - input v4l2 format (yuyv for encoding)
 *   video_codec_ind - video codec list index
 *   muxer_id - file muxer:
 *        ENCODER_MUX_MKV; ENCODER_MUX_WEBM; ENCODER_MUX_AVI
 *   video_width - video frame width
 *   video_height - video frame height
 *   fps_num - fps numerator
 *   fps_den - fps denominator
 *   audio_channels- audio channels
 *   audio_samprate- audio sample rate
 *
 * asserts:
 *   none
 *
 * returns: pointer to encoder context (NULL on error)
 */
encoder_context_t *encoder_get_context(
	int input_format,
	int video_codec_ind,
	int muxer_id,
	int video_width,
	int video_height,
	int fps_num,
	int fps_den,
	int audio_channels,
	int audio_samprate)
{
	encoder_context_t *encoder_ctx = calloc(1, sizeof(encoder_context_t));

	if(encoder_ctx == NULL)
	{
		fprintf(stderr, "ENCODER: FATAL memory allocation failure (encoder_get_context): %s\n", strerror(errno));
		exit(-1);
	}

	encoder_ctx->input_format = input_format;

	encoder_ctx->video_codec_ind = video_codec_ind;

	encoder_ctx->muxer_id = muxer_id;

	encoder_ctx->video_width = video_width;
	encoder_ctx->video_height = video_height;

	encoder_ctx->fps_num = fps_num;
	encoder_ctx->fps_den = fps_den;

	encoder_ctx->audio_channels = audio_channels;
	encoder_ctx->audio_samprate = audio_samprate;

	/******************* video **********************/
	encoder_video_init(encoder_ctx);

	encoder_ctx->audio_channels = 0; /*no audio*/

	/****************** ring buffer *****************/
	encoder_alloc_video_ring_buffer(
		video_width,
		video_height,
		fps_den,
		fps_num,
		video_codec_ind);

	return encoder_ctx;
}

/*
 * store unprocessed input video frame in video ring buffer
 * args:
 *   frame - pointer to unprocessed frame data
 *   size - frame size (in bytes)
 *   timestamp - frame timestamp (in nanosec)
 *   isKeyframe - flag if it's a key(IDR) frame
 *
 * asserts:
 *   none
 *
 * returns: error code
 */
int encoder_add_video_frame(uint8_t *frame, int size, int64_t timestamp, int isKeyframe)
{
	if(!video_ring_buffer)
		return -1;

	if (reference_pts == 0)
	{
		reference_pts = timestamp; /*first frame ts*/
		if(verbosity > 0)
			printf("ENCODER: ref ts = %" PRId64 "\n", timestamp);
	}

	int64_t pts = timestamp - reference_pts;

	__LOCK_MUTEX( __PMUTEX );
	int flag = video_ring_buffer[video_write_index].flag;
	__UNLOCK_MUTEX( __PMUTEX );

	if(flag != VIDEO_BUFF_FREE)
	{
		fprintf(stderr, "ENCODER: video ring buffer full - dropping frame\n");
		return -1;
	}

	/*clip*/
	if(size > video_frame_max_size)
	{
		fprintf(stderr, "ENCODER: frame (%i bytes) larger than buffer (%i bytes): clipping\n",
			size, video_frame_max_size);

		size = video_frame_max_size;
	}
	memcpy(video_ring_buffer[video_write_index].frame, frame, size);
	video_ring_buffer[video_write_index].frame_size = size;
	video_ring_buffer[video_write_index].timestamp = pts;
	video_ring_buffer[video_write_index].keyframe = isKeyframe;

	__LOCK_MUTEX( __PMUTEX );
	video_ring_buffer[video_write_index].flag = VIDEO_BUFF_USED;
	NEXT_IND(video_write_index, video_ring_buffer_size);
	__UNLOCK_MUTEX( __PMUTEX );

	return 0;
}

/*
 * process next video frame on the ring buffer (encode and mux to file)
 * args:
 *   encoder_ctx - pointer to encoder context
 *
 * asserts:
 *   encoder_ctx is not null
 *
 * returns: error code
 */
int encoder_process_next_video_buffer(encoder_context_t *encoder_ctx)
{
	/*assertions*/
	assert(encoder_ctx != NULL);

	__LOCK_MUTEX( __PMUTEX );

	int flag = video_ring_buffer[video_read_index].flag;

	__UNLOCK_MUTEX ( __PMUTEX );

	if(flag == VIDEO_BUFF_FREE)
		return 1; /*all done*/

	/*timestamp is zero indexed*/
	encoder_ctx->enc_video_ctx->pts = video_ring_buffer[video_read_index].timestamp;

	/*raw (direct input)*/
	if(encoder_ctx->video_codec_ind == 0)
	{
		/*outbuf_coded_size must already be set*/
		encoder_ctx->enc_video_ctx->outbuf_coded_size = video_ring_buffer[video_read_index].frame_size;
		if(video_ring_buffer[video_read_index].keyframe)
			encoder_ctx->enc_video_ctx->flags |= AV_PKT_FLAG_KEY;
	}

	encoder_encode_video(encoder_ctx, video_ring_buffer[video_read_index].frame);


	/*mux the frame*/
	__LOCK_MUTEX( __PMUTEX );

	video_ring_buffer[video_read_index].flag = VIDEO_BUFF_FREE;
	NEXT_IND(video_read_index, video_ring_buffer_size);

	__UNLOCK_MUTEX ( __PMUTEX );

	encoder_write_video_data(encoder_ctx);

	return 0;
}

/*
 * process all used video frames from buffer
  * args:
 *   encoder_ctx - pointer to encoder context
 *
 * asserts:
 *   encoder_ctx is not null
 *
 * returns: error code
 */
int encoder_flush_video_buffer(encoder_context_t *encoder_ctx)
{
	/*assertions*/
	assert(encoder_ctx != NULL);

	__LOCK_MUTEX( __PMUTEX );
	int flag = video_ring_buffer[video_read_index].flag;
	__UNLOCK_MUTEX ( __PMUTEX );

	int buffer_count = video_ring_buffer_size;

	while(flag != VIDEO_BUFF_FREE && buffer_count > 0)
	{
		buffer_count--;

		/*timestamp is zero indexed*/
		encoder_ctx->enc_video_ctx->pts = video_ring_buffer[video_read_index].timestamp;

		/*raw (direct input)*/
		if(encoder_ctx->video_codec_ind == 0)
		{
			/*outbuf_coded_size must already be set*/
			encoder_ctx->enc_video_ctx->outbuf_coded_size = video_ring_buffer[video_read_index].frame_size;
			if(video_ring_buffer[video_read_index].keyframe)
				encoder_ctx->enc_video_ctx->flags |= AV_PKT_FLAG_KEY;
		}

		encoder_encode_video(encoder_ctx, video_ring_buffer[video_read_index].frame);

		/*mux the frame*/
		__LOCK_MUTEX( __PMUTEX );

		video_ring_buffer[video_read_index].flag = VIDEO_BUFF_FREE;
		NEXT_IND(video_read_index, video_ring_buffer_size);

		__UNLOCK_MUTEX ( __PMUTEX );

		encoder_write_video_data(encoder_ctx);

		/*get next buffer flag*/
		__LOCK_MUTEX( __PMUTEX );
		flag = video_ring_buffer[video_read_index].flag;
		__UNLOCK_MUTEX ( __PMUTEX );
	}

	/*flush libav*/
	int flushed_frame_counter = 0;
	encoder_ctx->enc_video_ctx->flush_delayed_frames  = 1;
	while(!encoder_ctx->enc_video_ctx->flush_done &&
		flushed_frame_counter <= encoder_ctx->enc_video_ctx->delayed_frames)
	{
		encoder_encode_video(encoder_ctx, NULL);
		encoder_write_video_data(encoder_ctx);
		flushed_frame_counter++;
	}

	if(!buffer_count)
	{
		fprintf(stderr, "ENCODER: (flush video buffer) max processed buffers reached\n");
		return -1;
	}

	return 0;
}

/*
 * encode video frame
 * args:
 *   encoder_ctx - pointer to encoder context
 *   input_frame - pointer to frame data
 *
 * asserts:
 *   encoder_ctx is not null
 *
 * returns: encoded buffer size
 */
int encoder_encode_video(encoder_context_t *encoder_ctx, void *input_frame)
{
	/*assertions*/
	assert(encoder_ctx != NULL);

	encoder_video_context_t *enc_video_ctx = encoder_ctx->enc_video_ctx;

	int outsize = 0;

	if(!enc_video_ctx)
	{
		if(verbosity > 1)
			printf("ENCODER: video encoder not set\n");
		encoder_ctx->enc_video_ctx->outbuf_coded_size = outsize;
		return outsize;
	}
	
	encoder_codec_data_t *video_codec_data = (encoder_codec_data_t *) enc_video_ctx->codec_data;

	/*raw - direct input no software encoding*/
	if(encoder_ctx->video_codec_ind == 0)
	{
		if(input_frame == NULL)
		{
			encoder_ctx->enc_video_ctx->outbuf_coded_size = outsize;
			return outsize;
		}
		/*outbuf_coded_size must already be set*/
		outsize = enc_video_ctx->outbuf_coded_size;
		memcpy(enc_video_ctx->outbuf, input_frame, outsize);
		enc_video_ctx->flags = 0;
		/*enc_video_ctx->flags must be set*/
		enc_video_ctx->dts = AV_NOPTS_VALUE;

		if(last_video_pts == 0)
			last_video_pts = enc_video_ctx->pts;

		enc_video_ctx->duration = enc_video_ctx->pts - last_video_pts;
		last_video_pts = enc_video_ctx->pts;
		return (outsize);
	}

	if(input_frame != NULL)
	{
#ifdef USE_PLANAR_YUV
		prepare_video_frame(video_codec_data, input_frame, encoder_ctx->video_width, encoder_ctx->video_height);
#else
		/*convert default yuyv to y420p (libav input format)*/		
		switch(video_codec_data->codec_context->pix_fmt)
		{
			case AV_PIX_FMT_YUVJ420P:
				yuv422to420p(encoder_ctx, input_frame);
				break;
			default:
				yuv422to420p(encoder_ctx, input_frame);
				break;
		}
#endif
	}

	if(!enc_video_ctx->monotonic_pts) //generate a real pts based on the frame timestamp
		video_codec_data->frame->pts += ((enc_video_ctx->pts - last_video_pts)/1000) * 90;
	else  /*generate a true monotonic pts based on the codec fps*/
		video_codec_data->frame->pts +=
			(video_codec_data->codec_context->time_base.num * 1000 / video_codec_data->codec_context->time_base.den) * 90;

	if(enc_video_ctx->flush_delayed_frames)
	{
		//pkt.size = 0;
		if(!enc_video_ctx->flushed_buffers)
		{
			avcodec_flush_buffers(video_codec_data->codec_context);
			enc_video_ctx->flushed_buffers = 1;
		}
 	}

#if LIBAVCODEC_VER_AT_LEAST(54,01)
	AVPacket pkt;
    int got_packet = 0;
    av_init_packet(&pkt);
	pkt.data = enc_video_ctx->outbuf;
	pkt.size = enc_video_ctx->outbuf_size;

	int ret = 0;
    //if(enc_video_ctx->outbuf_size < FF_MIN_BUFFER_SIZE)
    //{
	//	av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
    //    return -1;
    //}
 	if(!enc_video_ctx->flush_delayed_frames)
    	ret = avcodec_encode_video2(
			video_codec_data->codec_context,
			&pkt,
			video_codec_data->frame,
			&got_packet);
   	else
   		ret = avcodec_encode_video2(
			video_codec_data->codec_context,
			&pkt, NULL, /*NULL flushes the encoder buffers*/
			&got_packet);

    if (!ret && got_packet && video_codec_data->codec_context->coded_frame)
    {
		/* Do we really need to set this ???*/
    	video_codec_data->codec_context->coded_frame->pts = pkt.pts;
        video_codec_data->codec_context->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }

	enc_video_ctx->dts = pkt.dts;
	enc_video_ctx->flags = pkt.flags;
	enc_video_ctx->duration = pkt.duration;

    /* free any side data since we cannot return it */
    if (pkt.side_data_elems > 0)
    {
    	int i;
        for (i = 0; i < pkt.side_data_elems; i++)
        	av_free(pkt.side_data[i].data);
        av_freep(&pkt.side_data);
        pkt.side_data_elems = 0;
    }

    outsize = pkt.size;
#else
	if(!enc_video_ctx->flush_delayed_frames)
		outsize = avcodec_encode_video(
			video_codec_data->codec_context,
			enc_video_ctx->outbuf,
			enc_video_ctx->outbuf_size,
			video_codec_data->frame);
	else
		outsize = avcodec_encode_video(
			video_codec_data->codec_context,
			enc_video_ctx->outbuf,
			enc_video_ctx->outbuf_size,
			NULL); /*NULL flushes the encoder buffers*/

	enc_video_ctx->flags = 0;
	if (video_codec_data->codec_context->coded_frame->key_frame)
		enc_video_ctx->flags |= AV_PKT_FLAG_KEY;
	enc_video_ctx->dts = AV_NOPTS_VALUE;

	if(last_video_pts == 0)
		last_video_pts = enc_video_ctx->pts;

	enc_video_ctx->duration = enc_video_ctx->pts - last_video_pts;
#endif

	last_video_pts = enc_video_ctx->pts;

	if(enc_video_ctx->flush_delayed_frames && outsize == 0)
    	enc_video_ctx->flush_done = 1;
	else if(outsize == 0 && enc_video_ctx->index_of_df < 0)
	{
	    enc_video_ctx->delayed_pts[enc_video_ctx->delayed_frames] = enc_video_ctx->pts;
	    enc_video_ctx->delayed_frames++;
	    if(enc_video_ctx->delayed_frames > MAX_DELAYED_FRAMES)
	    {
	    	enc_video_ctx->delayed_frames = MAX_DELAYED_FRAMES;
	    	printf("ENCODER: Maximum of %i delayed video frames reached...\n", MAX_DELAYED_FRAMES);
	    }
	}
	else
	{
		if(enc_video_ctx->delayed_frames > 0)
		{
			if(enc_video_ctx->index_of_df < 0)
			{
				enc_video_ctx->index_of_df = 0;
				printf("ENCODER: video codec is using %i delayed video frames\n", enc_video_ctx->delayed_frames);
			}
			int64_t my_pts = enc_video_ctx->pts;
			enc_video_ctx->pts = enc_video_ctx->delayed_pts[enc_video_ctx->index_of_df];
			enc_video_ctx->delayed_pts[enc_video_ctx->index_of_df] = my_pts;
			enc_video_ctx->index_of_df++;
			if(enc_video_ctx->index_of_df >= enc_video_ctx->delayed_frames)
				enc_video_ctx->index_of_df = 0;
		}
	}

	encoder_ctx->enc_video_ctx->outbuf_coded_size = outsize;
	return (outsize);
}

/*
 * close and clean encoder context
 * args:
 *   encoder_ctx - pointer to encoder context data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void encoder_close(encoder_context_t *encoder_ctx)
{
	encoder_clean_video_ring_buffer();

	if(!encoder_ctx)
		return;

	encoder_video_context_t *enc_video_ctx = encoder_ctx->enc_video_ctx;
	encoder_codec_data_t *video_codec_data = NULL;

	if(encoder_ctx->h264_pps)
		free(encoder_ctx->h264_pps);

	if(encoder_ctx->h264_sps)
		free(encoder_ctx->h264_sps);

	/*close video codec*/
	if(enc_video_ctx)
	{
		video_codec_data = (encoder_codec_data_t *) enc_video_ctx->codec_data;
		if(video_codec_data)
		{
			if(!(enc_video_ctx->flushed_buffers))
			{
				avcodec_flush_buffers(video_codec_data->codec_context);
				enc_video_ctx->flushed_buffers = 1;
			}
			avcodec_close(video_codec_data->codec_context);
			free(video_codec_data->codec_context);

#if LIBAVCODEC_VER_AT_LEAST(53,6)
			av_dict_free(&(video_codec_data->private_options));
#endif

			if(video_codec_data->frame)
#if LIBAVCODEC_VER_AT_LEAST(55,28)
				av_frame_free(&video_codec_data->frame);
#else
	#if LIBAVCODEC_VER_AT_LEAST(54,28)
				avcodec_free_frame(&video_codec_data->frame);
	#else
				av_freep(&video_codec_data->frame);
	#endif
#endif
			free(video_codec_data);		
		}

		if(enc_video_ctx->priv_data)
			free(enc_video_ctx->priv_data);
		if(enc_video_ctx->tmpbuf)
			free(enc_video_ctx->tmpbuf);
		if(enc_video_ctx->outbuf)
			free(enc_video_ctx->outbuf);

		free(enc_video_ctx);
	}

	free(encoder_ctx);

	/*reset static data*/
	last_video_pts = 0;
	last_audio_pts = 0;
	reference_pts  = 0;

	video_frame_max_size = 0;

	video_ring_buffer_size = 0;
	video_ring_buffer = NULL;
	video_read_index = 0;
	video_write_index = 0;
	video_scheduler = 0;

}
