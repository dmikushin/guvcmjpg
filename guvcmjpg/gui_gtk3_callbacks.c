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

#include <gtk/gtk.h>
#include <glib.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
/* support for internationalization - i18n */
#include <glib/gi18n.h>

#include "gviewv4l2core.h"
#include "video_capture.h"
#include "gviewrender.h"
#include "gui.h"
#include "gui_gtk3.h"
#include "core_io.h"
#include "config.h"

extern int debug_level;

/*
 * delete event (close window)
 * args:
 *   widget - pointer to event widget
 *   event - pointer to event data
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: error code
 */
int delete_event (GtkWidget *widget, GdkEventConfigure *event, void *data)
{
	/* Terminate program */
	quit_callback(NULL);
	return 0;
}

/*
 * quit button clicked event
 * args:
 *    widget - pointer to widget that caused the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void quit_button_clicked(GtkWidget *widget, void *data)
{
	/* Terminate program */
	quit_callback(NULL);
}

/*
 * camera_button_menu toggled event
 * args:
 *   widget - pointer to event widget
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void camera_button_menu_changed (GtkWidget *item, void *data)
{
	int flag = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (item), "camera_default"));

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
		set_default_camera_button_action(flag);
}

/*
 * control default clicked event
 * args:
 *   widget - pointer to event widget
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void control_defaults_clicked (GtkWidget *item, void *data)
{
    v4l2core_set_control_defaults();

    gui_gtk3_update_controls_state();
}

/*
 * called from profile format combo in file dialog
 * args:
 *    chooser - format combo that caused the event
 *    file_dialog - chooser parent
 *
 * asserts:
 *    none
 *
 * returns: none
 */
static void profile_update_extension (GtkComboBox *chooser, GtkWidget *file_dialog)
{
	int format = gtk_combo_box_get_active (chooser);

	GtkFileFilter *filter = gtk_file_filter_new();

	switch(format)
	{
		case 1:
			gtk_file_filter_add_pattern(filter, "*.*");
			break;

		default:
		case 0:
			gtk_file_filter_add_pattern(filter, "*.gpfl");
			break;
	}

	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER (file_dialog), filter);
}

/*
 * control profile (load/save) clicked event
 * args:
 *   widget - pointer to event widget
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void controls_profile_clicked (GtkWidget *item, void *data)
{
	GtkWidget *FileDialog;

	int save_or_load = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (item), "profile_dialog"));

	if(debug_level > 0)
		printf("GUVCVIEW: Profile dialog (%d)\n", save_or_load);

	GtkWidget *main_window = get_main_window_gtk3();

	if (save_or_load > 0) /*save*/
	{
		FileDialog = gtk_file_chooser_dialog_new (_("Save Profile"),
			GTK_WINDOW(main_window),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			 _("_Cancel"), GTK_RESPONSE_CANCEL,
			 _("_Save"), GTK_RESPONSE_ACCEPT,
			NULL);

		gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (FileDialog), TRUE);

		gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (FileDialog),
			get_profile_name());
	}
	else /*load*/
	{
		FileDialog = gtk_file_chooser_dialog_new (_("Load Profile"),
			GTK_WINDOW(main_window),
			GTK_FILE_CHOOSER_ACTION_OPEN,
			 _("_Cancel"), GTK_RESPONSE_CANCEL,
			 _("_Open"), GTK_RESPONSE_ACCEPT,
			NULL);
	}

	/** create a file filter */
	GtkFileFilter *filter = gtk_file_filter_new();

	GtkWidget *FBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget *format_label = gtk_label_new(_("File Format:"));
	gtk_widget_set_halign (FBox, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (FBox, TRUE);
	gtk_widget_set_hexpand (format_label, FALSE);
	gtk_widget_show(FBox);
	gtk_widget_show(format_label);
	gtk_box_pack_start(GTK_BOX(FBox), format_label, FALSE, FALSE, 2);

	GtkWidget *FileFormat = gtk_combo_box_text_new ();
	gtk_widget_set_halign (FileFormat, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (FileFormat, TRUE);

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(FileFormat),_("gpfl  (*.gpfl)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(FileFormat),_("any (*.*)"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(FileFormat), 0);
	gtk_box_pack_start(GTK_BOX(FBox), FileFormat, FALSE, FALSE, 2);
	gtk_widget_show(FileFormat);

	gtk_file_filter_add_pattern(filter, "*.gpfl");

	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER (FileDialog), filter);
	gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER (FileDialog), FBox);

	g_signal_connect (GTK_COMBO_BOX(FileFormat), "changed",
		G_CALLBACK (profile_update_extension), FileDialog);


	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (FileDialog),
		get_profile_path());

	if (gtk_dialog_run (GTK_DIALOG (FileDialog)) == GTK_RESPONSE_ACCEPT)
	{
		/*Save Controls Data*/
		const char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (FileDialog));

		if(save_or_load > 0)
		{
			v4l2core_save_control_profile(filename);
		}
		else
		{
			v4l2core_load_control_profile(filename);
			gui_gtk3_update_controls_state();
		}

		char *basename = get_file_basename(filename);
		if(basename)
		{
			set_profile_name(basename);
			free(basename);
		}
		char *pathname = get_file_pathname(filename);
		if(pathname)
		{
			set_profile_path(pathname);
			free(pathname);
		}
	}
	gtk_widget_destroy (FileDialog);
}

