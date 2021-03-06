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
#  Render library                                                               #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
/* support for internationalization - i18n */
#include <locale.h>
#include <libintl.h>

#include "gviewrender.h"
#include "config.h"
#include "render_sdl1.h"

static int render_api = RENDER_SDL;

static int my_width = 0;
static int my_height = 0;

static render_events_t render_events_list[] =
{
	{
		.id = EV_QUIT,
		.callback = NULL,
		.data = NULL,
	},
};

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
void render_set_verbosity(int value)
{
	verbosity = value;
}

/*
 * get render width
 * args:
 *   none
 *
 * asserts:
 *    none
 *
 * returns: render width
 */
int render_get_width()
{
	return my_width;
}

/*
 * get render height
 * args:
 *   none
 *
 * asserts:
 *    none
 *
 * returns: render height
 */
int render_get_height()
{
	return my_height;
}

/*
 * render initialization
 * args:
 *   render - render API to use (RENDER_NONE, RENDER_SDL1, ...)
 *   width - render width
 *   height - render height
 *   flags - window flags:
 *              0- none
 *              1- fullscreen
 *              2- maximized
 *
 * asserts:
 *   none
 *
 * returns: error code
 */
int render_init(int render, int width, int height, int flags)
{

	int ret = 0;

	render_api = render;
	my_width = width;
	my_height = height;

	switch(render_api)
	{
		case RENDER_NONE:
			break;

		case RENDER_SDL:
		default:
			#if ENABLE_SDL2
			ret = init_render_sdl2(my_width, my_height, flags);
			#else
			ret = init_render_sdl1(my_width, my_height, flags);
			#endif
			break;
	}

	if(ret)
		render_api = RENDER_NONE;

	return ret;
}

/*
 * render a frame
 * args:
 *   frame - pointer to frame data (yuyv format)
 *
 * asserts:
 *   frame is not null
 *
 * returns: error code
 */
int render_frame(uint8_t *frame)
{
	/*asserts*/
	assert(frame != NULL);

	int ret = 0;
	switch(render_api)
	{
		case RENDER_NONE:
			break;

		case RENDER_SDL:
		default:
			#if ENABLE_SDL2
			ret = render_sdl2_frame(frame, my_width, my_height);
			render_sdl2_dispatch_events();
			#else
			ret = render_sdl1_frame(frame, my_width, my_height);
			render_sdl1_dispatch_events();
			#endif
			break;
	}

	return ret;
}

/*
 * set event callback
 * args:
 *    id - event id
 *    callback_function - pointer to callback function
 *    data - pointer to user data (passed to callback)
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int render_set_event_callback(int id, render_event_callback callback_function, void *data)
{
	int index = render_get_event_index(id);
	if(index < 0)
		return index;

	render_events_list[index].callback = callback_function;
	render_events_list[index].data = data;

	return 0;
}

/*
 * call the event callback for event id
 * args:
 *    id - event id
 *
 * asserts:
 *    none
 *
 * returns: error code
 */
int render_call_event_callback(int id)
{
	int index = render_get_event_index(id);

	if(verbosity > 1)
		printf("RENDER: event %i -> callback %i\n", id, index);
               
	if(index < 0)
		return index;

	if(render_events_list[index].callback == NULL)
		return -1;

	int ret = render_events_list[index].callback(render_events_list[index].data);

	return ret;
}

/*
 * get event index on render_events_list
 * args:
 *    id - event id
 *
 * asserts:
 *    none
 *
 * returns: event index or -1 on error
 */
int render_get_event_index(int id)
{
	int i = 0;
	while(render_events_list[i].id >= 0)
	{
		if(render_events_list[i].id == id)
			return i;

		i++;
	}
	return -1;
}

/*
 * set caption
 * args:
 *   caption - string with render window caption
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_set_caption(const char* caption)
{
	switch(render_api)
	{
		case RENDER_NONE:
			break;

		case RENDER_SDL:
		default:
			#if ENABLE_SDL2
			set_render_sdl2_caption(caption);
			#else
			set_render_sdl1_caption(caption);
			#endif
			break;
	}
}

/*
 * clean render data
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_close()
{
	switch(render_api)
	{
		case RENDER_NONE:
			break;

		case RENDER_SDL:
		default:
			#if ENABLE_SDL2
			render_sdl2_clean();
			#else
			render_sdl1_clean();
			#endif
			break;
	}

	my_width = 0;
	my_height = 0;
}

