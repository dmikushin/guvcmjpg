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
#include "gui.h"
#include "config.h"

/*flags*/
extern int debug_level;

static int render = RENDER_SDL; /*render API*/
static int quit = 0; /*terminate flag*/
static int save_image = 0; /*save image flag*/
static int save_video = 0; /*save video flag*/

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
 * sets the save video flag
 * args:
 *    value - save_video flag value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void video_capture_save_video(int value)
{
	save_video = value;
	
	if(debug_level > 1)
		printf("GUVCVIEW: save video flag changed to %i\n", save_video);
}

/*
 * gets the save video flag
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: save_video flag
 */
int video_capture_get_save_video()
{
	return save_video;
}

/*
 * sets the save image flag
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void video_capture_save_image()
{
	save_image = 1;
}

/*
 * stops the photo timed capture
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void stop_photo_timer()
{
	my_photo_timer = 0;
	gui_set_image_capture_button_label(_("Cap. Image (I)"));
}

/*
 * checks if photo timed capture is on
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: 1 if on; 0 if off
 */
int check_photo_timer()
{
	return ( (my_photo_timer > 0) ? 1 : 0 );
}

/*
 * reset video timer
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void reset_video_timer()
{
	my_video_timer = 0;
	my_video_begin_time = 0;
}

/*
 * stops the video timed capture
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
static void stop_video_timer()
{
	/*
	 * if we are saving video stop it
	 * this also calls reset_video_timer
	 */
	if(video_capture_get_save_video())
		gui_click_video_capture_button();

	/*make sure the timer is reset*/
	reset_video_timer();
}

/*
 * checks if video timed capture is on
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: 1 if on; 0 if off
 */
int check_video_timer()
{
	return ( (my_video_timer > 0) ? 1 : 0 );
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
	/*make sure we only call gui_close once*/
	if(!quit)
		gui_close();

	quit = 1;

	return 0;
}

/************ RENDER callbacks *******************/
/*
 * key I pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_I_callback(void *data)
{
	gui_click_image_capture_button();
	
	if(debug_level > 1)
		printf("GUVCVIEW: I key pressed\n");

	return 0;
}

/*
 * key V pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_V_callback(void *data)
{
	gui_click_video_capture_button(data);
	
	if(debug_level > 1)
		printf("GUVCVIEW: V key pressed\n");

	return 0;
}

/*
 * key DOWN pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_DOWN_callback(void *data)
{
	if(v4l2core_has_pantilt_id())
    {
		int id = V4L2_CID_TILT_RELATIVE;
		int value = v4l2core_get_tilt_step();

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
}

/*
 * key UP pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_UP_callback(void *data)
{
	if(v4l2core_has_pantilt_id())
    {
		int id = V4L2_CID_TILT_RELATIVE;
		int value = - v4l2core_get_tilt_step();

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
}

/*
 * key LEFT pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_LEFT_callback(void *data)
{
	if(v4l2core_has_pantilt_id())
    {
		int id = V4L2_CID_PAN_RELATIVE;
		int value = v4l2core_get_pan_step();

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
}

/*
 * key RIGHT pressed callback
 * args:
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int key_RIGHT_callback(void *data)
{
	if(v4l2core_has_pantilt_id())
    {
		int id = V4L2_CID_PAN_RELATIVE;
		int value = - v4l2core_get_pan_step();

		v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

		if(control)
		{
			control->value =  value;

			if(v4l2core_set_control_value_by_id(id))
				fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

			return 0;
		}
	}

	return -1;
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

					gui_error("Guvcmjpg error", "could not start a video stream in the device", 1);

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

			if(check_photo_timer())
			{
				if((frame->timestamp - my_last_photo_time) > my_photo_timer)
				{
					save_image = 1;
					my_last_photo_time = frame->timestamp;

					if(my_options->photo_npics > 0)
					{
						if(my_photo_npics > 0)
							my_photo_npics--;
						else
							stop_photo_timer(); /*close timer*/
					}
				}
			}

			if(check_video_timer())
			{
				if((frame->timestamp - my_video_begin_time) > my_video_timer)
					stop_video_timer();
			}

			if(save_image)
			{
				char *img_filename = NULL;

				/*get_photo_[name|path] always return a non NULL value*/
				char *name = strdup(get_photo_name());
				char *path = strdup(get_photo_path());

				if(get_photo_sufix_flag())
				{
					char *new_name = add_file_suffix(path, name);
					free(name); /*free old name*/
					name = new_name; /*replace with suffixed name*/
				}
				int pathsize = strlen(path);
				if(path[pathsize] != '/')
					img_filename = smart_cat(path, '/', name);
				else
					img_filename = smart_cat(path, 0, name);

				//if(debug_level > 1)
				//	printf("GUVCVIEW: saving image to %s\n", img_filename);

				snprintf(status_message, 79, _("saving image to %s"), img_filename);
				gui_status_message(status_message);

				v4l2core_save_image(frame, img_filename, get_photo_format());

				free(path);
				free(name);
				free(img_filename);

				save_image = 0; /*reset*/
			}

			if(video_capture_get_save_video())
			{
#ifdef USE_PLANAR_YUV
				int size = (v4l2core_get_frame_width() * v4l2core_get_frame_height() * 3) / 2;
#else
				int size = v4l2core_get_frame_width() * v4l2core_get_frame_height() * 2;
#endif
				uint8_t *input_frame = frame->yuv_frame;
				/*
				 * TODO: check codec_id, format and frame flags
				 * (we may want to store a compressed format
				 */
				if(get_video_codec_ind() == 0) //raw frame
				{
					switch(v4l2core_get_requested_frame_format())
					{
						case  V4L2_PIX_FMT_H264:
							input_frame = frame->h264_frame;
							size = (int) frame->h264_frame_size;
							break;
						default:
							input_frame = frame->raw_frame;
							size = (int) frame->raw_frame_size;
							break;
					}

				}
			}
			/*we are done with the frame buffer release it*/
			v4l2core_release_frame(frame);
		}
	}

	v4l2core_stop_stream();
	
	render_close();

	return ((void *) 0);
}