/*
 * photo suffix toggled event
 * args:
 *    item - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void photo_sufix_toggled (GtkWidget *item, void *data)
{
  	int flag = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)) ? 1 : 0;
	set_photo_sufix_flag(flag);

	/*update config*/
	config_t *my_config = config_get();
	my_config->photo_sufix = flag;
}

/*
 * called from photo format combo in file dialog
 * args:
 *    chooser - format combo that caused the event
 *    file_dialog - chooser parent
 *
 * asserts:
 *    none
 *
 * returns: none
 */
static void photo_update_extension (GtkComboBox *chooser, GtkWidget *file_dialog)
{
	int format = gtk_combo_box_get_active (chooser);

	set_photo_format(format);

	char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (file_dialog));
	char *basename = get_file_basename(filename);

	GtkFileFilter *filter = gtk_file_filter_new();

	switch(format)
	{
		case IMG_FMT_RAW:
			gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_dialog),
				set_file_extension(basename, "raw"));
			gtk_file_filter_add_pattern(filter, "*.raw");
			break;
		case IMG_FMT_PNG:
			gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_dialog),
				set_file_extension(basename, "png"));
			gtk_file_filter_add_pattern(filter, "*.png");
			break;
		case IMG_FMT_BMP:
			gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_dialog),
				set_file_extension(basename, "bmp"));
			gtk_file_filter_add_pattern(filter, "*.bmp");
			break;
		default:
		case IMG_FMT_JPG:
			gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (file_dialog),
				set_file_extension(basename, "jpg"));
			gtk_file_filter_add_pattern(filter, "*.jpg");
			break;
	}

	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER (file_dialog), filter);

	if(filename)
		free(filename);
	if(basename)
		free(basename);
}

