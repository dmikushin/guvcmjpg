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

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>

#include "gviewv4l2core.h"
#include "gview.h"
#include "core_io.h"
#include "config.h"

#define MAXLINE 100 /*100 char lines max*/

int verbosity = 0;

extern int debug_level;

static config_t my_config =
{
	.width = 640,
	.height = 480,
	.format = "YU12",
	.render = "sdl",
	.gui = "gtk3",
	.capture = "mmap",
	.video_codec = "dx50",
	.fps_num = 1,
	.fps_denom = 30,
	.cmos_camera = 1, /*guvcview will use CMOS CAMERA on startup*/
};

/*
 * get the internal config data
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: pointer to internal config_t struct
 */
config_t *config_get()
{
	return &my_config;
}
