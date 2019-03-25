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

#include "gui_gtk3.h"
#include "gui_gtk3_callbacks.h"
#include "gui.h"
/*add this last to avoid redefining _() and N_()*/
#include "gview.h"
#include "gviewrender.h"
#include "video_capture.h"


extern int debug_level;

/*
 * attach v4l2 video controls tab widget
 * args:
 *   parent - tab parent widget
 *
 * asserts:
 *   parent is not null
 *
 * returns: error code (0 -OK)
 */
int gui_attach_gtk3_videoctrls(GtkWidget *parent)
{
	/*assertions*/
	assert(parent != NULL);

	if(debug_level > 1)
		printf("GUVCVIEW: attaching video controls\n");

	int format_index = v4l2core_get_frame_format_index(v4l2core_get_requested_frame_format());

	if(format_index < 0)
	{
		gui_error("Guvcmjpg error", "invalid pixel format", 0);
		printf("GUVCVIEW: invalid pixel format\n");
	}

	int resolu_index = v4l2core_get_format_resolution_index(
		format_index,
		v4l2core_get_frame_width(),
		v4l2core_get_frame_height());

	if(resolu_index < 0)
	{
		gui_error("Guvcmjpg error", "invalid resolution index", 0);
		printf("GUVCVIEW: invalid resolution index\n");
	}

	GtkWidget *video_controls_grid = gtk_grid_new();
	gtk_widget_show (video_controls_grid);

	gtk_grid_set_column_homogeneous (GTK_GRID(video_controls_grid), FALSE);
	gtk_widget_set_hexpand (video_controls_grid, TRUE);
	gtk_widget_set_halign (video_controls_grid, GTK_ALIGN_FILL);

	gtk_grid_set_row_spacing (GTK_GRID(video_controls_grid), 4);
	gtk_grid_set_column_spacing (GTK_GRID (video_controls_grid), 4);
	gtk_container_set_border_width (GTK_CONTAINER (video_controls_grid), 2);

	char temp_str[20];
	int line = 0;
	int i =0;

	/*---- Devices ----*/
	GtkWidget *label_Device = gtk_label_new(_("Device:"));
#if GTK_VER_AT_LEAST(3,15)
	gtk_label_set_xalign(GTK_LABEL(label_Device), 1);
	gtk_label_set_yalign(GTK_LABEL(label_Device), 0.5);
#else
	gtk_misc_set_alignment (GTK_MISC (label_Device), 1, 0.5);
#endif

	gtk_grid_attach (GTK_GRID(video_controls_grid), label_Device, 0, line, 1, 1);

	gtk_widget_show (label_Device);

	set_wgtDevices_gtk3(gtk_combo_box_text_new());
	gtk_widget_set_halign (get_wgtDevices_gtk3(), GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (get_wgtDevices_gtk3(), TRUE);

	v4l2_device_list *device_list = v4l2core_get_device_list();

	if (device_list->num_devices < 1)
	{
		/*use current*/
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(get_wgtDevices_gtk3()),
			v4l2core_get_videodevice());
		gtk_combo_box_set_active(GTK_COMBO_BOX(get_wgtDevices_gtk3()),0);
	}
	else
	{
		for(i = 0; i < (device_list->num_devices); i++)
		{
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(get_wgtDevices_gtk3()),
				device_list->list_devices[i].name);
			if(device_list->list_devices[i].current)
				gtk_combo_box_set_active(GTK_COMBO_BOX(get_wgtDevices_gtk3()),i);
		}
	}
	gtk_grid_attach (GTK_GRID(video_controls_grid), get_wgtDevices_gtk3(), 1, line, 1 ,1);
	gtk_widget_show (get_wgtDevices_gtk3());
	g_signal_connect (GTK_COMBO_BOX_TEXT(get_wgtDevices_gtk3()), "changed",
		G_CALLBACK (devices_changed), NULL);

	/*---- Frame Rate ----*/
	line++;

	GtkWidget *label_FPS = gtk_label_new(_("Frame Rate:"));
#if GTK_VER_AT_LEAST(3,15)
	gtk_label_set_xalign(GTK_LABEL(label_FPS), 1);
	gtk_label_set_yalign(GTK_LABEL(label_FPS), 0.5);
#else
	gtk_misc_set_alignment (GTK_MISC (label_FPS), 1, 0.5);