/*
 * photo file clicked event
 * args:
 *   item - pointer to widget that generated the event
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void photo_file_clicked (GtkWidget *item, void *data)
{
	GtkWidget *FileDialog;

	GtkWidget *main_window = get_main_window_gtk3();

	FileDialog = gtk_file_chooser_dialog_new (_("Photo file name"),
			GTK_WINDOW(main_window),
			GTK_FILE_CHOOSER_ACTION_SAVE,
			_("_Cancel"), GTK_RESPONSE_CANCEL,
			_("_Save"), GTK_RESPONSE_ACCEPT,
			NULL);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (FileDialog), TRUE);

	/** create a file filter */
	GtkFileFilter *filter = gtk_file_filter_new();

	GtkWidget *FBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkWidget *format_label = gtk_label_new(_("File Format:"));
	gtk_widget_set_halign (FBox, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (FBox, TRUE);
	gtk_widget_set_hexpand (format_label, FALSE);
	gtk_widget_show(FBox);
	gtk_widget_show(format_label);
	gtk_box_pack_start(GTK_BOX(FBox), format_label, FALSE, FALSE, 2);

	GtkWidget *ImgFormat = gtk_combo_box_text_new ();
	gtk_widget_set_halign (ImgFormat, GTK_ALIGN_FILL);
	gtk_widget_set_hexpand (ImgFormat, TRUE);

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ImgFormat),_("Raw  (*.raw)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ImgFormat),_("Jpeg (*.jpg)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ImgFormat),_("Png  (*.png)"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ImgFormat),_("Bmp  (*.bmp)"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(ImgFormat), get_photo_format());
	gtk_box_pack_start(GTK_BOX(FBox), ImgFormat, FALSE, FALSE, 2);
	gtk_widget_show(ImgFormat);

	/**add a pattern to the filter*/
	switch(get_photo_format())
	{
		case IMG_FMT_RAW:
			gtk_file_filter_add_pattern(filter, "*.raw");
			break;
		case IMG_FMT_PNG:
			gtk_file_filter_add_pattern(filter, "*.png");
			break;
		case IMG_FMT_BMP:
			gtk_file_filter_add_pattern(filter, "*.bmp");
			break;
		default:
		case IMG_FMT_JPG:
			gtk_file_filter_add_pattern(filter, "*.jpg");
			break;
	}

	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER (FileDialog), filter);
	gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER (FileDialog), FBox);

	g_signal_connect (GTK_COMBO_BOX(ImgFormat), "changed",
		G_CALLBACK (photo_update_extension), FileDialog);

	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (FileDialog),
		get_photo_name());

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (FileDialog),
		get_photo_path());

	if (gtk_dialog_run (GTK_DIALOG (FileDialog)) == GTK_RESPONSE_ACCEPT)
	{
		const char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (FileDialog));

		char *basename = get_file_basename(filename);
		if(basename)
		{
			set_photo_name(basename);
			free(basename);
		}
		char *pathname = get_file_pathname(filename);
		if(pathname)
		{
			set_photo_path(pathname);
			free(pathname);
		}
	}
	gtk_widget_destroy (FileDialog);
}

/*
 * capture image button clicked event
 * args:
 *   button - widget that generated the event
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void capture_image_clicked (GtkButton *button, void *data)
{
	int is_photo_timer = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (button), "control_info"));

	if(is_photo_timer)
	{
		stop_photo_timer();
		gtk_button_set_label(button, _("Cap. Image (I)"));
	}
	else
		video_capture_save_image();
}

/*
 * pan/tilt step changed
 * args:
 *    spin - spinbutton that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns:
 *    none
 */
void pan_tilt_step_changed (GtkSpinButton *spin, void *data)
{
	int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (spin), "control_info"));

	int val = gtk_spin_button_get_value_as_int (spin);

	if(id == V4L2_CID_PAN_RELATIVE)
		v4l2core_set_pan_step(val);
	if(id == V4L2_CID_TILT_RELATIVE)
		v4l2core_set_tilt_step(val);
}

