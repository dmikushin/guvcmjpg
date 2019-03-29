/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
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
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
/* support for internationalization - i18n */
#include <locale.h>
#include <libintl.h>

#include "gviewv4l2core.h"
#include "gviewrender.h"
#include "gview.h"
#include "video_capture.h"
#include "options.h"
#include "core_io.h"
#include "config.h"

/*flags*/
extern int debug_level;

static int render = RENDER_SDL; /*render API*/
static int quit = 0; /*terminate flag*/

static uint64_t my_photo_timer = 0; /*timer count*/

static uint64_t my_video_timer = 0; /*timer count*/
static uint64_t my_video_begin_time = 0; /*first video frame ts*/

static int restart = 0; /*restart flag*/

static char render_caption[64]; /*render window caption*/

/*continues focus*/
static int do_soft_autofocus = 0;
/*single time focus (can happen during continues focus)*/
static int do_soft_focus = 0;

static char status_message[80];

/*
 * set render flag
 * args:
 *    value - flag value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void set_render_flag(int value)
{
	render = value;
}

/*
 * set software autofocus flag
 * args:
 *    value - flag value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void set_soft_autofocus(int value)
{
	do_soft_autofocus = value;
}

/*
 * set software focus flag
 * args:
 *    value - flag value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void set_soft_focus(int value)
{
	v4l2core_soft_autofocus_set_focus();

	do_soft_focus = value;
}
/*
 * request format update
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void request_format_update()
{
	restart = 1;
}

/*
 * quit callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int quit_callback(void *data)
{
	return 0;
}

/*
 * capture loop (should run in a separate thread)
 * args:
 *    data - pointer to user data (options data)
 *
 * asserts:
 *    none
 *
 * returns: pointer to return code
 */
void *capture_loop(void *data)
{
    static double last_fps,cur_fps;
    int v = 0;
	capture_loop_data_t *cl_data = (capture_loop_data_t *) data;
	options_t *my_options = (options_t *) cl_data->options;
	//config_t *my_config = (config_t *) cl_data->config;

	uint64_t my_last_photo_time = 0; /*timer count*/
	int my_photo_npics = 0;/*no npics*/

	/*reset quit flag*/
	quit = 0;
    last_fps = -1.0;
	
	if(debug_level > 1)
		printf("GUVCVIEW: capture thread (tid: %u)\n", 
			(unsigned int) syscall (SYS_gettid));

	int ret = 0;
	
	int render_flags = 0;
	
	if (strcasecmp(my_options->render_flag, "full") == 0)
		render_flags = 1;
	else if (strcasecmp(my_options->render_flag, "max") == 0)
		render_flags = 2;
	
	render_set_verbosity(debug_level);
	
	if(render_init(render, v4l2core_get_frame_width(), v4l2core_get_frame_height(), render_flags) < 0)
		render = RENDER_NONE;
	else
		render_set_event_callback(EV_QUIT, &quit_callback, NULL);

	/*add a photo capture timer*/
	if(my_options->photo_timer > 0)
	{
		my_photo_timer = NSEC_PER_SEC * my_options->photo_timer;
		my_last_photo_time = v4l2core_time_get_timestamp(); /*timer count*/
	}

	if(my_options->photo_npics > 0)
		my_photo_npics = my_options->photo_npics;

	v4l2core_start_stream();
	
	v4l2_frame_buff_t *frame = NULL; //pointer to frame buffer

	while(!quit)
	{
		if(restart)
		{
			restart = 0; /*reset*/
			v4l2core_stop_stream();

			/*close render*/
			render_close();

			v4l2core_clean_buffers();

			/*try new format (values prepared by the request callback)*/
			ret = v4l2core_update_current_format();
			/*try to set the video stream format on the device*/
			if(ret != E_OK)
			{
				fprintf(stderr, "GUCVIEW: could not set the defined stream format\n");
				fprintf(stderr, "GUCVIEW: trying first listed stream format\n");

				v4l2core_prepare_valid_format();
				v4l2core_prepare_valid_resolution();
				ret = v4l2core_update_current_format();

				if(ret != E_OK)
				{
					fprintf(stderr, "GUCVIEW: also could not set the first listed stream format\n");
					return ((void *) -1);
				}
			}

			/*restart the render with new format*/
			if(render_init(render, v4l2core_get_frame_width(), v4l2core_get_frame_height(), render_flags) < 0)
				render = RENDER_NONE;
			else
				render_set_event_callback(EV_QUIT, &quit_callback, NULL);

			if(debug_level > 0)
				printf("GUVCVIEW: reset to pixelformat=%x width=%i and height=%i\n", v4l2core_get_requested_frame_format(), v4l2core_get_frame_width(), v4l2core_get_frame_height());

			v4l2core_start_stream();

		}

		frame = v4l2core_get_decoded_frame();
		if( frame != NULL)
		{
			/*run software autofocus (must be called after frame was grabbed and decoded)*/
			if(do_soft_autofocus || do_soft_focus)
				do_soft_focus = v4l2core_soft_autofocus_run(frame);

			/*render the decoded frame*/
            cur_fps = v4l2core_get_realfps();
            if (last_fps != cur_fps) {
                last_fps = cur_fps;
                if (my_options->cmos_camera)
                    snprintf(render_caption, 63, "Guvcmjpg ver:%s - %s - CMOS %dx%d (%2.2f fps) - seq: %d",  VERSION, my_options->format, v4l2core_get_frame_width(), v4l2core_get_frame_height(), cur_fps, v);
                else
                    snprintf(render_caption, 63, "Guvcmjpg ver:%s - %s - %dx%d (%2.2f fps) - seq: %d",  VERSION, my_options->format, v4l2core_get_frame_width(), v4l2core_get_frame_height(), cur_fps, v);
                render_set_caption(render_caption);
                v++;
            }
			render_frame(frame->yuv_frame);

			/*we are done with the frame buffer release it*/
			v4l2core_release_frame(frame);
		}
	}

	v4l2core_stop_stream();
	
	render_close();

	return ((void *) 0);
}
