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

#ifndef CONFIG_H
#define CONFIG_H

#include "options.h"

extern int verbosity;

typedef struct _config_t
{
	int  width;      /*width*/
	int  height;     /*height*/
	char format[5];  /*pixelformat fourcc*/
	char render[5];  /*render api*/
	char gui[5];     /*gui api*/
	char capture[5]; /*capture method: read or mmap*/
	char video_codec[5]; /*video codec*/
	int fps_num;
	int fps_denom;
	int cmos_camera;    /* using CMOS camera */
} config_t;

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
config_t *config_get();

#endif