/*
 * Pan Tilt button 1 clicked
 * args:
 *    button - button that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void button_PanTilt1_clicked (GtkButton * Button, void *data)
{
    int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (Button), "control_info"));

    v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

	if(id == V4L2_CID_PAN_RELATIVE)
		control->value = v4l2core_get_pan_step();
	else
		control->value = v4l2core_get_tilt_step();

    if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting pan/tilt\n");
}

/*
 * Pan Tilt button 2 clicked
 * args:
 *    button - button that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void button_PanTilt2_clicked (GtkButton * Button, void *data)
{
    int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (Button), "control_info"));

    v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

    if(id == V4L2_CID_PAN_RELATIVE)
		control->value =  - v4l2core_get_pan_step();
	else
		control->value =  - v4l2core_get_tilt_step();

    if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting pan/tilt\n");
}

/*
 * generic button clicked
 * args:
 *    button - button that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void button_clicked (GtkButton * Button, void *data)
{
    int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (Button), "control_info"));

    v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

	control->value = 1;

    if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting button value\n");

	gui_gtk3_update_controls_state();
}

#ifdef V4L2_CTRL_TYPE_STRING
/*
 * a string control apply button clicked
 * args:
 *    button - button that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    control->string not null
 *
 * returns: none
 */
void string_button_clicked(GtkButton * Button, void *data)
{
	int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (Button), "control_info"));
	GtkWidget *entry = (GtkWidget *) g_object_get_data (G_OBJECT (Button), "control_entry");

	v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

	assert(control->string != NULL);

	strncpy(control->string, gtk_entry_get_text(GTK_ENTRY(entry)), control->control.maximum);

	if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting string value\n");
}
#endif

#ifdef V4L2_CTRL_TYPE_INTEGER64
/*
 * a int64 control apply button clicked
 * args:
 *    button - button that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void int64_button_clicked(GtkButton * Button, void *data)
{
	int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (Button), "control_info"));
	GtkWidget *entry = (GtkWidget *) g_object_get_data (G_OBJECT (Button), "control_entry");

	v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

	char* text_input = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
	text_input = g_strstrip(text_input);
	if( g_str_has_prefix(text_input, "0x")) //hex format
	{
		text_input = g_strcanon(text_input, "0123456789ABCDEFabcdef", '\0');
		control->value64 = g_ascii_strtoll(text_input, NULL, 16);
	}
	else //decimal or hex ?
	{
		text_input = g_strcanon(text_input, "0123456789ABCDEFabcdef", '\0');
		control->value64 = g_ascii_strtoll(text_input, NULL, 0);
	}
	g_free(text_input);

	if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting string value\n");

}
#endif

#ifdef V4L2_CTRL_TYPE_BITMASK
/*
 * a bitmask control apply button clicked
 * args:
 *    button - button that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void bitmask_button_clicked(GtkButton * Button, void *data)
{
	int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (Button), "control_info"));
	GtkWidget *entry = (GtkWidget *) g_object_get_data (G_OBJECT (Button), "control_entry");

	v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

	char* text_input = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
	text_input = g_strcanon(text_input,"0123456789ABCDEFabcdef", '\0');
	control->value = (int32_t) g_ascii_strtoll(text_input, NULL, 16);
	g_free(text_input);

	if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting string value\n");
}
#endif

/*
 * slider changed event
 * args:
 *    range - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void slider_changed (GtkRange * range, void *data)
{
    int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (range), "control_info"));
    v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

    int val = (int) gtk_range_get_value (range);

    control->value = val;

    if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting slider value\n");

   /*
    if(widget2)
    {
        //disable widget signals
        g_signal_handlers_block_by_func(GTK_SPIN_BUTTON(widget2),
            G_CALLBACK (spin_changed), data);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(widget2), control->value);
        //enable widget signals
        g_signal_handlers_unblock_by_func(GTK_SPIN_BUTTON(widget2),
            G_CALLBACK (spin_changed), data);
    }
	*/
}

/*
 * spin changed event
 * args:
 *    spin - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void spin_changed (GtkSpinButton * spin, void *data)
{
    int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (spin), "control_info"));
    v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

	int val = gtk_spin_button_get_value_as_int (spin);
    control->value = val;

     if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting spin value\n");

	/*
    if(widget)
    {
        //disable widget signals
        g_signal_handlers_block_by_func(GTK_SCALE (widget),
            G_CALLBACK (slider_changed), data);
        gtk_range_set_value (GTK_RANGE (widget), control->value);
        //enable widget signals
        g_signal_handlers_unblock_by_func(GTK_SCALE (widget),
            G_CALLBACK (slider_changed), data);
    }
	*/

}

