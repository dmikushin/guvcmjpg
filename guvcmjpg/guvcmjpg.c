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
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <locale.h>

#include "gview.h"
#include "gviewv4l2core.h"
#include "gviewrender.h"
#include "core_time.h"

#include "video_capture.h"
#include "options.h"
#include "core_io.h"

#include "config.h"

int debug_level = 0;

int main(int argc, char *argv[])
{
	/*check stack size*/
	const rlim_t kStackSize = 128L * 1024L * 1024L;   /* min stack size = 128 Mb*/
    struct rlimit rl;
    int result;

    result = getrlimit(RLIMIT_STACK, &rl);
    if (result == 0)
    {
        if (rl.rlim_cur < kStackSize)
        {
            rl.rlim_cur = kStackSize;
            result = setrlimit(RLIMIT_STACK, &rl);
            if (result != 0)
            {
                fprintf(stderr, "GUVCVIEW: setrlimit returned result = %d\n", result);
            }
        }
    }
	
	/*parse command line options*/
	if(options_parse(argc, argv))
		return 0;
	
	printf("GUVCVIEW: version %s\n", VERSION);

	/*get command line options*/
	options_t *my_options = options_get();

	char *device_name = get_file_basename(my_options->device);
	free(device_name);

	/*get config data*/
	config_t *my_config = config_get();

	debug_level = my_options->verbosity;
	
	/*select render API*/
	int render = RENDER_SDL;

	if(strcasecmp(my_config->render, "none") == 0)
		render = RENDER_NONE;
	else if(strcasecmp(my_config->render, "sdl") == 0)
		render = RENDER_SDL;

	/*initialize the v4l2 core*/
	v4l2core_set_verbosity(debug_level);
	
	if(my_options->disable_libv4l2)
		v4l2core_disable_libv4l2();
	/*init the device list*/
	v4l2core_init_device_list();
	/*init the v4l2core (redefines language catalog)*/
	if(v4l2core_init_dev(my_options->device) < 0)
	{
		char message[60];
		snprintf(message, 59, "no video device (%s) found", my_options->device);
		v4l2core_close_v4l2_device_list();
		options_clean();
		return -1;
	}
	else		
		set_render_flag(render);
	
	/*select capture method*/
	if(strcasecmp(my_config->capture, "read") == 0)
		v4l2core_set_capture_method(IO_READ);
	else
		v4l2core_set_capture_method(IO_MMAP);

	/*set software autofocus sort method*/
	v4l2core_soft_autofocus_set_sort(AUTOF_SORT_INSERT);

	/*set the intended fps*/
	v4l2core_define_fps(my_config->fps_num,my_config->fps_denom);

	/*select video codec*/
	if(debug_level > 1)
		printf("GUVCVIEW: setting video codec to '%s'\n", my_config->video_codec);
		
	/*
	 * prepare format:
	 *   doing this inside the capture thread may create a race
	 *   condition with gui_attach, as it requires the current
	 *   format to be set
	 */
	int format = v4l2core_fourcc_2_v4l2_pixelformat(my_options->format);

	if(debug_level > 0)
		printf("GUVCVIEW: setting pixelformat to '%s'\n", my_options->format);

	v4l2core_prepare_new_format(format);
	/*prepare resolution*/
	v4l2core_prepare_new_resolution(my_config->width, my_config->height);
	/*try to set the video stream format on the device*/
	int ret = v4l2core_update_current_format();

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
			fprintf(stderr, "GUVCVIEW: Video capture failed\n");
		}
		
		return -1;
	}

	capture_loop_data_t cl_data;
	cl_data.options = (void *) my_options;
	cl_data.config = (void *) my_config;

	capture_loop((void *) &cl_data);

	v4l2core_close_dev();

	v4l2core_close_v4l2_device_list();

	options_clean();

	return 0;
}

