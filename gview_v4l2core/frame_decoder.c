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
#include "frame_decoder.h"
#include "jpeg_decoder.h"
#include "colorspaces.h"

extern int verbosity;

/*
 * Alloc image buffers for decoding video stream
 * args:
 *   vd - pointer to video device data
 *
 * asserts:
 *   vd is not null
 *
 * returns: error code  (0- E_OK)
 */
int alloc_v4l2_frames(v4l2_dev_t *vd)
{
	/*assertions*/
	assert(vd != NULL);

	if(verbosity > 2)
		printf("V4L2_CORE: allocating frame buffers\n");
	/*clean any previous frame buffers*/
	clean_v4l2_frames(vd);

	int ret = E_OK;

	int i = 0;
	size_t framebuf_size = 0;

	int width = vd->format.fmt.pix.width;
	int height = vd->format.fmt.pix.height;

	if(width <= 0 || height <= 0)
		return E_ALLOC_ERR;
#ifdef USE_PLANAR_YUV
	int framesizeIn = (width * height * 3/2); /* 3/2 bytes per pixel*/
#else
	int framesizeIn = (width * height * 2); /*2 bytes per pixel*/
#endif
	switch (vd->requested_fmt)
	{
		case V4L2_PIX_FMT_JPEG:
		case V4L2_PIX_FMT_MJPEG:
			/*init jpeg decoder*/
			ret = jpeg_init_decoder(width, height);

			if(ret)
			{
				fprintf(stderr, "V4L2_CORE: couldn't init jpeg decoder\n");
				return ret;
			}
			
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				vd->frame_queue[i].yuv_frame = calloc(framesizeIn, sizeof(uint8_t));
				if(vd->frame_queue[i].yuv_frame == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			}
			break;

		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YVYU:
		case V4L2_PIX_FMT_YYUV:
		case V4L2_PIX_FMT_YUV420: /* only needs 3/2 bytes per pixel but we alloc 2 bytes per pixel*/
		case V4L2_PIX_FMT_YVU420: /* only needs 3/2 bytes per pixel but we alloc 2 bytes per pixel*/
		case V4L2_PIX_FMT_Y41P:   /* only needs 3/2 bytes per pixel but we alloc 2 bytes per pixel*/
		case V4L2_PIX_FMT_NV12:
		case V4L2_PIX_FMT_NV21:
		case V4L2_PIX_FMT_NV16:
		case V4L2_PIX_FMT_NV61:
		case V4L2_PIX_FMT_SPCA501:
		case V4L2_PIX_FMT_SPCA505:
		case V4L2_PIX_FMT_SPCA508:
			framebuf_size = framesizeIn;
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				/*FIXME: do we need the temp buffer ?*/
				/* alloc a temp buffer for colorspace conversion*/
				vd->frame_queue[i].tmp_buffer_max_size = width * height * 2;
				vd->frame_queue[i].tmp_buffer = calloc(vd->frame_queue[i].tmp_buffer_max_size, sizeof(uint8_t));
				if(vd->frame_queue[i].tmp_buffer == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			
				vd->frame_queue[i].yuv_frame = calloc(framebuf_size, sizeof(uint8_t));
				if(vd->frame_queue[i].yuv_frame == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			}
			break;

		case V4L2_PIX_FMT_GREY:
			framebuf_size = framesizeIn;
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				/* alloc a temp buffer for colorspace conversion*/
				vd->frame_queue[i].tmp_buffer_max_size = width * height; /* 1 byte per pixel*/
				vd->frame_queue[i].tmp_buffer = calloc(vd->frame_queue[i].tmp_buffer_max_size, sizeof(uint8_t));
				if(vd->frame_queue[i].tmp_buffer == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
				vd->frame_queue[i].yuv_frame = calloc(framebuf_size, sizeof(uint8_t));
				if(vd->frame_queue[i].yuv_frame == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			}
			break;

	    case V4L2_PIX_FMT_Y10BPACK:
	    case V4L2_PIX_FMT_Y16:
			framebuf_size = framesizeIn;
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				/* alloc a temp buffer for colorspace conversion*/
				vd->frame_queue[i].tmp_buffer_max_size = framesizeIn; /* 2 byte per pixel*/
				vd->frame_queue[i].tmp_buffer = calloc(vd->frame_queue[i].tmp_buffer_max_size, sizeof(uint8_t));
				if(vd->frame_queue[i].tmp_buffer == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
				vd->frame_queue[i].yuv_frame = calloc(framebuf_size, sizeof(uint8_t));
				if(vd->frame_queue[i].yuv_frame == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			}
			break;

		case V4L2_PIX_FMT_YUYV:
			/*
			 * YUYV doesn't need a temp buffer but we will set it if/when
			 *  video processing disable is set (bayer processing).
			 *            (logitech cameras only)
			 */
			framebuf_size = framesizeIn;
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				vd->frame_queue[i].yuv_frame = calloc(framebuf_size, sizeof(uint8_t));
				if(vd->frame_queue[i].yuv_frame == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			}
			break;

		case V4L2_PIX_FMT_SGBRG8: /*0*/
		case V4L2_PIX_FMT_SGRBG8: /*1*/
		case V4L2_PIX_FMT_SBGGR8: /*2*/
		case V4L2_PIX_FMT_SRGGB8: /*3*/
			/*
			 * Raw 8 bit bayer
			 * when grabbing use:
			 *    bayer_to_rgb24(bayer_data, RGB24_data, width, height, 0..3)
			 *    rgb2yuyv(RGB24_data, vd->framebuffer, width, height)
			 */
			framebuf_size = framesizeIn;
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				/* alloc a temp buffer for converting to YUYV*/
				/* rgb buffer for decoding bayer data*/
				vd->frame_queue[i].tmp_buffer_max_size = width * height * 3;
				vd->frame_queue[i].tmp_buffer = calloc(vd->frame_queue[i].tmp_buffer_max_size, sizeof(uint8_t));
				if(vd->frame_queue[i].tmp_buffer == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
				vd->frame_queue[i].yuv_frame = calloc(framebuf_size, sizeof(uint8_t));
				if(vd->frame_queue[i].yuv_frame == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			}
			break;
		case V4L2_PIX_FMT_RGB24:
		case V4L2_PIX_FMT_BGR24:
			/*convert directly from raw_frame*/
			framebuf_size = framesizeIn;
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				vd->frame_queue[i].yuv_frame = calloc(framebuf_size, sizeof(uint8_t));
				if(vd->frame_queue[i].yuv_frame == NULL)
				{
					fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (alloc_v4l2_frames): %s\n", strerror(errno));
					exit(-1);
				}
			}
			break;

		default:
			/*
			 * we check formats against a support formats list
			 * so we should never have to alloc for a unknown format
			 */
			fprintf(stderr, "V4L2_CORE: (v4l2uvc.c) should never arrive (1)- exit fatal !!\n");
			ret = E_UNKNOWN_ERR;
			
			/*frame queue*/
			for(i=0; i<vd->frame_queue_size; ++i)
			{
				vd->frame_queue[i].raw_frame = NULL;
				if(vd->frame_queue[i].yuv_frame)
					free(vd->frame_queue[i].yuv_frame);
				vd->frame_queue[i].yuv_frame = NULL;
				if(vd->frame_queue[i].tmp_buffer)
					free(vd->frame_queue[i].tmp_buffer);
				vd->frame_queue[i].tmp_buffer = NULL;
			}
			return (ret);
	}

	for(i=0; i<vd->frame_queue_size; ++i)
	{
		int j = 0;
		/* set framebuffer to black (y=0x00 u=0x80 v=0x80) by default*/
#ifdef USE_PLANAR_YUV
		uint8_t *pframe = vd->frame_queue[i].yuv_frame;
		for (j=0; j<width*height; j++)
		*pframe++=0x00; //Y
		for(j=0; j<width*height/2; j++)
		{
			*pframe++=0x80; //U V
		}
#else
		for (j=0; j<((width*height*2)-4); j+=4)
		{
			vd->frame_queue[i].yuv_frame[j]=0x00;  //Y
			vd->frame_queue[i].yuv_frame[j+1]=0x80;//U
			vd->frame_queue[i].yuv_frame[j+2]=0x00;//Y
			vd->frame_queue[i].yuv_frame[j+3]=0x80;//V
		}
#endif
	}
	return (ret);
}

/*
 * free image buffers for decoding video stream
 * args:
 *   vd - pointer to video device data
 *
 * asserts:
 *   vd is not null
 *
 * returns: none
 */
void clean_v4l2_frames(v4l2_dev_t *vd)
{
	/*assertions*/
	assert(vd != NULL);

	int i = 0;
	
	for(i=0; i<vd->frame_queue_size; ++i)
	{
		vd->frame_queue[i].raw_frame = NULL;

		if(vd->frame_queue[i].tmp_buffer)
		{
			free(vd->frame_queue[i].tmp_buffer);
			vd->frame_queue[i].tmp_buffer = NULL;
		}

		if(vd->frame_queue[i].yuv_frame)
		{
			free(vd->frame_queue[i].yuv_frame);
			vd->frame_queue[i].yuv_frame = NULL;
		}
	}

	if(vd->requested_fmt == V4L2_PIX_FMT_JPEG ||
	   vd->requested_fmt == V4L2_PIX_FMT_MJPEG)
		jpeg_close_decoder();
}

/*
 * decode video stream ( from raw_frame to frame buffer (yuyv format))
 * args:
 *    vd - pointer to device data
 *    frame - pointer to frame buffer
 *
 * asserts:
 *    vd is not null
 *
 * returns: error code ( 0 - E_OK)
*/
int decode_v4l2_frame(v4l2_dev_t *vd, v4l2_frame_buff_t *frame)
{
	/*asserts*/
	assert(vd != NULL);

	if(!frame->raw_frame || frame->raw_frame_size == 0)
	{
		fprintf(stderr, "V4L2_CORE: not decoding empty raw frame (frame of size %i at 0x%p)\n", (int) frame->raw_frame_size, frame->raw_frame);
		return E_DECODE_ERR;
	}

	if(verbosity > 3)
		printf("V4L2_CORE: decoding raw frame of size %i at 0x%p\n",
			(int) frame->raw_frame_size, frame->raw_frame );

	int ret = E_OK;

	int width = vd->format.fmt.pix.width;
	int height = vd->format.fmt.pix.height;

	/*
	 * use the requested format since it may differ
	 * from format.fmt.pix.pixelformat (muxed H264)
	 */
	int format = vd->requested_fmt;

	int framesizeIn =(width * height << 1);//2 bytes per pixel
	switch (format)
	{
		case V4L2_PIX_FMT_JPEG:
		case V4L2_PIX_FMT_MJPEG:
			if(frame->raw_frame_size <= HEADERFRAME1)
			{
				// Prevent crash on empty image
				fprintf(stderr, "V4L2_CORE: (jpeg decoder) Ignoring empty buffer\n");
				ret = E_DECODE_ERR;
				return (ret);
			}
			
			
			ret = jpeg_decode(frame->yuv_frame, frame->raw_frame, frame->raw_frame_size);						
			
			//memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			//ret = jpeg_decode(&frame->yuv_frame, frame->tmp_buffer, width, height);
			//if ( ret < 0)
			//{
			//	fprintf(stderr, "V4L2_CORE: jpeg decoder exit with error (%i) (res: %ix%i - %x)\n", ret, width, height, vd->format.fmt.pix.pixelformat);
			//	return E_DECODE_ERR;
			//}
			if(verbosity > 3)
				fprintf(stderr, "V4L2_CORE: (jpeg decoder) decode frame of size %i\n", ret);
			ret = E_OK;
			break;

		case V4L2_PIX_FMT_UYVY:
#ifdef USE_PLANAR_YUV
			uyvy_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (uyvy decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			uyvy_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_YVYU:
#ifdef USE_PLANAR_YUV
			yvyu_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (yvyu decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			yvyu_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_YYUV:
#ifdef USE_PLANAR_YUV
			yyuv_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (yyuv decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			yyuv_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_YUV420:
#ifdef USE_PLANAR_YUV
			if(frame->raw_frame_size > (width * height * 3/2))
				frame->raw_frame_size = width * height * 3/2;
			memcpy(frame->yuv_frame, frame->raw_frame, frame->raw_frame_size);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (yuv420 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			yu12_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_YVU420:
#ifdef USE_PLANAR_YUV
			yv12_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (yvu420 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			yvu420_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_NV12:
#ifdef USE_PLANAR_YUV
			nv12_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (nv12 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			nv12_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_NV21:
#ifdef USE_PLANAR_YUV
			nv21_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (nv21 decoder) cliping unexpected large buffer (%i bytes).\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			nv21_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_NV16:
#ifdef USE_PLANAR_YUV
			nv16_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (nv16 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			nv16_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_NV61:
#ifdef USE_PLANAR_YUV
			nv61_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (nv61 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			nv61_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_Y41P:
#ifdef USE_PLANAR_YUV
			y41p_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (y41p decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			y41p_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_GREY:
#ifdef USE_PLANAR_YUV
			grey_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (grey decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			grey_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_Y10BPACK:
#ifdef USE_PLANAR_YUV
			y10b_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (y10b decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			y10b_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

	    case V4L2_PIX_FMT_Y16:
#ifdef USE_PLANAR_YUV
			y16_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (y16 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			y16_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_SPCA501:
#ifdef USE_PLANAR_YUV
			s501_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (spca501 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			s501_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_SPCA505:
#ifdef USE_PLANAR_YUV
			s505_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (spca505 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			s505_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_SPCA508:
#ifdef USE_PLANAR_YUV
			s508_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			/*FIXME: do we need the tmp_buffer or can we just use the raw_frame?*/
			if(frame->raw_frame_size > frame->tmp_buffer_max_size)
			{
				// Prevent crash on very large image
				fprintf(stderr, "V4L2_CORE: (spca508 decoder) cliping unexpected large buffer (%i bytes)\n",
					(int) frame->raw_frame_size);
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->tmp_buffer_max_size);
			}
			else
				memcpy(frame->tmp_buffer, frame->raw_frame, frame->raw_frame_size);
			s508_to_yuyv(frame->yuv_frame, frame->tmp_buffer, width, height);
#endif
			break;

		case V4L2_PIX_FMT_YUYV:
#ifdef USE_PLANAR_YUV
			if(vd->isbayer>0)
			{
				if (!(frame->tmp_buffer))
				{
					/* rgb buffer for decoding bayer data*/
					frame->tmp_buffer_max_size = width * height * 3;
					frame->tmp_buffer = calloc(frame->tmp_buffer_max_size, sizeof(uint8_t));
					if(frame->tmp_buffer == NULL)
					{
						fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (v4l2core_frame_decode): %s\n", strerror(errno));
						exit(-1);
					}
				}
				/*convert raw bayer to iyuv*/
				bayer_to_rgb24 (frame->raw_frame, frame->tmp_buffer, width, height, vd->bayer_pix_order);
				rgb24_to_yu12(frame->yuv_frame, frame->tmp_buffer, width, height);
			}
			else
				yuyv_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			if(vd->isbayer>0)
			{
				if (!(frame->tmp_buffer))
				{
					/* rgb buffer for decoding bayer data*/
					frame->tmp_buffer_max_size = width * height * 3;
					frame->tmp_buffer = calloc(frame->tmp_buffer_max_size, sizeof(uint8_t));
					if(frame->tmp_buffer == NULL)
					{
						fprintf(stderr, "V4L2_CORE: FATAL memory allocation failure (v4l2core_frame_decode): %s\n", strerror(errno));
						exit(-1);
					}
				}
				bayer_to_rgb24 (frame->raw_frame, frame->tmp_buffer, width, height, vd->bayer_pix_order);
				// raw bayer is only available in logitech cameras in yuyv mode
				rgb2yuyv (frame->tmp_buffer, frame->yuv_frame, width, height);
			}
			else
			{
				if (frame->raw_frame_size > framesizeIn)
					memcpy(frame->yuv_frame, frame->raw_frame,
						(size_t) framesizeIn);
				else
					memcpy(frame->yuv_frame, frame->raw_frame, frame->raw_frame_size);
			}
#endif
			break;

		case V4L2_PIX_FMT_SGBRG8: //0
			bayer_to_rgb24 (frame->raw_frame, frame->tmp_buffer, width, height, 0);
#ifdef USE_PLANAR_YUV
			rgb24_to_yu12(frame->yuv_frame, frame->tmp_buffer, width, height);
#else
			rgb2yuyv (frame->tmp_buffer, frame->yuv_frame, width, height);
#endif
			break;

		case V4L2_PIX_FMT_SGRBG8: //1
			bayer_to_rgb24 (frame->raw_frame, frame->tmp_buffer, width, height, 1);
#ifdef USE_PLANAR_YUV
			rgb24_to_yu12(frame->yuv_frame, frame->tmp_buffer, width, height);
#else
			rgb2yuyv (frame->tmp_buffer, frame->yuv_frame, width, height);
#endif
			break;

		case V4L2_PIX_FMT_SBGGR8: //2
			bayer_to_rgb24 (frame->raw_frame, frame->tmp_buffer, width, height, 2);
#ifdef USE_PLANAR_YUV
			rgb24_to_yu12(frame->yuv_frame, frame->tmp_buffer, width, height);
#else
			rgb2yuyv (frame->tmp_buffer, frame->yuv_frame, width, height);
#endif
			break;
		case V4L2_PIX_FMT_SRGGB8: //3
			bayer_to_rgb24 (frame->raw_frame, frame->tmp_buffer, width, height, 3);
#ifdef USE_PLANAR_YUV
			rgb24_to_yu12(frame->yuv_frame, frame->tmp_buffer, width, height);
#else
			rgb2yuyv (frame->tmp_buffer, frame->yuv_frame, width, height);
#endif
			break;

		case V4L2_PIX_FMT_RGB24:
#ifdef USE_PLANAR_YUV
			rgb24_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			rgb2yuyv(frame->raw_frame, frame->yuv_frame, width, height);
#endif
			break;
		case V4L2_PIX_FMT_BGR24:
#ifdef USE_PLANAR_YUV
			bgr24_to_yu12(frame->yuv_frame, frame->raw_frame, width, height);
#else
			bgr2yuyv(frame->raw_frame, frame->yuv_frame, width, height);
#endif
			break;

		default:
			fprintf(stderr, "V4L2_CORE: error decoding frame: unknown format: %i\n", format);
			ret = E_UNKNOWN_ERR;
			break;
	}

	return ret;
}