/*
 * combo box changed event
 * args:
 *    combo - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void combo_changed (GtkComboBox * combo, void *data)
{
    int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (combo), "control_info"));
    v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

    int index = gtk_combo_box_get_active (combo);
    control->value = control->menu[index].index;

	if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting menu value\n");

	gui_gtk3_update_controls_state();
}

/*
 * bayer pixel order combo box changed event
 * args:
 *    combo - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void bayer_pix_ord_changed (GtkComboBox * combo, void *data)
{
	//int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (combo), "control_info"));

	int index = gtk_combo_box_get_active (combo);
	v4l2core_set_bayer_pix_order(index);
}

/*
 * check box changed event
 * args:
 *    toggle - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void check_changed (GtkToggleButton *toggle, void *data)
{
    int id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (toggle), "control_info"));
    v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

    int val = gtk_toggle_button_get_active (toggle) ? 1 : 0;

    control->value = val;

	if(v4l2core_set_control_value_by_id(id))
		fprintf(stderr, "GUVCVIEW: error setting menu value\n");

    if(id == V4L2_CID_DISABLE_PROCESSING_LOGITECH)
    {
        if (control->value > 0)
			v4l2core_set_isbayer(1);
        else
			v4l2core_set_isbayer(0);

        /*
         * must restart stream and requeue
         * the buffers for changes to take effect
         * (updating fps provides all that is needed)
         */
        v4l2core_request_framerate_update ();
    }

    gui_gtk3_update_controls_state();
}

/*
 * device list box changed event
 * args:
 *    wgtDevices - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void devices_changed (GtkComboBox *wgtDevices, void *data)
{
	GError *error = NULL;

	int index = gtk_combo_box_get_active(wgtDevices);
	if(index == v4l2core_get_this_device_index())
		return;

	v4l2_device_list *device_list = v4l2core_get_device_list();

	GtkWidget *restartdialog = gtk_dialog_new_with_buttons (_("start new"),
		GTK_WINDOW(get_main_window_gtk3()),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("restart"),
		GTK_RESPONSE_ACCEPT,
		_("new"),
		GTK_RESPONSE_REJECT,
		_("cancel"),
		GTK_RESPONSE_CANCEL,
		NULL);

	GtkWidget *content_area = gtk_dialog_get_content_area (GTK_DIALOG (restartdialog));
	GtkWidget *message = gtk_label_new (_("launch new process or restart?.\n\n"));
	gtk_container_add (GTK_CONTAINER (content_area), message);
	gtk_widget_show_all(restartdialog);

	gint result = gtk_dialog_run (GTK_DIALOG (restartdialog));

	/*check device index only after dialog response*/
	char videodevice[30];
	strncpy(videodevice, device_list->list_devices[index].device, 29);
	gchar *command = g_strjoin("",
		g_get_prgname(),
		" --device=",
		videodevice,
		NULL);

	switch (result)
	{
		case GTK_RESPONSE_ACCEPT: /*FIXME: restart or reset device without closing the app*/
			if(debug_level > 1)
				printf("GUVCVIEW: spawning new process: '%s'\n", command);
			/*spawn new process*/
			if(!(g_spawn_command_line_async(command, &error)))
			{
				fprintf(stderr, "GUVCVIEW: spawn failed: %s\n", error->message);
				g_error_free( error );
			}
			else
				quit_callback(NULL);
			break;
		case GTK_RESPONSE_REJECT:
			if(debug_level > 1)
				printf("GUVCVIEW: spawning new process: '%s'\n", command);
			/*spawn new process*/
			if(!(g_spawn_command_line_async(command, &error)))
			{
				fprintf(stderr, "GUVCVIEW: spawn failed: %s\n", error->message);
				g_error_free( error );
			}
			break;
		default:
			/* do nothing since dialog was canceled*/
			break;
	}
	/*reset to current device*/
	gtk_combo_box_set_active(GTK_COMBO_BOX(wgtDevices), v4l2core_get_this_device_index());

	gtk_widget_destroy (restartdialog);
	g_free(command);
}