#endif
	gtk_widget_show (label_FPS);

	gtk_grid_attach (GTK_GRID(video_controls_grid), label_FPS, 0, line, 1, 1);

	GtkWidget *wgtFrameRate = gtk_combo_box_text_new ();
	gtk_widget_set_halign (wgtFrameRate, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (wgtFrameRate, TRUE);

	gtk_widget_show (wgtFrameRate);
	gtk_widget_set_sensitive (wgtFrameRate, TRUE);

	int deffps=0;

	v4l2_stream_formats_t *list_stream_formats = v4l2core_get_formats_list();
	
	if (debug_level > 0)
		printf("GUVCVIEW: frame rates of resolution index %d = %d \n",
			resolu_index+1,
			list_stream_formats[format_index].list_stream_cap[resolu_index].numb_frates);
	for ( i = 0 ; i < list_stream_formats[format_index].list_stream_cap[resolu_index].numb_frates ; ++i)
	{
		g_snprintf(
			temp_str,
			18,
			"%i/%i fps",
			list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_denom[i],
			list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_num[i]);

		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wgtFrameRate), temp_str);

		if (( v4l2core_get_fps_num() == list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_num[i]) &&
			( v4l2core_get_fps_denom() == list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_denom[i]))
				deffps=i;//set selected
	}

	gtk_grid_attach (GTK_GRID(video_controls_grid), wgtFrameRate, 1, line, 1 ,1);

	gtk_combo_box_set_active(GTK_COMBO_BOX(wgtFrameRate), deffps);

	if (deffps==0)
	{
		if (list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_denom)
			v4l2core_define_fps(-1, list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_denom[0]);

		if (list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_num)
			v4l2core_define_fps(list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_num[0], -1);
	}

	g_signal_connect (GTK_COMBO_BOX_TEXT(wgtFrameRate), "changed",
		G_CALLBACK (frame_rate_changed), NULL);

	/*try to sync the device fps (capture thread must have started by now)*/
	v4l2core_request_framerate_update ();

	/*---- Resolution ----*/
	line++;

	GtkWidget *label_Resol = gtk_label_new(_("Resolution:"));
#if GTK_VER_AT_LEAST(3,15)
	gtk_label_set_xalign(GTK_LABEL(label_Resol), 1);
	gtk_label_set_yalign(GTK_LABEL(label_Resol), 0.5);
#else
	gtk_misc_set_alignment (GTK_MISC (label_Resol), 1, 0.5);
#endif
	gtk_grid_attach (GTK_GRID(video_controls_grid), label_Resol, 0, line, 1, 1);
	gtk_widget_show (label_Resol);

	GtkWidget *wgtResolution = gtk_combo_box_text_new ();
	gtk_widget_set_halign (wgtResolution, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (wgtResolution, TRUE);

	gtk_widget_show (wgtResolution);
	gtk_widget_set_sensitive (wgtResolution, TRUE);

	int defres=0;

	if (debug_level > 0)
		printf("GUVCVIEW: resolutions of format(%d) = %d \n",
			format_index+1,
			list_stream_formats[format_index].numb_res);
	for(i = 0 ; i < list_stream_formats[format_index].numb_res ; i++)
	{
		if (list_stream_formats[format_index].list_stream_cap[i].width > 0)
		{
			g_snprintf(
				temp_str,
				18,
				"%ix%i",
				list_stream_formats[format_index].list_stream_cap[i].width,
				list_stream_formats[format_index].list_stream_cap[i].height);

			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wgtResolution), temp_str);

			if ((v4l2core_get_frame_width() == list_stream_formats[format_index].list_stream_cap[i].width) &&
				(v4l2core_get_frame_height() == list_stream_formats[format_index].list_stream_cap[i].height))
					defres=i;//set selected resolution index
		}
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(wgtResolution), defres);

	gtk_grid_attach (GTK_GRID(video_controls_grid), wgtResolution, 1, line, 1 ,1);

	g_object_set_data (G_OBJECT (wgtResolution), "control_fps", wgtFrameRate);

	g_signal_connect (wgtResolution, "changed",
		G_CALLBACK (resolution_changed), NULL);


	/*---- Input Format ----*/
	line++;

	GtkWidget *label_InpType = gtk_label_new(_("Camera Output:"));
#if GTK_VER_AT_LEAST(3,15)
	gtk_label_set_xalign(GTK_LABEL(label_InpType), 1);
	gtk_label_set_yalign(GTK_LABEL(label_InpType), 0.5);
#else
	gtk_misc_set_alignment (GTK_MISC (label_InpType), 1, 0.5);
#endif

	gtk_widget_show (label_InpType);

	gtk_grid_attach (GTK_GRID(video_controls_grid), label_InpType, 0, line, 1, 1);

	GtkWidget *wgtInpType= gtk_combo_box_text_new ();
	gtk_widget_set_halign (wgtInpType, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (wgtInpType, TRUE);

	gtk_widget_show (wgtInpType);
	gtk_widget_set_sensitive (wgtInpType, TRUE);

	int fmtind=0;
	for (fmtind=0; fmtind < v4l2core_get_number_formats(); fmtind++)
	{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wgtInpType), list_stream_formats[fmtind].fourcc);
		if(v4l2core_get_requested_frame_format() == list_stream_formats[fmtind].format)
			gtk_combo_box_set_active(GTK_COMBO_BOX(wgtInpType), fmtind); /*set active*/
	}

	gtk_grid_attach (GTK_GRID(video_controls_grid), wgtInpType, 1, line, 1 ,1);

	//g_object_set_data (G_OBJECT (wgtInpType), "control_fps", wgtFrameRate);
	g_object_set_data (G_OBJECT (wgtInpType), "control_resolution", wgtResolution);

	g_signal_connect (GTK_COMBO_BOX_TEXT(wgtInpType), "changed",
		G_CALLBACK (format_changed), NULL);

	return 0;
}