/*
 * frame rate list box changed event
 * args:
 *    wgtFrameRate - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void frame_rate_changed (GtkComboBox *wgtFrameRate, void *data)
{
	int format_index = v4l2core_get_frame_format_index(v4l2core_get_requested_frame_format());

	int resolu_index = v4l2core_get_format_resolution_index(
		format_index,
		v4l2core_get_frame_width(),
		v4l2core_get_frame_height());

	int index = gtk_combo_box_get_active (wgtFrameRate);

	v4l2_stream_formats_t *list_stream_formats = v4l2core_get_formats_list();
	
	int fps_denom = list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_denom[index];
	int fps_num = list_stream_formats[format_index].list_stream_cap[resolu_index].framerate_num[index];
	
	v4l2core_define_fps(fps_num, fps_denom);

	int fps[2] = {fps_num, fps_denom};
	gui_set_fps(fps);

	v4l2core_request_framerate_update ();
}

/*
 * resolution list box changed event
 * args:
 *    wgtResolution - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void resolution_changed (GtkComboBox *wgtResolution, void *data)
{
	int format_index = v4l2core_get_frame_format_index(v4l2core_get_requested_frame_format());

	int cmb_index = gtk_combo_box_get_active(wgtResolution);

	GtkWidget *wgtFrameRate = (GtkWidget *) g_object_get_data (G_OBJECT (wgtResolution), "control_fps");

	char temp_str[20];

	/*disable fps combobox signals*/
	g_signal_handlers_block_by_func(GTK_COMBO_BOX_TEXT(wgtFrameRate), G_CALLBACK (frame_rate_changed), NULL);
	/* clear out the old fps list... */
	GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model (GTK_COMBO_BOX(wgtFrameRate)));
	gtk_list_store_clear(store);

	v4l2_stream_formats_t *list_stream_formats = v4l2core_get_formats_list();
	
	int width = list_stream_formats[format_index].list_stream_cap[cmb_index].width;
	int height = list_stream_formats[format_index].list_stream_cap[cmb_index].height;

	/*check if frame rate is available at the new resolution*/
	int i=0;
	int deffps=0;

	for ( i = 0 ; i < list_stream_formats[format_index].list_stream_cap[cmb_index].numb_frates ; i++)
	{
		g_snprintf(
			temp_str,
			18,
			"%i/%i fps",
			list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_denom[i],
			list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_num[i]);

		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wgtFrameRate), temp_str);

		if (( v4l2core_get_fps_num() == list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_num[i]) &&
			( v4l2core_get_fps_denom() == list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_denom[i]))
				deffps=i;
	}

	/*set default fps in combo*/
	gtk_combo_box_set_active(GTK_COMBO_BOX(wgtFrameRate), deffps);

	/*enable fps combobox signals*/
	g_signal_handlers_unblock_by_func(GTK_COMBO_BOX_TEXT(wgtFrameRate), G_CALLBACK (frame_rate_changed), NULL);

	if (list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_num)
		v4l2core_define_fps(list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_num[deffps], -1);

	if (list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_denom)
		v4l2core_define_fps(-1, list_stream_formats[format_index].list_stream_cap[cmb_index].framerate_denom[deffps]);

	/*change resolution (try new format and reset render)*/
	v4l2core_prepare_new_resolution(width, height);

	request_format_update();

	/*update the config data*/
	config_t *my_config = config_get();

	my_config->width = width;
	my_config->height= height;
}

/*
 * device pixel format list box changed event
 * args:
 *    wgtInpType - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void format_changed(GtkComboBox *wgtInpType, void *data)
{
	char temp_str[20];
	int index = gtk_combo_box_get_active(wgtInpType);

	//GtkWidget *wgtFrameRate = (GtkWidget *) g_object_get_data (G_OBJECT (wgtInpType), "control_fps");
	GtkWidget *wgtResolution = (GtkWidget *) g_object_get_data (G_OBJECT (wgtInpType), "control_resolution");

	int i=0;
	int defres = 0;

	/*disable resolution combobox signals*/
	g_signal_handlers_block_by_func(GTK_COMBO_BOX_TEXT(wgtResolution), G_CALLBACK (resolution_changed), NULL);

	/* clear out the old resolution list... */
	GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model (GTK_COMBO_BOX(wgtResolution)));
	gtk_list_store_clear(store);

	v4l2_stream_formats_t *list_stream_formats = v4l2core_get_formats_list();
		
	int format = list_stream_formats[index].format;

	/*update config*/
	config_t *my_config = config_get();
	strncpy(my_config->format, list_stream_formats[index].fourcc, 4);

	/*redraw resolution combo for new format*/
	for(i = 0 ; i < list_stream_formats[index].numb_res ; i++)
	{
		if (list_stream_formats[index].list_stream_cap[i].width > 0)
		{
			g_snprintf(
				temp_str,
				18,
				"%ix%i",
				list_stream_formats[index].list_stream_cap[i].width,
				list_stream_formats[index].list_stream_cap[i].height);

			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wgtResolution), temp_str);

			if ((v4l2core_get_frame_width() == list_stream_formats[index].list_stream_cap[i].width) &&
				(v4l2core_get_frame_height() == list_stream_formats[index].list_stream_cap[i].height))
					defres=i;//set selected resolution index
		}
	}

	/*enable resolution combobox signals*/
	g_signal_handlers_unblock_by_func(GTK_COMBO_BOX_TEXT(wgtResolution), G_CALLBACK (resolution_changed), NULL);

	/*prepare new format*/
	v4l2core_prepare_new_format(format);
	/*change resolution*/
	gtk_combo_box_set_active(GTK_COMBO_BOX(wgtResolution), defres);
}

/*
 * render fx filter changed event
 * args:
 *    toggle - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void render_fx_filter_changed(GtkToggleButton *toggle, void *data)
{
	int filter = GPOINTER_TO_INT(g_object_get_data (G_OBJECT (toggle), "filt_info"));

	uint32_t mask = gtk_toggle_button_get_active (toggle) ?
			get_render_fx_mask() | filter :
			get_render_fx_mask() & ~filter;

	set_render_fx_mask(mask);
}

/*
 * software autofocus checkbox changed event
 * args:
 *    toggle - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void autofocus_changed (GtkToggleButton * toggle, void *data)
{
	int val = gtk_toggle_button_get_active (toggle) ? 1 : 0;

	GtkWidget *wgtFocus_slider = (GtkWidget *) g_object_get_data (G_OBJECT (toggle), "control_entry");
	GtkWidget *wgtFocus_spin = (GtkWidget *) g_object_get_data (G_OBJECT (toggle), "control2_entry");
	/*if autofocus disable manual focus control*/
	gtk_widget_set_sensitive (wgtFocus_slider, !val);
	gtk_widget_set_sensitive (wgtFocus_spin, !val);

	set_soft_autofocus(val);

}

/*
 * software autofocus button clicked event
 * args:
 *    button - widget that generated the event
 *    data - pointer to user data
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void setfocus_clicked (GtkButton * button, void *data)
{
	set_soft_focus(1);
}

/*
 * gtk3 window key pressed event
 * args:
 *   win - pointer to widget (main window) where event ocurred
 *   event - pointer to GDK key event structure
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: true if we handled the event or false otherwise
 */
gboolean window_key_pressed (GtkWidget *win, GdkEventKey *event, void *data)
{
	/* If we have modifiers, and either Ctrl, Mod1 (Alt), or any
	 * of Mod3 to Mod5 (Mod2 is num-lock...) are pressed, we
	 * let Gtk+ handle the key */
	//printf("GUVCVIEW: key pressed (key:%i)\n", event->keyval);
	if (event->state != 0
		&& ((event->state & GDK_CONTROL_MASK)
		|| (event->state & GDK_MOD1_MASK)
		|| (event->state & GDK_MOD3_MASK)
		|| (event->state & GDK_MOD4_MASK)
		|| (event->state & GDK_MOD5_MASK)))
		return FALSE;

    if(v4l2core_has_pantilt_id())
    {
		int id = 0;
		int value = 0;

        switch (event->keyval)
        {
            case GDK_KEY_Down:
            case GDK_KEY_KP_Down:
				id = V4L2_CID_TILT_RELATIVE;
				value = v4l2core_get_tilt_step();
				break;

            case GDK_KEY_Up:
            case GDK_KEY_KP_Up:
				id = V4L2_CID_TILT_RELATIVE;
				value = - v4l2core_get_tilt_step();
				break;

            case GDK_KEY_Left:
            case GDK_KEY_KP_Left:
				id = V4L2_CID_PAN_RELATIVE;
				value = v4l2core_get_pan_step();
				break;

            case GDK_KEY_Right:
            case GDK_KEY_KP_Right:
                id = V4L2_CID_PAN_RELATIVE;
				value = - v4l2core_get_pan_step();
				break;

            default:
                break;
        }

        if(id != 0 && value != 0)
        {
			v4l2_ctrl_t *control = v4l2core_get_control_by_id(id);

			if(control)
			{
				control->value =  value;

				if(v4l2core_set_control_value_by_id(id))
					fprintf(stderr, "GUVCVIEW: error setting pan/tilt value\n");

				return TRUE;
			}
		}
    }

    switch (event->keyval)
    {
        case GDK_KEY_WebCam:
			/* camera button pressed */
			if (get_default_camera_button_action() == DEF_ACTION_IMAGE)
				gui_click_image_capture_button_gtk3();
			else
				gui_click_video_capture_button_gtk3();
			return TRUE;

		case GDK_KEY_V:
		case GDK_KEY_v:
			gui_click_video_capture_button_gtk3();
			return TRUE;

		case GDK_KEY_I:
		case GDK_KEY_i:
			gui_click_image_capture_button_gtk3();
			return TRUE;

	}

    return FALSE;
}

/*
 * device list events timer callback
 * args:
 *   data - pointer to user data
 *
 * asserts:
 *   none
 *
 * returns: true if timer is to be reset or false otherwise
 */
gboolean check_device_events(gpointer data)
{
	if(v4l2core_check_device_list_events())
	{
		/*update device list*/
		g_signal_handlers_block_by_func(GTK_COMBO_BOX_TEXT(get_wgtDevices_gtk3()),
                G_CALLBACK (devices_changed), NULL);

		GtkListStore *store = GTK_LIST_STORE(gtk_combo_box_get_model (GTK_COMBO_BOX(get_wgtDevices_gtk3())));
		gtk_list_store_clear(store);

		v4l2_device_list *device_list = v4l2core_get_device_list();
		int i = 0;
        for(i = 0; i < (device_list->num_devices); i++)
		{
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(get_wgtDevices_gtk3()),
				device_list->list_devices[i].name);
			if(device_list->list_devices[i].current)
				gtk_combo_box_set_active(GTK_COMBO_BOX(get_wgtDevices_gtk3()),i);
		}

		g_signal_handlers_unblock_by_func(GTK_COMBO_BOX_TEXT(get_wgtDevices_gtk3()),
                G_CALLBACK (devices_changed), NULL);
	}

	return (TRUE);
}
