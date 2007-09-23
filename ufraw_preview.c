/*
 * UFRaw - Unidentified Flying Raw converter for digital camera images
 *
 * ufraw_preview.c - GUI for controlling all the image manipulations
 * Copyright 2004-2007 by Udi Fuchs
 *
 * based on the GIMP plug-in by Pawel T. Jochym jochym at ifj edu pl,
 *
 * based on the GIMP plug-in by Dave Coffin
 * http://www.cybercom.net/~dcoffin/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation. You should have received
 * a copy of the license along with this program.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "ufraw.h"
#include "curveeditor_widget.h"
//#undef HAVE_GTKIMAGEVIEW
#ifdef HAVE_GTKIMAGEVIEW
#include <gtkimageview/gtkimagescrollwin.h>
#include <gtkimageview/gtkimageview.h>
#endif
#if GTK_CHECK_VERSION(2,6,0)
void ufraw_chooser_toggle(GtkToggleButton *button, GtkFileChooser *filechooser);
#endif

extern GtkFileChooser *ufraw_raw_chooser(conf_data *conf,
					 const char *defPath,
					 const gchar *label,
					 GtkWindow *toplevel,
					 const gchar *cancel,
					 gboolean multiple);

/* Set to huge number so that the preview size is set by the screen size */
static const int def_preview_width = 9000;
static const int def_preview_height = 9000;
#define raw_his_size 320
#define live_his_size 256
#define min_scale 2
#define max_scale 20

enum { pixel_format, percent_format };
enum { without_zone, with_zone };

static char *expanderText[] = { N_("Raw histogram"), N_("Live histogram"), NULL };

typedef struct {
    GtkLabel *labels[5];
    int num;
    int format;
    gboolean zonep;                 /* non-zero to display value/zone */
} colorLabels;

typedef enum { render_default, render_overexposed, render_underexposed
	} RenderModeType;

typedef enum { cancel_button, ok_button, save_button, save_as_button,
	gimp_button, delete_button, num_buttons } ControlButtons;
typedef enum { spot_cursor, crop_cursor,
    left_cursor, right_cursor, top_cursor, bottom_cursor,
    top_left_cursor, top_right_cursor, bottom_left_cursor, bottom_right_cursor,
    move_cursor, cursor_num } CursorType;

static const GdkCursorType Cursors[cursor_num] = {
    GDK_HAND2, GDK_CROSSHAIR,
    GDK_LEFT_SIDE, GDK_RIGHT_SIDE, GDK_TOP_SIDE, GDK_BOTTOM_SIDE,
    GDK_TOP_LEFT_CORNER, GDK_TOP_RIGHT_CORNER,
    GDK_BOTTOM_LEFT_CORNER, GDK_BOTTOM_RIGHT_CORNER,
    GDK_FLEUR };

// Response can be any unique positive integer
#define UFRAW_RESPONSE_DELETE 1
#define UFRAW_NO_RESPONSE 2

/* All the "global" information is here: */
typedef struct {
    ufraw_data *UF;
    conf_data SaveConfig;
    char initialWB[max_name];
    double initialTemperature, initialGreen;
    double initialChanMul[4];
    int raw_his[raw_his_size][4];
    GList *WBPresets; /* List of WB presets in WBCombo*/
    /* Map from interpolation combobox index to real values */
    int InterpolationTable[num_interpolations];
    /* combobox index */
    int Interpolation;
    GdkPixbuf *PreviewPixbuf;
    GdkCursor *Cursor[cursor_num];
    /* Remember the Gtk Widgets that we need to access later */
    GtkWidget *Controls, *PreviewWidget, *RawHisto, *LiveHisto;
    GtkLabel *DarkFrameLabel;
    GtkWidget *BaseCurveWidget, *CurveWidget, *BlackLabel;
    GtkComboBox *WBCombo, *BaseCurveCombo, *CurveCombo,
		*ProfileCombo[profile_types];
    GtkTable *SpotTable;
    GtkLabel *SpotPatch;
    colorLabels *SpotLabels, *AvrLabels, *DevLabels, *OverLabels, *UnderLabels;
    GtkToggleButton *AutoExposureButton, *AutoBlackButton;
    GtkButton *AutoCurveButton;
    GtkWidget *ResetWBButton, *ResetGammaButton, *ResetLinearButton;
    GtkWidget *ResetExposureButton, *ResetSaturationButton;
    GtkWidget *ResetThresholdButton;
    GtkWidget *ResetBlackButton, *ResetBaseCurveButton, *ResetCurveButton;
    GtkWidget *UseMatrixButton;
    GtkWidget *ControlButton[num_buttons];
    guint16 ButtonMnemonic[num_buttons];
    GtkTooltips *ToolTips;
    GtkProgressBar *ProgressBar;
    GtkSpinButton *CropX1Spin;
    GtkSpinButton *CropY1Spin;
    GtkSpinButton *CropX2Spin;
    GtkSpinButton *CropY2Spin;
    GtkSpinButton *ShrinkSpin;
    GtkSpinButton *HeightSpin;
    GtkSpinButton *WidthSpin;
    /* We need the adjustments for update_scale() */
    GtkAdjustment *WBTuningAdjustment;
    GtkAdjustment *TemperatureAdjustment;
    GtkAdjustment *GreenAdjustment;
    GtkAdjustment *ChannelAdjustment[4];
    GtkAdjustment *GammaAdjustment;
    GtkAdjustment *LinearAdjustment;
    GtkAdjustment *ExposureAdjustment;
    GtkAdjustment *ThresholdAdjustment;
    GtkAdjustment *SaturationAdjustment;
    GtkAdjustment *CropX1Adjustment;
    GtkAdjustment *CropY1Adjustment;
    GtkAdjustment *CropX2Adjustment;
    GtkAdjustment *CropY2Adjustment;
    GtkAdjustment *ZoomAdjustment;
    GtkAdjustment *ShrinkAdjustment;
    GtkAdjustment *HeightAdjustment;
    GtkAdjustment *WidthAdjustment;
    long (*SaveFunc)();
    RenderModeType RenderMode;
    int RenderLine;
    UFRawPhase fromPhase;
    /* Some actions update the progress bar while working, but meanwhile we
     * want to freeze all other actions. After we thaw the dialog we must
     * call update_scales() which was also frozen. */
    gboolean FreezeDialog;
    /* Since the event-box can be larger than the preview pixbuf we need: */
    gboolean PreviewButtonPressed, SpotDraw;
    int SpotX1, SpotY1, SpotX2, SpotY2;
    CursorType CropMotionType;
    int DrawnCropX1, DrawnCropX2, DrawnCropY1, DrawnCropY2;
    double shrink, height, width;
    gboolean OptionsChanged;
    int PageNum;
    /* Original aspect ratio (0) or actual aspect ratio */
    float AspectRatio;
    /* True if aspect ratio is locked */
    gboolean LockAspect;
    /* The aspect ratio entry field */
    GtkEntry *AspectEntry;
    /* Mouse coordinates in previous frame (used when dragging crop area) */
    int OldMouseX, OldMouseY;
    int OverUnderTicker;
    /* The event source number when the highlight blink function is enabled. */
    guint BlinkTimer;
} preview_data;


/* These #defines are not very elegant, but otherwise things get tooo long */
#define CFG data->UF->conf
#define RC data->SaveConfig
#define Developer data->UF->developer
#define CFG_cameraCurve (CFG->BaseCurve[camera_curve].m_numAnchors>0)

enum { base_curve, luminosity_curve };

static preview_data *get_preview_data(void *object)
{
    GtkWidget *widget;
    if (GTK_IS_ADJUSTMENT(object)) {
	widget = g_object_get_data(object, "Parent-Widget");
    } else if (GTK_IS_MENU(object)) {
	widget = g_object_get_data(G_OBJECT(object), "Parent-Widget");
    } else if (GTK_IS_MENU_ITEM(object)) {
	GtkWidget *menu = gtk_widget_get_ancestor(object, GTK_TYPE_MENU);
	widget = g_object_get_data(G_OBJECT(menu), "Parent-Widget");
    } else {
	widget = object;
    }
    GtkWidget *parentWindow = gtk_widget_get_toplevel(widget);
    preview_data *data =
	    g_object_get_data(G_OBJECT(parentWindow), "Preview-Data");
    return data;
}

void ufraw_focus(void *window, gboolean focus)
{
    if (focus) {
        GtkWindow *parentWindow = (void *)ufraw_message(UFRAW_SET_PARENT, (char *)window);
	g_object_set_data(G_OBJECT(window), "WindowParent", parentWindow);
	if (parentWindow!=NULL)
	    gtk_window_set_accept_focus(GTK_WINDOW(parentWindow), FALSE);
    } else {
	GtkWindow *parentWindow = g_object_get_data(G_OBJECT(window),
		"WindowParent");
	ufraw_message(UFRAW_SET_PARENT, (char *)parentWindow);
	if (parentWindow!=NULL)
	    gtk_window_set_accept_focus(GTK_WINDOW(parentWindow), TRUE);
    }
}

void ufraw_messenger(char *message,  void *parentWindow)
{
    GtkDialog *dialog;

    if (parentWindow==NULL) {
	ufraw_batch_messenger(message);
    } else {
	dialog = GTK_DIALOG(gtk_message_dialog_new(GTK_WINDOW(parentWindow),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, message));
	gtk_dialog_run(dialog);
	gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void load_curve(GtkWidget *widget, long curveType)
{
    preview_data *data = get_preview_data(widget);
    GtkFileChooser *fileChooser;
    GtkFileFilter *filter;
    GSList *list, *saveList;
    CurveData c;
    char *cp;

    if (data->FreezeDialog) return;
    if ( (curveType==base_curve && CFG->BaseCurveCount>=max_curves) ||
	 (curveType==luminosity_curve && CFG->curveCount>=max_curves) ) {
	ufraw_message(UFRAW_ERROR, _("No more room for new curves."));
	return;
    }
    fileChooser = GTK_FILE_CHOOSER(gtk_file_chooser_dialog_new(_("Load curve"),
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL));
    ufraw_focus(fileChooser, TRUE);
    gtk_file_chooser_set_select_multiple(fileChooser, TRUE);
#if GTK_CHECK_VERSION(2,6,0)
    gtk_file_chooser_set_show_hidden(fileChooser, FALSE);
    GtkWidget *button = gtk_check_button_new_with_label(_("Show hidden files"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
    g_signal_connect(G_OBJECT(button), "toggled",
	    G_CALLBACK(ufraw_chooser_toggle), fileChooser);
    gtk_file_chooser_set_extra_widget(fileChooser, button);
#endif
    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("All curve formats"));
    gtk_file_filter_add_pattern(filter, "*.ntc");
    gtk_file_filter_add_pattern(filter, "*.NTC");
    gtk_file_filter_add_pattern(filter, "*.ncv");
    gtk_file_filter_add_pattern(filter, "*.NCV");
    gtk_file_filter_add_pattern(filter, "*.curve");
    gtk_file_filter_add_pattern(filter, "*.CURVE");
    gtk_file_chooser_add_filter(fileChooser, filter);

    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("UFRaw curve format"));
    gtk_file_filter_add_pattern(filter, "*.curve");
    gtk_file_filter_add_pattern(filter, "*.CURVE");
    gtk_file_chooser_add_filter(fileChooser, filter);

    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("Nikon curve format"));
    gtk_file_filter_add_pattern(filter, "*.ntc");
    gtk_file_filter_add_pattern(filter, "*.NTC");
    gtk_file_filter_add_pattern(filter, "*.ncv");
    gtk_file_filter_add_pattern(filter, "*.NCV");
    gtk_file_chooser_add_filter(fileChooser, filter);

    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("All files"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(fileChooser, filter);

    if (strlen(CFG->curvePath)>0)
        gtk_file_chooser_set_current_folder(fileChooser, CFG->curvePath);
    if (gtk_dialog_run(GTK_DIALOG(fileChooser))==GTK_RESPONSE_ACCEPT) {
        for ( list=saveList=gtk_file_chooser_get_filenames(fileChooser);
              list!=NULL;
              list=g_slist_next(list) ) {
            c = conf_default.curve[0];
            if (curve_load(&c, list->data)!=UFRAW_SUCCESS) continue;
	    if (curveType==base_curve) {
		if ( CFG->BaseCurveCount>= max_curves ) break;
		gtk_combo_box_append_text(data->BaseCurveCombo, c.name);
		CFG->BaseCurve[CFG->BaseCurveCount] = c;
		CFG->BaseCurveIndex = CFG->BaseCurveCount;
		CFG->BaseCurveCount++;
		/* Add curve to .ufrawrc but don't make it default */
		RC.BaseCurve[RC.BaseCurveCount++] = c;
		RC.BaseCurveCount++;
		if (CFG_cameraCurve)
		    gtk_combo_box_set_active(data->BaseCurveCombo,
			    CFG->BaseCurveIndex);
		else
		    gtk_combo_box_set_active(data->BaseCurveCombo,
			    CFG->BaseCurveIndex-2);
	    } else { /* curveType==luminosity_curve */
		if ( CFG->curveCount >= max_curves ) break;
		gtk_combo_box_append_text(data->CurveCombo, c.name);
		CFG->curve[CFG->curveCount] = c;
		CFG->curveIndex = CFG->curveCount;
		CFG->curveCount++;
		/* Add curve to .ufrawrc but don't make it default */
		RC.curve[RC.curveCount++] = c;
		RC.curveCount++;
		gtk_combo_box_set_active(data->CurveCombo, CFG->curveIndex);
	    }
            cp = g_path_get_dirname(list->data);
            g_strlcpy(CFG->curvePath, cp, max_path);
            g_strlcpy(RC.curvePath, cp, max_path);
	    conf_save(&RC, NULL, NULL);
            g_free(cp);
            g_free(list->data);
        }
        if (list!=NULL)
        ufraw_message(UFRAW_ERROR, _("No more room for new curves."));
        g_slist_free(saveList);
    }
    ufraw_focus(fileChooser, FALSE);
    gtk_widget_destroy(GTK_WIDGET(fileChooser));
}

static void save_curve(GtkWidget *widget, long curveType)
{
    preview_data *data = get_preview_data(widget);
    GtkFileChooser *fileChooser;
    GtkFileFilter *filter;
    char defFilename[max_name];
    char *filename, *cp;

    if (data->FreezeDialog) return;

    fileChooser = GTK_FILE_CHOOSER(gtk_file_chooser_dialog_new(_("Save curve"),
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL));
    ufraw_focus(fileChooser, TRUE);
#if GTK_CHECK_VERSION(2,6,0)
    gtk_file_chooser_set_show_hidden(fileChooser, FALSE);
    GtkWidget *button = gtk_check_button_new_with_label(_("Show hidden files"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
    g_signal_connect(G_OBJECT(button), "toggled",
	    G_CALLBACK(ufraw_chooser_toggle), fileChooser);
    gtk_file_chooser_set_extra_widget(fileChooser, button);
#endif
    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("All curve formats"));
    gtk_file_filter_add_pattern(filter, "*.ntc");
    gtk_file_filter_add_pattern(filter, "*.NTC");
    gtk_file_filter_add_pattern(filter, "*.ncv");
    gtk_file_filter_add_pattern(filter, "*.NCV");
    gtk_file_filter_add_pattern(filter, "*.curve");
    gtk_file_filter_add_pattern(filter, "*.CURVE");
    gtk_file_chooser_add_filter(fileChooser, filter);

    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("UFRaw curve format"));
    gtk_file_filter_add_pattern(filter, "*.curve");
    gtk_file_filter_add_pattern(filter, "*.CURVE");
    gtk_file_chooser_add_filter(fileChooser, filter);

    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("Nikon curve format"));
    gtk_file_filter_add_pattern(filter, "*.ntc");
    gtk_file_filter_add_pattern(filter, "*.NTC");
    gtk_file_filter_add_pattern(filter, "*.ncv");
    gtk_file_filter_add_pattern(filter, "*.NCV");
    gtk_file_chooser_add_filter(fileChooser, filter);

    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("All files"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(fileChooser, filter);

    if (strlen(CFG->curvePath)>0)
        gtk_file_chooser_set_current_folder(fileChooser, CFG->curvePath);
    if (curveType==base_curve)
	snprintf(defFilename, max_name, "%s.curve",
		CFG->BaseCurve[CFG->BaseCurveIndex].name);
    else
	snprintf(defFilename, max_name, "%s.curve",
		CFG->curve[CFG->curveIndex].name);
    gtk_file_chooser_set_current_name(fileChooser, defFilename);
    if (gtk_dialog_run(GTK_DIALOG(fileChooser))==GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(fileChooser);
	if (curveType==base_curve)
	    curve_save(&CFG->BaseCurve[CFG->BaseCurveIndex], filename);
	else
	    curve_save(&CFG->curve[CFG->curveIndex], filename);
        cp = g_path_get_dirname(filename);
        g_strlcpy(CFG->curvePath, cp, max_path);
        g_free(cp);
        g_free(filename);
    }
    ufraw_focus(fileChooser, FALSE);
    gtk_widget_destroy(GTK_WIDGET(fileChooser));
}

static void load_profile(GtkWidget *widget, long type)
{
    preview_data *data = get_preview_data(widget);
    GtkFileChooser *fileChooser;
    GSList *list, *saveList;
    GtkFileFilter *filter;
    char *filename, *cp;
    profile_data p;

    if (data->FreezeDialog) return;
    if (CFG->profileCount[type]==max_profiles) {
        ufraw_message(UFRAW_ERROR, _("No more room for new profiles."));
        return;
    }
    fileChooser = GTK_FILE_CHOOSER(gtk_file_chooser_dialog_new(
            _("Load color profile"),
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL));
    ufraw_focus(fileChooser, TRUE);
    gtk_file_chooser_set_select_multiple(fileChooser, TRUE);
#if GTK_CHECK_VERSION(2,6,0)
    gtk_file_chooser_set_show_hidden(fileChooser, FALSE);
    GtkWidget *button = gtk_check_button_new_with_label(_("Show hidden files"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
    g_signal_connect(G_OBJECT(button), "toggled",
	    G_CALLBACK(ufraw_chooser_toggle), fileChooser);
    gtk_file_chooser_set_extra_widget(fileChooser, button);
#endif
    if (strlen(CFG->profilePath)>0)
        gtk_file_chooser_set_current_folder(fileChooser, CFG->profilePath);
    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("Color Profiles"));
    gtk_file_filter_add_pattern(filter, "*.icm");
    gtk_file_filter_add_pattern(filter, "*.ICM");
    gtk_file_filter_add_pattern(filter, "*.icc");
    gtk_file_filter_add_pattern(filter, "*.ICC");
    gtk_file_chooser_add_filter(fileChooser, filter);
    filter = GTK_FILE_FILTER(gtk_file_filter_new());
    gtk_file_filter_set_name(filter, _("All files"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(fileChooser, filter);
    if (gtk_dialog_run(GTK_DIALOG(fileChooser))==GTK_RESPONSE_ACCEPT) {
        for (list=saveList=gtk_file_chooser_get_filenames(fileChooser);
            list!=NULL && CFG->profileCount[type]<max_profiles;
            list=g_slist_next(list))
        {
            filename = list->data;
            p = conf_default.profile[type][1];
            g_strlcpy(p.file, filename, max_path);
	    /* Make sure we update product name */
	    Developer->updateTransform = TRUE;
            developer_profile(Developer, type, &p);
            if (Developer->profile[type]==NULL) {
                g_free(list->data);
                continue;
            }
	    char *base = g_path_get_basename(filename);
	    char *name = uf_file_set_type(base, "");
	    char *utf8 = g_filename_to_utf8(name, -1, NULL, NULL, NULL);
	    if (utf8==NULL) utf8 = g_strdup("Unknown file name");
	    g_strlcpy(p.name, utf8, max_name);
	    g_free(utf8);
	    g_free(name);
	    g_free(base);
            /* Set some defaults to the profile's curve */
	    p.gamma = profile_default_gamma(&p);

            CFG->profile[type][CFG->profileCount[type]++] = p;
            gtk_combo_box_append_text(data->ProfileCombo[type], p.name);
            CFG->profileIndex[type] = CFG->profileCount[type]-1;
            cp = g_path_get_dirname(list->data);
            g_strlcpy(CFG->profilePath, cp, max_path);
	    /* Add profile to .ufrawrc but don't make it default */
            RC.profile[type][RC.profileCount[type]++] = p;
            g_strlcpy(RC.profilePath, cp, max_path);
	    conf_save(&RC, NULL, NULL);
            g_free(cp);
            g_free(list->data);
        }
        gtk_combo_box_set_active(data->ProfileCombo[type],
		CFG->profileIndex[type]);
        if (list!=NULL)
            ufraw_message(UFRAW_ERROR, _("No more room for new profiles."));
        g_slist_free(saveList);
    }
    ufraw_focus(fileChooser, FALSE);
    gtk_widget_destroy(GTK_WIDGET(fileChooser));
}

static colorLabels *color_labels_new(GtkTable *table, int x, int y,
	char *label, int format, int zonep, preview_data *data)
{
    colorLabels *l;
    GtkWidget *lbl;
    int c, i = 0, numlabels;

    l = g_new(colorLabels, 1);
    l->format = format;
    l->zonep = zonep;

    numlabels = (zonep == with_zone ? 5 : 3);

    if (label!=NULL){
        lbl = gtk_label_new(label);
        gtk_misc_set_alignment(GTK_MISC(lbl), 1, 0.5);
        gtk_table_attach(table, lbl, x, x+1, y, y+1,
                GTK_SHRINK|GTK_FILL, GTK_FILL, 0, 0);
        i++;
    }
    /* 0-2 for RGB, 3 for value, 4 for zone */
    for (c=0; c<numlabels; c++, i++) {
	l->labels[c] = GTK_LABEL(gtk_label_new(NULL));
	GtkWidget *event_box = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(event_box), GTK_WIDGET(l->labels[c]));
	gtk_table_attach_defaults(table, event_box, x+i, x+i+1, y, y+1);
	if ( c==3 )
	    gtk_tooltips_set_tip(data->ToolTips, event_box,
                    _("Luminosity (Y value)"), NULL);
	if ( c==4 )
	    gtk_tooltips_set_tip(data->ToolTips, event_box,
                              _("Adams' zone"), NULL);
    }
    return l;
}

/* Return numeric zone representation from 0-1 luminance value.
 * Unlike Adams, we use arabic for now. */
static double value2zone(double v)
{
    const double zoneV = 0.18;
    double z_rel_V;
    double zone;

    /* Convert to value relative to zone V value. */
    z_rel_V = v / zoneV;

    /* Convert to log-based value, with zone V 0. */
    zone = log(z_rel_V)/log(2.0);

    /* Move zone 5 to the proper place. */
    zone += 5.0;

    return zone;
}

static void color_labels_set(colorLabels *l, double data[])
{
    int c;
    char buf1[max_name], buf2[max_name];
    /* Value black, zone purple, for no good reason. */
    const char *colorName[] = {"red", "green", "blue", "black", "purple"};

    for (c=0; c<3; c++) {
        switch (l->format) {
        case pixel_format: snprintf(buf1, max_name, "%3.f", data[c]); break;
        case percent_format:
                      if (data[c]<10.0)
                          snprintf(buf1, max_name, "%2.1f%%", data[c]);
                      else
                          snprintf(buf1, max_name, "%2.0f%%", data[c]);
                      break;
        default: snprintf(buf1, max_name, "ERR");
        }
        snprintf(buf2, max_name, "<span foreground='%s'>%s</span>",
                colorName[c], buf1);
        gtk_label_set_markup(l->labels[c], buf2);
    }
    /* If value/zone not requested, just return now. */
    if (l->zonep == without_zone)
	return;

    /* Value */
    c = 3;
    snprintf(buf2, max_name, "<span foreground='%s'>%0.3f</span>",
	    colorName[c], data[3]);
    gtk_label_set_markup(l->labels[c], buf2);

    /* Zone */
    c = 4;
    /* 2 decimal places may be excessive */
    snprintf(buf2, max_name, "<span foreground='%s'>%0.2f</span>",
	    colorName[c], data[4]);
    gtk_label_set_markup(l->labels[c], buf2);
}

static void redraw_navigation_image(preview_data *data)
{
#if defined(HAVE_GTKIMAGEVIEW) && GTK_IMAGE_VIEW_DAMAGE_PIXELS==0
    GtkWidget *scroll =
	gtk_widget_get_ancestor(data->PreviewWidget, GTK_TYPE_IMAGE_SCROLL_WIN);
    GtkImageNav *nav = GTK_IMAGE_NAV(GTK_IMAGE_SCROLL_WIN(scroll)->nav);
    if ( nav->pixbuf==NULL ) return;
    int navWidth = gdk_pixbuf_get_width(nav->pixbuf);
    int navHeight = gdk_pixbuf_get_height(nav->pixbuf);
    g_object_unref(nav->pixbuf);
    // GDK_INTERP_BILINEAR is too slow
    nav->pixbuf = gdk_pixbuf_scale_simple(data->PreviewPixbuf,
	    navWidth, navHeight, GDK_INTERP_NEAREST);
#else
    (void)data;
#endif
}

static void image_draw_area(preview_data *data, int x, int y,
			    int width, int height)
{
    GtkWidget *widget = data->PreviewWidget;
#ifdef HAVE_GTKIMAGEVIEW
#if GTK_IMAGE_VIEW_DAMAGE_PIXELS==1
    GtkImageView *view = GTK_IMAGE_VIEW(widget);
    GdkRectangle rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    gtk_image_view_damage_pixels(view, &rect);
#else
    GtkImageView *view = GTK_IMAGE_VIEW(widget);
    /* Find location of area in the view. */
    GdkRectangle viewRect;
    gtk_image_view_get_viewport(view, &viewRect);
    GdkPixbuf *pixbuf = gtk_image_view_get_pixbuf(view);
    int pixbufWidth = gdk_pixbuf_get_width(pixbuf);
    int pixbufHeight = gdk_pixbuf_get_height(pixbuf);
    int viewWidth = GTK_WIDGET(view)->allocation.width;
    int viewHeight = GTK_WIDGET(view)->allocation.height;
    if ( pixbufWidth>viewWidth ) {
	if (x<viewRect.x)
	    width += x - viewRect.x;
	x = MAX(x-viewRect.x, 0);
    } else {
	x += (viewWidth - pixbufWidth) / 2;
    }
    if ( pixbufHeight>viewHeight ) {
	if (y<viewRect.y)
	    height += y - viewRect.y;
	y = MAX(y-viewRect.y, 0);
    } else {
	y += (viewHeight - pixbufHeight) / 2;
    }
//preview_notify_dirty(preview_data *data)
    /* As a workaround we use the esoteric knowledge of GtkImageView's
     * internal logic. It will reuse pieces of the cached old pixmap
     * only if nothing changes in the view, otherwise it will discard
     * the cache. So we will change the transparency background for our
     * GtkImageView since anyway it is not seen through.
     */
    GTK_IMAGE_VIEW(data->PreviewWidget)->check_color1++;

    if ( height>0 && width>0 )
        gtk_widget_queue_draw_area(widget, x, y, width, height);
#endif
#else
    if ( height>0 && width>0 )
        gtk_widget_queue_draw_area(widget, x, y, width, height);
#endif
}

/* Modify the preview image to mark crop and spot areas.
 * Note that all coordinate intervals are semi-inclusive, e.g.
 * X1 <= pixels < X2 and Y1 <= pixels < Y2
 * This approach makes computing width/height just a matter of
 * substracting X1 from X2 or Y1 from Y2.
 */
static void preview_draw_area(preview_data *data, int x, int y,
			      int width, int height)
{
    int pixbufHeight = gdk_pixbuf_get_height(data->PreviewPixbuf);
    if ( y<0 || y>=pixbufHeight )
	g_error("preview_draw_area(): y:%d out of range 0 <= y < %d", y, pixbufHeight);
    if ( height==0 ) return; // Nothing to do

    int pixbufWidth = gdk_pixbuf_get_width(data->PreviewPixbuf);
    if ( x<0 || x>=pixbufWidth )
	g_error("preview_draw_area(): x:%d out of range 0 <= x < %d", x, pixbufWidth);
    if ( width==0 ) return; // Nothing to do

    int rowstride = gdk_pixbuf_get_rowstride(data->PreviewPixbuf);
    guint8 *pixies = gdk_pixbuf_get_pixels(data->PreviewPixbuf);
    guint8 *p8;
    ufraw_image_data img = data->UF->Images[ufraw_final_phase];
    /* Scale crop image coordinates to pixbuf coordinates */
    float scale_x = ((float)pixbufWidth) / data->UF->initialWidth;
    float scale_y = ((float)pixbufHeight) / data->UF->initialHeight;

    int CropY1 = floor (CFG->CropY1 * scale_y);
    int CropY2 =  ceil (CFG->CropY2 * scale_y);
    int CropX1 = floor (CFG->CropX1 * scale_x);
    int CropX2 =  ceil (CFG->CropX2 * scale_x);

    /* Scale spot image coordinates to pixbuf coordinates */
    int SpotY1 = floor (MIN(data->SpotY1, data->SpotY2) * scale_y);
    int SpotY2 =  ceil (MAX(data->SpotY1, data->SpotY2) * scale_y);
    int SpotX1 = floor (MIN(data->SpotX1, data->SpotX2) * scale_x);
    int SpotX2 =  ceil (MAX(data->SpotX1, data->SpotX2) * scale_x);
    int xx, yy, c;
    for (yy=y; yy<y+height; yy++) {
        p8 = pixies+yy*rowstride+x*3;
	memcpy(p8, img.buffer + yy*img.rowstride + x*img.depth, width*img.depth);
	for (xx=x; xx<x+width; xx++, p8+=3) {
	    if ( data->SpotDraw &&
		( ((yy==SpotY1-1 || yy==SpotY2) && xx>=SpotX1-1 && xx<=SpotX2) ||
		  ((xx==SpotX1-1 || xx==SpotX2) && yy>=SpotY1-1 && yy<=SpotY2) ) ) {
		if ( ((xx+yy) & 7) >= 4 )
		    p8[0] = p8[1] = p8[2] = 0;
		else
		    p8[0] = p8[1] = p8[2] = 255;
		continue;
	    }
	    /* Draw white frame around crop area */
	    if ( ((yy==CropY1-1 || yy==CropY2) && xx>=CropX1-1 && xx<=CropX2) ||
		 ((xx==CropX1-1 || xx==CropX2) && yy>=CropY1-1 && yy<=CropY2) )
	    {
		p8[0] = p8[1] = p8[2] = 255;
		continue;
	    }
	    /* Shade the cropped out area */
	    if ( yy<CropY1 || yy>=CropY2 || xx<CropX1 || xx>=CropX2 ) {
		for (c=0; c<3; c++) p8[c] = p8[c]/4;
		continue;
	    }
	    if (data->RenderMode==render_default) {
		if ( CFG->overExp && (data->OverUnderTicker & 3) == 1 &&
		     (p8[0]==255 || p8[1]==255 || p8[2]==255) )
		    p8[0] = p8[1] = p8[2] = 0;
		else if ( CFG->underExp && (data->OverUnderTicker & 3) == 3 &&
			  (p8[0]==0 || p8[1]==0 || p8[2]==0) )
		    p8[0] = p8[1] = p8[2] = 255;
		continue;
	    } else if (data->RenderMode==render_overexposed) {
		for (c=0; c<3; c++) if (p8[c]!=255) p8[c] = 0;
	    } else if (data->RenderMode==render_underexposed) {
		for (c=0; c<3; c++) if (p8[c]!=0) p8[c] = 255;
	    }
	}
    }
    /* Redraw the changed areas */
    image_draw_area(data, x, y, width, height);
}

static gboolean switch_highlights(gpointer ptr)
{
    preview_data *data = ptr;
    /* Only redraw the highlights in the default rendering mode. */
    if (data->RenderMode!=render_default || data->FreezeDialog)
	return TRUE;
    int pixbufWidth = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int pixbufHeight = gdk_pixbuf_get_height(data->PreviewPixbuf);
    if (data->RenderLine==MAX(pixbufHeight, pixbufWidth)) {
	float scale_x = ((float)pixbufWidth) / data->UF->initialWidth;
	float scale_y = ((float)pixbufHeight) / data->UF->initialHeight;
	/* Set the area to redraw based on the crop rectangle and view port. */
#ifdef HAVE_GTKIMAGEVIEW
	GdkRectangle viewRect;
	gtk_image_view_get_viewport(GTK_IMAGE_VIEW(data->PreviewWidget),
		&viewRect);
	
	int x = MAX(viewRect.x, floor(CFG->CropX1 * scale_x));
	int x2 = MIN(viewRect.x+viewRect.width, ceil(CFG->CropX2 * scale_x));
	int width =  MAX(x2 - x, 0);
	int y = MAX(viewRect.y, floor (CFG->CropY1 * scale_y));
	int y2 = MIN(viewRect.y+viewRect.height, ceil(CFG->CropY2 * scale_y));
	int height =  MAX(y2 - y, 0);
#else
	int x = floor(CFG->CropX1 * scale_x);
	int width =  ceil(CFG->CropX2 * scale_x) - x;
	int y = floor (CFG->CropY1 * scale_y);
	int height =  ceil(CFG->CropY2 * scale_x) - y;
#endif
	data->OverUnderTicker++;
	preview_draw_area(data, x, y, width, height);
    }
    /* If no highlights are needed, disable this timeout function. */
    if (!CFG->overExp && !CFG->underExp) {
        data->BlinkTimer = 0;
	return FALSE;
    }
    return TRUE;
}

static void start_blink(preview_data *data)
{
    if (!data->BlinkTimer) {
	data->BlinkTimer = g_timeout_add(500, switch_highlights, data);
    }
}

static void stop_blink(preview_data *data)
{
    if (data->BlinkTimer) {
	data->OverUnderTicker = 0;
	switch_highlights(data);
	g_source_remove(data->BlinkTimer);
	data->BlinkTimer = 0;
    }
}

static void window_map_event(GtkWidget *widget, GdkEvent *event,
			     gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    if (CFG->overExp || CFG->underExp)
	start_blink(data);
    else
	stop_blink(data);
    (void)event;
    (void)user_data;
}

static void window_unmap_event(GtkWidget *widget, GdkEvent *event,
			       gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    stop_blink(data);
    (void)event;
    (void)user_data;
}

static void render_preview(preview_data *data);
static gboolean render_raw_histogram(preview_data *data);
static gboolean render_preview_image(preview_data *data);
static gboolean render_live_histogram(preview_data *data);
static gboolean render_spot(preview_data *data);
static void draw_spot(preview_data *data, gboolean draw);

static void render_special_mode(GtkWidget *widget, long mode)
{
    preview_data *data = get_preview_data(widget);
    data->RenderMode = mode;
    int width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int height = gdk_pixbuf_get_height(data->PreviewPixbuf);
    preview_draw_area(data, 0, 0, width, height);
}

static void render_init(preview_data *data)
{
    /* Check if we need a new pixbuf */
    int width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int height = gdk_pixbuf_get_height(data->PreviewPixbuf);
    ufraw_image_data *image = &data->UF->Images[ufraw_first_phase];
    if (width!=image->width || height!=image->height) {
        data->PreviewPixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                                             image->width, image->height);
        /* Clear the pixbuffer to avoid displaying garbage */
        gdk_pixbuf_fill (data->PreviewPixbuf, 0);
#ifdef HAVE_GTKIMAGEVIEW
	gtk_image_view_set_pixbuf(GTK_IMAGE_VIEW(data->PreviewWidget),
		data->PreviewPixbuf, FALSE);
	gtk_image_view_set_zoom(GTK_IMAGE_VIEW(data->PreviewWidget), 1.0);
#else
	gtk_image_set_from_pixbuf(GTK_IMAGE(data->PreviewWidget),
		data->PreviewPixbuf);
#endif
	g_object_unref(data->PreviewPixbuf);
    }
    if (data->ProgressBar!=NULL) {
	char progressText[max_name];
	if (CFG->Scale==0)
	    snprintf(progressText, max_name, _("size %dx%d, zoom %2.f%%"),
		    data->UF->initialWidth, data->UF->initialHeight,
		    CFG->Zoom);
	else
	    snprintf(progressText, max_name, _("size %dx%d, scale 1/%d"),
		    data->UF->initialWidth, data->UF->initialHeight,
		    CFG->Scale);
	gtk_progress_bar_set_text(data->ProgressBar, progressText);
	gtk_progress_bar_set_fraction(data->ProgressBar, 0);
    }
}

static void render_preview(preview_data *data)
{
    if (data->FreezeDialog) return;

    render_init(data);
    ufraw_convert_image_init_phase(data->UF);
    data->RenderLine = 0;
    g_idle_remove_by_data(data);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
	    (GSourceFunc)(render_raw_histogram), data, NULL);
}

static gboolean render_raw_histogram(preview_data *data)
{
    if (data->FreezeDialog) return FALSE;
    GdkPixbuf *pixbuf;
    guint8 *pixies;
    int rowstride;
    guint8 pix[99], *p8, pen[4][3];
    guint16 pixtmp[9999];
    image_type p16;
    int x, c, cl, colors, y, y0, y1;
    int raw_his[raw_his_size][4], raw_his_max;

    if (CFG->autoExposure == apply_state) {
	ufraw_auto_expose(data->UF);
	gdouble lower, upper;
	g_object_get(data->ExposureAdjustment, "lower", &lower,
		"upper", &upper, NULL);
	/* Clip the exposure to prevent a "changed" signal */
	if (CFG->exposure > upper) CFG->exposure = upper;
	if (CFG->exposure < lower) CFG->exposure = lower;
	gtk_adjustment_set_value(data->ExposureAdjustment, CFG->exposure);
	gtk_widget_set_sensitive(data->ResetExposureButton,
		fabs( conf_default.exposure - CFG->exposure) > 0.001);
    }
    if (CFG->autoBlack == apply_state) {
	ufraw_auto_black(data->UF);
	curveeditor_widget_set_curve(data->CurveWidget,
		&CFG->curve[CFG->curveIndex]);
	gtk_widget_set_sensitive(data->ResetBlackButton,
		CFG->curve[CFG->curveIndex].m_anchors[0].x!=0.0 ||
		CFG->curve[CFG->curveIndex].m_anchors[0].y!=0.0 );
    }
    char text[max_name];
    g_snprintf(text, max_name, _("Black point: %0.3lf"),
	    CFG->curve[CFG->curveIndex].m_anchors[0].x);
    gtk_label_set_text(GTK_LABEL(data->BlackLabel), text);
    developer_prepare(Developer, CFG,
	    data->UF->rgbMax, data->UF->rgb_cam,
	    data->UF->colors, data->UF->useMatrix, display_developer);

    pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(data->RawHisto));
    if (gdk_pixbuf_get_height(pixbuf)!=CFG->rawHistogramHeight+2) {
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
	        raw_his_size+2, CFG->rawHistogramHeight+2);
	gtk_image_set_from_pixbuf(GTK_IMAGE(data->RawHisto), pixbuf);
	g_object_unref(pixbuf);
    }
    colors = data->UF->colors;
    pixies = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    memset(pixies, 0, (gdk_pixbuf_get_height(pixbuf)-1)*rowstride +
            gdk_pixbuf_get_width(pixbuf)*gdk_pixbuf_get_n_channels(pixbuf));
    /* Normalize raw histogram data */
    for (x=0, raw_his_max=1; x<raw_his_size; x++) {
	for (c=0, y=0; c<colors; c++) {
	    if (CFG->rawHistogramScale==log_histogram)
		raw_his[x][c] = log(1+data->raw_his[x][c])*1000;
	    else
		raw_his[x][c] = data->raw_his[x][c];
	    y += raw_his[x][c];
	}
        raw_his_max = MAX(raw_his_max, y);
    }
    /* Prepare pen color, which is not effected by exposure.
     * Use a small value to avoid highlights and then normalize. */
    for (c=0; c<colors; c++) {
	for (cl=0; cl<colors; cl++) p16[cl] = 0;
	p16[c] = Developer->max * 0x08000 / Developer->rgbWB[c] *
		0x10000 / Developer->exposure;
        develope(pen[c], p16, Developer, 8, pixtmp, 1);
	guint8 max=1;
	for (cl=0; cl<3; cl++) max = MAX(pen[c][cl], max);
	for (cl=0; cl<3; cl++) pen[c][cl] = pen[c][cl] * 0xff / max;
    }
    /* Calculate the curves */
    p8 = pix;
    guint8 grayCurve[raw_his_size+1][4];
    guint8 pureCurve[raw_his_size+1][4];
    for (x=0; x<raw_his_size+1; x++) {
	for (c=0; c<colors; c++) {
            /* Value for pixel x of color c in a gray pixel */
            for (cl=0; cl<colors; cl++)
                p16[cl] = MIN((guint64)x*Developer->rgbMax *
                          Developer->rgbWB[c] /
                          Developer->rgbWB[cl] / raw_his_size, 0xFFFF);
            develope(p8, p16, Developer, 8, pixtmp, 1);
            grayCurve[x][c] = MAX(MAX(p8[0],p8[1]),p8[2]) *
		    (CFG->rawHistogramHeight-1) / MAXOUT;
            /* Value for pixel x of pure color c */
            p16[0] = p16[1] = p16[2] = p16[3] = 0;
            p16[c] = MIN((guint64)x*Developer->rgbMax/raw_his_size, 0xFFFF);
            develope(p8, p16, Developer, 8, pixtmp, 1);
            pureCurve[x][c] = MAX(MAX(p8[0],p8[1]),p8[2]) *
		    (CFG->rawHistogramHeight-1) / MAXOUT;
	}
    }
    for (x=0; x<raw_his_size; x++) {
	/* draw the raw histogram */
        for (c=0, y0=0; c<colors; c++) {
	    for (y=0; y<raw_his[x][c]*CFG->rawHistogramHeight/raw_his_max; y++)
		for (cl=0; cl<3; cl++)
		    pixies[(CFG->rawHistogramHeight-y-y0)*rowstride
			    + 3*(x+1)+cl] = pen[c][cl];
	    y0 += y;
	}
	/* draw curves on the raw histogram */
        for (c=0; c<colors; c++) {
            y = grayCurve[x][c];
            y1 = grayCurve[x+1][c];
            for (; y<=y1; y++)
		for (cl=0; cl<3; cl++)
		    pixies[(CFG->rawHistogramHeight-y)*rowstride
			    + 3*(x+1)+cl] = pen[c][cl];
            y1 = pureCurve[x][c];
            for (; y<y1; y++)
		for (cl=0; cl<3; cl++)
		    pixies[(CFG->rawHistogramHeight-y)*rowstride
			    + 3*(x+1)+cl] = pen[c][cl]/2;
            y1 = pureCurve[x+1][c];
            for (; y<=y1; y++)
		for (cl=0; cl<3; cl++)
		    pixies[(CFG->rawHistogramHeight-y)*rowstride
			    + 3*(x+1)+cl] = pen[c][cl];
        }
    }
    gtk_widget_queue_draw(data->RawHisto);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
            (GSourceFunc)(render_preview_image), data, NULL);
    return FALSE;
}

static gboolean render_preview_image(preview_data *data)
{
    if (data->FreezeDialog) return FALSE;

    int width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int height = gdk_pixbuf_get_height(data->PreviewPixbuf);
    int tileHeight = MIN(height - data->RenderLine, 32);
    ufraw_convert_image_area(data->UF, 0, data->RenderLine, width, tileHeight,
	    data->fromPhase);
    preview_draw_area(data, 0, data->RenderLine, width, tileHeight);
    data->RenderLine += tileHeight;
    if ( data->RenderLine<height )
        return TRUE;

    data->RenderLine = MAX(height, width);
    data->fromPhase = ufraw_final_phase;
    redraw_navigation_image(data);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
            (GSourceFunc)(render_live_histogram), data, NULL);
    return FALSE;
}

static gboolean render_live_histogram(preview_data *data)
{
    if (data->FreezeDialog) return FALSE;

    int width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int height = gdk_pixbuf_get_height(data->PreviewPixbuf);
    int i, x, y, c, min, max;
    guint8 *p8;
    ufraw_image_data img = data->UF->Images[ufraw_final_phase];
    double rgb[3];
    guint64 sum[3], sqr[3];
    int live_his[live_his_size][4];
    memset(live_his, 0, sizeof(live_his));
    for (i=0; i < img.height*img.width; i++) {
	p8 = img.buffer + i*img.depth;
        for (c=0, max=0, min=0x100; c<3; c++) {
            max = MAX(max, p8[c]);
            min = MIN(min, p8[c]);
            live_his[p8[c]][c]++;
	}
        if (CFG->histogram==luminosity_histogram)
            live_his[(int)(0.3*p8[0]+0.59*p8[1]+0.11*p8[2])][3]++;
        if (CFG->histogram==value_histogram)
            live_his[max][3]++;
        if (CFG->histogram==saturation_histogram) {
            if (max==0) live_his[0][3]++;
            else live_his[255*(max-min)/max][3]++;
        }
    }
    GdkPixbuf *pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(data->LiveHisto));
    if (gdk_pixbuf_get_height(pixbuf)!=CFG->liveHistogramHeight+2) {
        pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
	        live_his_size+2, CFG->liveHistogramHeight+2);
	gtk_image_set_from_pixbuf(GTK_IMAGE(data->LiveHisto), pixbuf);
	g_object_unref(pixbuf);
    }
    guint8 *pixies = gdk_pixbuf_get_pixels(pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    memset(pixies, 0, (gdk_pixbuf_get_height(pixbuf)-1)*rowstride +
            gdk_pixbuf_get_width(pixbuf)*gdk_pixbuf_get_n_channels(pixbuf));
    for (c=0; c<3; c++) {
        sum[c] = 0;
        sqr[c] = 0;
        for (x=1; x<live_his_size; x++) {
            sum[c] += x*live_his[x][c];
            sqr[c] += (guint64)x*x*live_his[x][c];
        }
    }
    int live_his_max;
    for (x=1, live_his_max=1; x<live_his_size-1; x++) {
	if (CFG->liveHistogramScale==log_histogram)
	    for (c=0; c<4; c++) live_his[x][c] = log(1+live_his[x][c])*1000;
        if (CFG->histogram==rgb_histogram)
            for (c=0; c<3; c++)
                live_his_max = MAX(live_his_max, live_his[x][c]);
        else if (CFG->histogram==r_g_b_histogram)
            live_his_max = MAX(live_his_max,
		    live_his[x][0]+live_his[x][1]+live_his[x][2]);
        else live_his_max = MAX(live_his_max, live_his[x][3]);
    }
    for (x=0; x<live_his_size; x++) for (y=0; y<CFG->liveHistogramHeight; y++)
        if (CFG->histogram==r_g_b_histogram) {
            if (y*live_his_max < live_his[x][0]*CFG->liveHistogramHeight)
                pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+0] = 255;
            else if (y*live_his_max <
                    (live_his[x][0]+live_his[x][1])*CFG->liveHistogramHeight)
                pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+1] = 255;
            else if (y*live_his_max <
                    (live_his[x][0]+live_his[x][1]+live_his[x][2])
                    *CFG->liveHistogramHeight)
                pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+2] = 255;
        } else {
            for (c=0; c<3; c++)
                if (CFG->histogram==rgb_histogram) {
                    if (y*live_his_max<live_his[x][c]*CFG->liveHistogramHeight)
                        pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+c]
			    = 255;
                } else {
                    if (y*live_his_max<live_his[x][3]*CFG->liveHistogramHeight)
                        pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+c]
			    = 255;
                }
       }

    /* draw vertical line at quarters "behind" the live histogram */
    for (y=-1; y<CFG->liveHistogramHeight+1; y++) for (x=64; x<255; x+=64)
        if (pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+0]==0 &&
            pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+1]==0 &&
            pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+2]==0 )
            for (c=0; c<3; c++)
                pixies[(CFG->liveHistogramHeight-y)*rowstride+3*(x+1)+c]=64;
    gtk_widget_queue_draw(data->LiveHisto);
    for (c=0;c<3;c++) rgb[c] = sum[c]/height/width;
    color_labels_set(data->AvrLabels, rgb);
    for (c=0;c<3;c++) rgb[c] = sqrt(sqr[c]/height/width-rgb[c]*rgb[c]);
    color_labels_set(data->DevLabels, rgb);
    for (c=0;c<3;c++) rgb[c] = 100.0*live_his[live_his_size-1][c]/height/width;
    color_labels_set(data->OverLabels, rgb);
    for (c=0;c<3;c++) rgb[c] = 100.0*live_his[0][c]/height/width;
    color_labels_set(data->UnderLabels, rgb);

    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
            (GSourceFunc)(render_spot), data, NULL);
    return FALSE;
}

static gboolean render_spot(preview_data *data)
{
    if (data->FreezeDialog) return FALSE;

    if (data->SpotX1<0) return FALSE;
    if ( data->SpotX1>=data->UF->initialWidth ||
	 data->SpotY1>=data->UF->initialHeight ) return FALSE;
    int width = data->UF->Images[ufraw_final_phase].width;
    int height = data->UF->Images[ufraw_final_phase].height;
    int outDepth = data->UF->Images[ufraw_final_phase].depth;
    void *outBuffer = data->UF->Images[ufraw_final_phase].buffer;
    int rawDepth = data->UF->Images[ufraw_first_phase].depth;
    void *rawBuffer = data->UF->Images[ufraw_first_phase].buffer;
    /* We assume that first_phase and final_phase buffer sizes are the same. */
    /* Scale image coordinates to Images[ufraw_final_phase] coordinates */
    int spotHeight = abs(data->SpotY1 - data->SpotY2)
	    * height / data->UF->initialHeight + 1;
    int spotStartY = MIN(data->SpotY1, data->SpotY2)
	    * height / data->UF->initialHeight;
    if (spotHeight+spotStartY>height) spotStartY = height - spotHeight;
    int spotWidth = abs(data->SpotX1 - data->SpotX2)
	    * width / data->UF->initialWidth + 1;
    int spotStartX = MIN(data->SpotX1, data->SpotX2)
	    * width / data->UF->initialWidth;
    if (spotWidth+spotStartX>width) spotStartX = width - spotWidth;
    guint64 rawSum[4], outSum[3];
    int c, y, x;
    for (c=0; c<3; c++) rawSum[c] = outSum[c] = 0;
    for (y=spotStartY; y<spotStartY+spotHeight; y++) {
        for (x=spotStartX; x<spotStartX+spotWidth; x++) {
	    guint16 *rawPixie = rawBuffer + (y*width + x)*rawDepth;
            for (c=0; c<data->UF->colors; c++)
		rawSum[c] += rawPixie[c];
	    guint8 *outPixie = outBuffer + (y*width + x)*outDepth;
            for (c=0; c<3; c++)
		outSum[c] += outPixie[c];
	}
    }
    double rgb[5];
    for (c=0; c<3; c++) rgb[c] = outSum[c] / (spotWidth * spotHeight);
    /*
     * Convert RGB pixel value to 0-1 space, intending to reprsent,
     * absent contrast manipulation and color issues, luminance relative
     * to an 18% grey card at 0.18.
     * The RGB color space is approximately linearized sRGB as it is not
     * affected from the ICC profile.
     */
    guint16 rawChannels[4], linearChannels[3];
    for (c=0; c<data->UF->colors; c++)
	rawChannels[c] = rawSum[c] / (spotWidth * spotHeight);
    develop_linear(rawChannels, linearChannels, Developer);
    double yValue = 0.5;
    extern const double xyz_rgb[3][3];
    for (c=0; c<3; c++)
        yValue += xyz_rgb[1][c] * linearChannels[c];
    yValue /= 0xFFFF;
    if ( Developer->clipHighlights==film_highlights )
	yValue *= (double)Developer->exposure/0x10000;
    rgb[3] = yValue;
    rgb[4] = value2zone(yValue);
    color_labels_set(data->SpotLabels, rgb);
    char tmp[max_name];
    snprintf(tmp, max_name, "<span background='#%02X%02X%02X'>"
	    "                    </span>",
	    (int)rgb[0], (int)rgb[1], (int)rgb[2]);
    gtk_label_set_markup(data->SpotPatch, tmp);
    gtk_widget_show(GTK_WIDGET(data->SpotTable));
    if ( data->PageNum!=4 )
	draw_spot(data, TRUE);

    return FALSE;
}

static void close_spot(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    preview_data *data = get_preview_data(widget);
    draw_spot(data, FALSE);
    data->SpotX1 = -1;
    gtk_widget_hide(GTK_WIDGET(data->SpotTable));
}

static void draw_spot(preview_data *data, gboolean draw)
{
    if (data->SpotX1<0) return;
    int width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int height = gdk_pixbuf_get_height(data->PreviewPixbuf);
    data->SpotDraw = draw;
    /* Scale spot image coordinates to pixbuf coordinates */
    int SpotY1 = MAX(MIN(data->SpotY1, data->SpotY2)
	    * height / data->UF->initialHeight - 1, 0);
    int SpotY2 = MIN(MAX(data->SpotY1, data->SpotY2)
	    * height / data->UF->initialHeight + 1, height-1);
    int SpotX1 = MAX(MIN(data->SpotX1, data->SpotX2)
	    * width / data->UF->initialWidth - 1, 0);
    int SpotX2 = MIN(MAX(data->SpotX1, data->SpotX2)
	    * width / data->UF->initialWidth + 1, width-1);
    preview_draw_area(data, SpotX1, SpotY1, SpotX2-SpotX1+1, 1);
    preview_draw_area(data, SpotX1, SpotY2, SpotX2-SpotX1+1, 1);
    preview_draw_area(data, SpotX1, SpotY1, 1, SpotY2-SpotY1+1);
    preview_draw_area(data, SpotX2, SpotY1, 1, SpotY2-SpotY1+1);
}

static void update_shrink_ranges(preview_data *data);

/* update the UI entries that could have changed automatically */
static void update_scales(preview_data *data)
{
    if (data->FreezeDialog) return;
    data->FreezeDialog = TRUE;

    if (CFG->BaseCurveIndex>camera_curve && !CFG_cameraCurve)
        gtk_combo_box_set_active(data->BaseCurveCombo, CFG->BaseCurveIndex-2);
    else
        gtk_combo_box_set_active(data->BaseCurveCombo, CFG->BaseCurveIndex);
    gtk_combo_box_set_active(data->CurveCombo, CFG->curveIndex);

    int i;
    GList *l;
    gtk_combo_box_set_active(data->WBCombo, 0);
    for (i=0, l=data->WBPresets; l!=NULL; i++, l=g_list_next(l))
	if (!strcmp(CFG->wb, l->data))
	    gtk_combo_box_set_active(data->WBCombo, i);

    if (data->WBTuningAdjustment!=NULL)
	gtk_adjustment_set_value(data->WBTuningAdjustment, CFG->WBTuning);
    gtk_adjustment_set_value(data->TemperatureAdjustment, CFG->temperature);
    gtk_adjustment_set_value(data->GreenAdjustment, CFG->green);
    gtk_adjustment_set_value(data->ExposureAdjustment, CFG->exposure);
    gtk_adjustment_set_value(data->ThresholdAdjustment, CFG->threshold);
    gtk_adjustment_set_value(data->SaturationAdjustment, CFG->saturation);
    gtk_adjustment_set_value(data->GammaAdjustment,
            CFG->profile[0][CFG->profileIndex[0]].gamma);
    gtk_adjustment_set_value(data->LinearAdjustment,
            CFG->profile[0][CFG->profileIndex[0]].linear);
    for (i=0; i<data->UF->colors; i++)
	gtk_adjustment_set_value(data->ChannelAdjustment[i],
		CFG->chanMul[i]);
    gtk_adjustment_set_value(data->ZoomAdjustment, CFG->Zoom);

    if (GTK_WIDGET_SENSITIVE(data->UseMatrixButton)) {
	data->UF->useMatrix = CFG->profile[0][CFG->profileIndex[0]].useMatrix;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->UseMatrixButton),
		data->UF->useMatrix);
    }
    gtk_toggle_button_set_active(data->AutoExposureButton, CFG->autoExposure);
    gtk_toggle_button_set_active(data->AutoBlackButton, CFG->autoBlack);
    curveeditor_widget_set_curve(data->CurveWidget,
	    &CFG->curve[CFG->curveIndex]);

    gtk_widget_set_sensitive(data->ResetWBButton,
	    ( strcmp(CFG->wb, data->SaveConfig.wb)
	    ||fabs(CFG->temperature-data->initialTemperature)>1
	    ||fabs(CFG->green-data->initialGreen)>0.001
	    ||CFG->chanMul[0]!=data->initialChanMul[0]
	    ||CFG->chanMul[1]!=data->initialChanMul[1]
	    ||CFG->chanMul[2]!=data->initialChanMul[2]
	    ||CFG->chanMul[3]!=data->initialChanMul[3] ) );
    gtk_widget_set_sensitive(data->ResetGammaButton,
	    fabs( profile_default_gamma(&CFG->profile[0][CFG->profileIndex[0]])
		- CFG->profile[0][CFG->profileIndex[0]].gamma) > 0.001);
    gtk_widget_set_sensitive(data->ResetLinearButton,
	    fabs( profile_default_linear(&CFG->profile[0][CFG->profileIndex[0]])
		- CFG->profile[0][CFG->profileIndex[0]].linear) > 0.001);
    gtk_widget_set_sensitive(data->ResetExposureButton,
	    fabs( conf_default.exposure - CFG->exposure) > 0.001);
    gtk_widget_set_sensitive(data->ResetThresholdButton,
	    fabs( conf_default.threshold - CFG->threshold) > 1);
    gtk_widget_set_sensitive(data->ResetSaturationButton,
	    fabs( conf_default.saturation - CFG->saturation) > 0.001);
    gtk_widget_set_sensitive(data->ResetBaseCurveButton,
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_numAnchors>2 ||
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[0].x!=0.0 ||
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[0].y!=0.0 ||
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[1].x!=1.0 ||
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[1].y!=1.0 );
    gtk_widget_set_sensitive(data->ResetBlackButton,
	    CFG->curve[CFG->curveIndex].m_anchors[0].x!=0.0 ||
	    CFG->curve[CFG->curveIndex].m_anchors[0].y!=0.0 );
    gtk_widget_set_sensitive(data->ResetCurveButton,
	    CFG->curve[CFG->curveIndex].m_numAnchors>2 ||
	    CFG->curve[CFG->curveIndex].m_anchors[1].x!=1.0 ||
	    CFG->curve[CFG->curveIndex].m_anchors[1].y!=1.0 );

    if ( fabs(data->shrink-floor(data->shrink+0.0005))<0.0005 ) {
        CFG->shrink = floor(data->shrink+0.0005);
        CFG->size = 0;
    } else {
        CFG->shrink = 1;
        CFG->size = floor(MAX(data->height, data->width)+0.5);
    }
    data->FreezeDialog = FALSE;
    update_shrink_ranges(data);
    render_preview(data);
}

static void curve_update(GtkWidget *widget, long curveType)
{
    preview_data *data = get_preview_data(widget);
    if (curveType==base_curve) {
	CFG->BaseCurveIndex = manual_curve;
	CFG->BaseCurve[CFG->BaseCurveIndex] =
	    *curveeditor_widget_get_curve(data->BaseCurveWidget);
	if (CFG->autoExposure==enabled_state) CFG->autoExposure = apply_state;
	if (CFG->autoBlack==enabled_state) CFG->autoBlack = apply_state;
    } else {
	CFG->curveIndex = manual_curve;
	CFG->curve[CFG->curveIndex] =
	    *curveeditor_widget_get_curve(data->CurveWidget);
	CFG->autoBlack = FALSE;
    }
    update_scales(data);
}

static void spot_wb_event(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    int spotStartX, spotStartY, spotWidth, spotHeight;
    int width, height, x, y, c;
    guint64 rgb[4];

    user_data = user_data;

    if (data->FreezeDialog) return;
    if (data->SpotX1<=0) return;
    width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    height = gdk_pixbuf_get_height(data->PreviewPixbuf);
    /* Scale image coordinates to pixbuf coordinates */
    spotHeight = abs(data->SpotY1 - data->SpotY2)
	    * height / data->UF->initialHeight + 1;
    spotStartY = MIN(data->SpotY1, data->SpotY2)
	    *height / data->UF->initialHeight;
    spotWidth = abs(data->SpotX1 - data->SpotX2)
	    * width / data->UF->initialWidth + 1;
    spotStartX = MIN(data->SpotX1, data->SpotX2)
	    * width / data->UF->initialWidth;
    ufraw_image_data *image = &data->UF->Images[ufraw_first_phase];

    for (c=0; c<4; c++) rgb[c] = 0;
    for (y=spotStartY; y<spotStartY+spotHeight; y++)
        for (x=spotStartX; x<spotStartX+spotWidth; x++) {
	    guint16 *pixie = (guint16*)(image->buffer +
		    (y*image->width + x)*image->depth);
            for (c=0; c<data->UF->colors; c++)
                rgb[c] += pixie[c];
	}
    for (c=0; c<4; c++) rgb[c] = MAX(rgb[c], 1);
    for (c=0; c<data->UF->colors; c++)
        CFG->chanMul[c] = (double)spotWidth * spotHeight * data->UF->rgbMax
		/ rgb[c];
    if (data->UF->colors<4) CFG->chanMul[3] = 0.0;
    ufraw_message(UFRAW_SET_LOG,
	    "spot_wb: channel multipliers = { %.0f, %.0f, %.0f, %.0f }\n",
            CFG->chanMul[0], CFG->chanMul[1], CFG->chanMul[2], CFG->chanMul[3]);
    g_strlcpy(CFG->wb, spot_wb, max_name);
    ufraw_set_wb(data->UF);
    if (CFG->autoExposure==enabled_state) CFG->autoExposure = apply_state;
    if (CFG->autoBlack==enabled_state) CFG->autoBlack = apply_state;
    update_scales(data);
}

static void event_coordinate_rescale(gdouble *x, gdouble *y, preview_data *data)
{
    int width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int height = gdk_pixbuf_get_height(data->PreviewPixbuf);
#ifdef HAVE_GTKIMAGEVIEW
    /* Find location of event in the view. */
    GdkRectangle viewRect;
    gtk_image_view_get_viewport(GTK_IMAGE_VIEW(data->PreviewWidget),
	    &viewRect);
    int viewWidth = data->PreviewWidget->allocation.width;
    int viewHeight = data->PreviewWidget->allocation.height;
    if ( viewWidth<width ) {
	*x += viewRect.x;
    } else {
	*x -= (viewWidth - width) / 2;
    }
    if ( viewHeight<height ) {
	*y += viewRect.y;
    } else {
	*y -= (viewHeight - height) / 2;
    }
#endif
    if ( *x<0) *x = 0;
    if ( *x>width ) *x = width;
    if ( *y<0 ) *y = 0;
    if ( *y>height ) *y = height;
    /* Scale pixbuf coordinates to image coordinates */
    *x = *x * data->UF->initialWidth / width;
    *y = *y * data->UF->initialHeight / height;
}

static gboolean preview_button_press(GtkWidget *event_box, GdkEventButton *event,
	gpointer user_data)
{
    preview_data *data = get_preview_data(event_box);
    (void)user_data;
    if (data->FreezeDialog) return FALSE;
    if (event->button!=1) return FALSE;
    event_coordinate_rescale(&event->x, &event->y, data);
    if ( data->PageNum==0 ) {
	data->PreviewButtonPressed = TRUE;
	draw_spot(data, FALSE);
	data->SpotX1 = data->SpotX2 = event->x;
	data->SpotY1 = data->SpotY2 = event->y;
	render_spot(data);
	return TRUE;
    }
    if ( data->PageNum==4 ) {
	data->PreviewButtonPressed = TRUE;
	return TRUE;
    }
    return FALSE;
}

static gboolean preview_button_release(GtkWidget *event_box, GdkEventButton *event,
	gpointer user_data)
{
    preview_data *data = get_preview_data(event_box);
    (void)user_data;
    if (event->button!=1) return FALSE;
    data->PreviewButtonPressed = FALSE;
    return TRUE;
}

static void update_crop_ranges(preview_data *data);
static void fix_crop_aspect(preview_data *data, CursorType cursor);
static void set_new_aspect(preview_data *data);
static void refresh_aspect(preview_data *data);

static gboolean crop_motion_notify(preview_data *data, GdkEventMotion *event)
{
    event_coordinate_rescale(&event->x, &event->y, data);
    int x = event->x;
    int y = event->y;
    int pixbufHeight = gdk_pixbuf_get_height(data->PreviewPixbuf);
    int pixbufWidth = gdk_pixbuf_get_width(data->PreviewPixbuf);
    int sideSizeX = MIN(16 * data->UF->initialWidth / pixbufWidth,
	    (CFG->CropX2-CFG->CropX1)/3);
    int sideSizeY = MIN(16 * data->UF->initialHeight / pixbufHeight,
	    (CFG->CropY2-CFG->CropY1)/3);

    if ( (event->state&GDK_BUTTON1_MASK)==0 ) {
        const CursorType tr_cursor [16] =
        {
            crop_cursor, crop_cursor, crop_cursor, crop_cursor,
            crop_cursor, top_left_cursor, left_cursor, bottom_left_cursor,
            crop_cursor, top_cursor, move_cursor, bottom_cursor,
            crop_cursor, top_right_cursor, right_cursor, bottom_right_cursor
        };

        int sel_cursor = 0;
        if (y >= CFG->CropY1 - 1)
        {
            if (y < CFG->CropY1 + sideSizeY)
                sel_cursor |= 1;
            else if (y <= CFG->CropY2 - sideSizeY)
                sel_cursor |= 2;
            else if (y <= CFG->CropY2)
                sel_cursor |= 3;
        }
        if (x >= CFG->CropX1 - 1)
        {
            if (x < CFG->CropX1 + sideSizeX)
                sel_cursor |= 4;
            else if (x <= CFG->CropX2 - sideSizeX)
                sel_cursor |= 8;
            else if (x <= CFG->CropX2)
                sel_cursor |= 12;
        }

        data->CropMotionType = tr_cursor [sel_cursor];

	GtkWidget *event_box =
	    gtk_widget_get_ancestor(data->PreviewWidget, GTK_TYPE_EVENT_BOX);
	gdk_window_set_cursor(event_box->window,
		data->Cursor[data->CropMotionType]);
    } else {
	if ( data->CropMotionType==top_cursor ||
	     data->CropMotionType==top_left_cursor ||
	     data->CropMotionType==top_right_cursor )
	    if (y<CFG->CropY2) CFG->CropY1 = y;
	if ( data->CropMotionType==bottom_cursor ||
	     data->CropMotionType==bottom_left_cursor ||
	     data->CropMotionType==bottom_right_cursor )
	    if (y>CFG->CropY1) CFG->CropY2 = y;
	if ( data->CropMotionType==left_cursor ||
	     data->CropMotionType==top_left_cursor ||
	     data->CropMotionType==bottom_left_cursor )
	    if (x<CFG->CropX2) CFG->CropX1 = x;
	if ( data->CropMotionType==right_cursor ||
	     data->CropMotionType==top_right_cursor ||
	     data->CropMotionType==bottom_right_cursor )
            if (x>CFG->CropX1) CFG->CropX2 = x;
        if ( data->CropMotionType==move_cursor )
        {
            int d = x - data->OldMouseX;
            if (CFG->CropX1 + d < 0)
                d = -CFG->CropX1;
            if (CFG->CropX2 + d >= data->UF->initialWidth)
                d = data->UF->initialWidth - CFG->CropX2;
            CFG->CropX1 += d;
            CFG->CropX2 += d;

            d = y - data->OldMouseY;
            if (CFG->CropY1 + d < 0)
                d = -CFG->CropY1;
            if (CFG->CropY2 + d >= data->UF->initialHeight)
                d = data->UF->initialHeight - CFG->CropY2;
            CFG->CropY1 += d;
            CFG->CropY2 += d;
        }
	if ( data->CropMotionType!=crop_cursor )
	{
	    fix_crop_aspect(data, data->CropMotionType);
	    update_crop_ranges(data);
	    if (!data->LockAspect) refresh_aspect(data);
	}
    }

    data->OldMouseX = x;
    data->OldMouseY = y;

    return TRUE;
}

static gboolean preview_motion_notify(GtkWidget *event_box, GdkEventMotion *event,
	gpointer user_data)
{
    preview_data *data = get_preview_data(event_box);

    (void)user_data;
#ifdef HAVE_GTKIMAGEVIEW
    if (!gtk_event_box_get_above_child(GTK_EVENT_BOX(event_box))) return FALSE;
#endif
    if ( data->PageNum==4 )
	return crop_motion_notify(data, event);
    if ((event->state&GDK_BUTTON1_MASK)==0) return FALSE;
    if ( !data->PreviewButtonPressed ) return FALSE;
    draw_spot(data, FALSE);
    event_coordinate_rescale(&event->x, &event->y, data);
    data->SpotX2 = event->x;
    data->SpotY2 = event->y;
    render_spot(data);
    return TRUE;
}

static void create_base_image(preview_data *data)
{
    int shrinkSave = CFG->shrink;
    int sizeSave = CFG->size;
    if (CFG->Scale==0) {
	int cropHeight = data->UF->conf->CropY2 - data->UF->conf->CropY1;
	int cropWidth = data->UF->conf->CropX2 - data->UF->conf->CropX1;
	int cropSize = MAX(cropHeight, cropWidth);
	CFG->size = CFG->Zoom / 100.0 * cropSize;
	CFG->shrink = 0;
    } else {
	CFG->size = 0;
	CFG->shrink = CFG->Scale;
    }
    ufraw_convert_image_init(data->UF);
    ufraw_convert_image_first_phase(data->UF);
    CFG->shrink = shrinkSave;
    CFG->size = sizeSave;
    data->fromPhase = ufraw_denoise_phase;
}

static void update_shrink_ranges(preview_data *data)
{
    if (data->FreezeDialog) return;
    data->FreezeDialog++;

    int croppedWidth = CFG->CropX2 - CFG->CropX1;
    int croppedHeight = CFG->CropY2 - CFG->CropY1;
    if (CFG->size > 0) {
        if (croppedHeight > croppedWidth) {
            data->height = CFG->size;
            data->width = CFG->size * croppedWidth / croppedHeight;
            data->shrink = (double)croppedHeight / CFG->size;
        } else {
            data->width = CFG->size;
            data->height = CFG->size * croppedHeight / croppedWidth;
            data->shrink = (double)croppedWidth / CFG->size;
        }
    } else {
        if (CFG->shrink<1) {
            ufraw_message(UFRAW_ERROR, _("Fatal Error: uf->conf->shrink<1"));
            CFG->shrink = 1;
        }
        data->height = croppedHeight / CFG->shrink;
        data->width = croppedWidth / CFG->shrink;
        data->shrink = CFG->shrink;
    }
    gtk_spin_button_set_range(data->HeightSpin, 0, croppedHeight);
    gtk_spin_button_set_range(data->WidthSpin, 0, croppedWidth);
    gtk_adjustment_set_value(data->ShrinkAdjustment, data->shrink);
    gtk_adjustment_set_value(data->HeightAdjustment, data->height);
    gtk_adjustment_set_value(data->WidthAdjustment, data->width);

    data->FreezeDialog--;
}

static void update_crop_ranges(preview_data *data)
{
    if (data->FreezeDialog) return;

    /* Avoid recursive handling of the same event */
    data->FreezeDialog++;

    int CropX1 = CFG->CropX1;
    int CropX2 = CFG->CropX2;
    int CropY1 = CFG->CropY1;
    int CropY2 = CFG->CropY2;

    gtk_spin_button_set_range(data->CropX1Spin, 0, CropX2-1);
    gtk_spin_button_set_range(data->CropY1Spin, 0, CropY2-1);
    gtk_spin_button_set_range(data->CropX2Spin,
	CropX1+1, data->UF->initialWidth);
    gtk_spin_button_set_range(data->CropY2Spin,
	CropY1+1, data->UF->initialHeight);

    gtk_adjustment_set_value(data->CropX1Adjustment, CropX1);
    gtk_adjustment_set_value(data->CropY1Adjustment, CropY1);
    gtk_adjustment_set_value(data->CropX2Adjustment, CropX2);
    gtk_adjustment_set_value(data->CropY2Adjustment, CropY2);

    data->FreezeDialog--;

    int pixbufHeight = gdk_pixbuf_get_height(data->PreviewPixbuf);
    int pixbufWidth = gdk_pixbuf_get_width(data->PreviewPixbuf);
    float scale_x = ((float)pixbufWidth) / data->UF->initialWidth;
    float scale_y = ((float)pixbufHeight) / data->UF->initialHeight;

    CropX1 = floor (CropX1 * scale_x);
    CropX2 =  ceil (CropX2 * scale_x);
    CropY1 = floor (CropY1 * scale_y);
    CropY2 =  ceil (CropY2 * scale_y);

    int x1[4], x2[4], y1[4], y2[4], i=0;
    if (CropX1!=data->DrawnCropX1) {
	x1[i] = MIN(CropX1, data->DrawnCropX1);
	x2[i] = MAX(CropX1, data->DrawnCropX1);
	y1[i] = MIN(CropY1, data->DrawnCropY1);
        y2[i] = MAX(CropY2, data->DrawnCropY2);
        data->DrawnCropX1 = CropX1;
	i++;
    }
    if (CropX2!=data->DrawnCropX2) {
	x1[i] = MIN(CropX2, data->DrawnCropX2);
	x2[i] = MAX(CropX2, data->DrawnCropX2);
	y1[i] = MIN(CropY1, data->DrawnCropY1);
        y2[i] = MAX(CropY2, data->DrawnCropY2);
        data->DrawnCropX2 = CropX2;
	i++;
    }
    if (CropY1!=data->DrawnCropY1) {
	y1[i] = MIN(CropY1, data->DrawnCropY1);
	y2[i] = MAX(CropY1, data->DrawnCropY1);
	x1[i] = MIN(CropX1, data->DrawnCropX1);
        x2[i] = MAX(CropX2, data->DrawnCropX2);
        data->DrawnCropY1 = CropY1;
	i++;
    }
    if (CropY2!=data->DrawnCropY2) {
	y1[i] = MIN(CropY2, data->DrawnCropY2);
	y2[i] = MAX(CropY2, data->DrawnCropY2);
	x1[i] = MIN(CropX1, data->DrawnCropX1);
        x2[i] = MAX(CropX2, data->DrawnCropX2);
        data->DrawnCropY2 = CropY2;
	i++;
    }
    for (i--; i>=0; i--) {
	x1[i] = MAX (x1[i] - 1, 0);
	x2[i] = MIN (x2[i] + 1, pixbufWidth);
	y1[i] = MAX (y1[i] - 1, 0);
        y2[i] = MIN (y2[i] + 1, pixbufHeight);
	preview_draw_area (data, x1[i], y1[i], x2[i]-x1[i], y2[i]-y1[i]);
    }
    update_shrink_ranges(data);
    redraw_navigation_image(data);
}

static void crop_reset(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    user_data = user_data;

    CFG->CropX1 = 0;
    CFG->CropY1 = 0;
    CFG->CropX2 = data->UF->initialWidth;
    CFG->CropY2 = data->UF->initialHeight;
    data->AspectRatio = 0.0;

    set_new_aspect(data);
}

static void zoom_in_event(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    const double prev_zoom = CFG->Zoom;
    (void)user_data;
    if (data->FreezeDialog) return;
    if (CFG->Scale==0) {
	if (CFG->Zoom<100.0/max_scale) CFG->Zoom = 100.0/max_scale;
	CFG->Scale = floor(100.0/CFG->Zoom);
	if (CFG->Scale==100.0/CFG->Zoom) CFG->Scale--;
    } else {
	CFG->Scale--;
    }
    if (CFG->Scale<min_scale) CFG->Scale = min_scale;
    if (CFG->Scale>max_scale) CFG->Scale = max_scale;
    CFG->Zoom = 100.0/CFG->Scale;
    if (prev_zoom != CFG->Zoom) {
	create_base_image(data);
	gtk_adjustment_set_value(data->ZoomAdjustment, CFG->Zoom);
	render_preview(data);
    }
}

static void zoom_out_event(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    const double prev_zoom = CFG->Zoom;
    (void)user_data;
    if (data->FreezeDialog) return;
    if (CFG->Scale==0) {
	if (CFG->Zoom<100.0/max_scale) CFG->Zoom = 100.0/max_scale;
	CFG->Scale = ceil(100.0/CFG->Zoom);
	if (CFG->Scale==100.0/CFG->Zoom) CFG->Scale++;
    } else {
	CFG->Scale++;
    }
    if (CFG->Scale<min_scale) CFG->Scale = min_scale;
    if (CFG->Scale>max_scale) CFG->Scale = max_scale;
    CFG->Zoom = 100.0/CFG->Scale;
    if (prev_zoom != CFG->Zoom) {
	create_base_image(data);
	gtk_adjustment_set_value(data->ZoomAdjustment, CFG->Zoom);
	render_preview(data);
    }
}

#ifdef HAVE_GTKIMAGEVIEW
static void zoom_fit_event(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    const double prev_zoom = CFG->Zoom;
    (void)user_data;
    if (data->FreezeDialog) return;
    GtkWidget *preview =
	gtk_widget_get_ancestor(data->PreviewWidget, GTK_TYPE_IMAGE_SCROLL_WIN);
    int previewWidth = preview->allocation.width;
    int previewHeight = preview->allocation.height;
    double wScale = (double)data->UF->initialWidth / previewWidth;
    double hScale = (double)data->UF->initialHeight / previewHeight;
    CFG->Zoom = 100/MAX(wScale, hScale);
    CFG->Scale = 0;
    if (prev_zoom != CFG->Zoom) {
	create_base_image(data);
	gtk_adjustment_set_value(data->ZoomAdjustment, CFG->Zoom);
	render_preview(data);
    }
}
#endif

static void zoom_max_event(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    const double prev_zoom = CFG->Zoom;
    (void)user_data;
    if (data->FreezeDialog) return;
    CFG->Scale = min_scale;
    CFG->Zoom = 100.0/CFG->Scale;
    if (prev_zoom != CFG->Zoom) {
	create_base_image(data);
	gtk_adjustment_set_value(data->ZoomAdjustment, CFG->Zoom);
	render_preview(data);
    }
}

static void flip_image(GtkWidget *widget, int flip)
{
    int temp;
    preview_data *data = get_preview_data(widget);

    if (data->FreezeDialog) return;
    ufraw_flip_image(data->UF, flip);

    if (flip & 1) {
	temp = CFG->CropX1;
	CFG->CropX1 = data->UF->initialWidth - CFG->CropX2;
	CFG->CropX2 = data->UF->initialWidth - temp;
	if ( data->SpotX1>=0 ) {
	    temp = data->SpotX1;
	    data->SpotX1 = data->UF->initialWidth - data->SpotX2;
	    data->SpotX2 = data->UF->initialWidth - temp;
	}
    }
    if (flip & 2) {
	temp = CFG->CropY1;
	CFG->CropY1 = data->UF->initialHeight - CFG->CropY2;
	CFG->CropY2 = data->UF->initialHeight - temp;
	if ( data->SpotX1>=0 ) {
	    temp = data->SpotY1;
	    data->SpotY1 = data->UF->initialHeight - data->SpotY2;
	    data->SpotY2 = data->UF->initialHeight - temp;
	}
    }
    if (flip & 4) {
	temp = data->UF->initialWidth;
	data->UF->initialWidth = data->UF->initialHeight;
	data->UF->initialHeight = temp;
	temp = CFG->CropX1;
	CFG->CropX1 = CFG->CropY1;
	CFG->CropY1 = temp;
	temp = CFG->CropX2;
	CFG->CropX2 = CFG->CropY2;
	CFG->CropY2 = temp;
	if ( data->SpotX1>=0 ) {
	    temp = data->SpotX1;
	    data->SpotX1 = data->SpotY1;
	    data->SpotY1 = temp;
	    temp = data->SpotX2;
	    data->SpotX2 = data->SpotY2;
	    data->SpotY2 = temp;
	}
    }
    render_init(data);
    int height = gdk_pixbuf_get_height(data->PreviewPixbuf);
    int width = gdk_pixbuf_get_width(data->PreviewPixbuf);
    if ( data->RenderLine<MAX(height,width) ) {
	/* We are in the middle or a rendering scan,
	 * so just start from the beginning. */
	data->RenderLine = 0;
    } else {
	/* Full image was already rendered.
	 * We only need to draw the flipped image. */
	render_special_mode(widget, render_default);
    }
    refresh_aspect(data);
}

/*
 * Used to offer choices of aspect ratios, and to show numbers
 * as ratios.
 */
static const struct
{
    float val;
    const char text[5];
} predef_aspects [] =
{
    { 21.0/ 9.0, "21:9" },
    { 16.0/ 9.0, "16:9" },
    {  3.0/ 2.0, "3:2"  },
    {  7.0/ 5.0, "7:5"  },
    {  4.0/ 3.0, "4:3"  },
    {  5.0/ 4.0, "5:4"  },
    {  1.0/ 1.0, "1:1"  },
    {  4.0/ 5.0, "4:5"  },
    {  3.0/ 4.0, "3:4"  },
    {  5.0/ 7.0, "5:7"  },
    {  2.0/ 3.0, "2:3"  },
    {  9.0/16.0, "9:16" },
    {  9.0/21.0, "9:21" }
};

static void refresh_aspect(preview_data *data)
{
    int dy = CFG->CropY2 - CFG->CropY1;
    data->AspectRatio = dy ? ((CFG->CropX2 - CFG->CropX1) / (float)dy) : 1.0;

    // Look through a predefined list of aspect ratios
    size_t i;
    for (i = 0; i < sizeof (predef_aspects) / sizeof (predef_aspects [0]); i++)
        if (data->AspectRatio >= predef_aspects [i].val * 0.999 &&
            data->AspectRatio <= predef_aspects [i].val * 1.001)
        {
	    data->FreezeDialog++;
            gtk_entry_set_text (data->AspectEntry, predef_aspects [i].text);
	    data->FreezeDialog--;
            return;
        }
    char *text = g_strdup_printf ("%.4g", data->AspectRatio);
    data->FreezeDialog++;
    gtk_entry_set_text (data->AspectEntry, text);
    data->FreezeDialog--;
    g_free(text);
}

static void fix_crop_aspect(preview_data *data, CursorType cursor)
{
    float aspect;

    /* Sanity checks first */
    if (CFG->CropX2 < CFG->CropX1)
        CFG->CropX2 = CFG->CropX1;
    if (CFG->CropY2 < CFG->CropY1)
        CFG->CropY2 = CFG->CropY1;
    if (CFG->CropX1 < 0)
        CFG->CropX1 = 0;
    if (CFG->CropX2 > data->UF->initialWidth)
        CFG->CropX2 = data->UF->initialWidth;
    if (CFG->CropY1 < 0)
        CFG->CropY1 = 0;
    if (CFG->CropY2 > data->UF->initialHeight)
        CFG->CropY2 = data->UF->initialHeight;

    if (!data->LockAspect)
        return;

    if (data->AspectRatio == 0)
        aspect = ((float)data->UF->initialWidth) / data->UF->initialHeight;
    else
        aspect = data->AspectRatio;

    switch (cursor)
    {
        case left_cursor:
        case right_cursor:
            {
                /* Try to expand the rectangle evenly vertically,
                 * if space permits */
                float cy = (CFG->CropY1 + CFG->CropY2) / 2.0;
                float dy = (CFG->CropX2 - CFG->CropX1) * 0.5 / aspect;
                int fix_dx = 0;
                if (dy > cy)
                    dy = cy, fix_dx++;
                if (cy + dy > data->UF->initialHeight)
                    dy = data->UF->initialHeight - cy, fix_dx++;
                if (fix_dx)
                {
                    float dx = rint (dy * 2.0 * aspect);
                    if (cursor == left_cursor)
                        CFG->CropX1 = CFG->CropX2 - dx;
                    else
                        CFG->CropX2 = CFG->CropX1 + dx;
                }
                CFG->CropY1 = rint (cy - dy);
                CFG->CropY2 = rint (cy + dy);
            }
            break;
        case top_cursor:
        case bottom_cursor:
            {
                /* Try to expand the rectangle evenly horizontally,
                 * if space permits */
                float cx = (CFG->CropX1 + CFG->CropX2) / 2.0;
                float dx = (CFG->CropY2 - CFG->CropY1) * 0.5 * aspect;
                int fix_dy = 0;
                if (dx > cx)
                    dx = cx, fix_dy++;
                if (cx + dx > data->UF->initialWidth)
                    dx = data->UF->initialWidth - cx, fix_dy++;
                if (fix_dy)
                {
                    float dy = rint (dx * 2.0 / aspect);
                    if (cursor == top_cursor)
                        CFG->CropY1 = CFG->CropY2 - dy;
                    else
                        CFG->CropY2 = CFG->CropY1 + dy;
                }
                CFG->CropX1 = rint (cx - dx);
                CFG->CropX2 = rint (cx + dx);
            }
            break;

        case top_left_cursor:
        case top_right_cursor:
        case bottom_left_cursor:
        case bottom_right_cursor:
            {
                /* Adjust rectangle width/height according to aspect ratio
                 * trying to preserve the area of the crop rectangle.
                 * See the comment in set_new_aspect() */
                float dy, dx = sqrt (aspect * (CFG->CropX2 - CFG->CropX1) *
                                     (CFG->CropY2 - CFG->CropY1));

                for (;;)
                {
                    if (cursor == top_left_cursor ||
                        cursor == bottom_left_cursor)
                    {
                        if (CFG->CropX2 < dx)
                            dx = CFG->CropX2;
                        CFG->CropX1 = CFG->CropX2 - dx;
                    }
                    else
                    {
                        if (CFG->CropX1 + dx > data->UF->initialWidth)
                            dx = data->UF->initialWidth - CFG->CropX1;
                        CFG->CropX2 = CFG->CropX1 + dx;
                    }

                    dy = dx / aspect;

                    if (cursor == top_left_cursor ||
                        cursor == top_right_cursor)
                    {
                        if (CFG->CropY2 < dy)
                        {
                            dx = CFG->CropY2 * aspect;
                            continue;
                        }
                        CFG->CropY1 = CFG->CropY2 - dy;
                    }
                    else
                    {
                        if (CFG->CropY1 + dy > data->UF->initialHeight)
                        {
                            dx = (data->UF->initialHeight - CFG->CropY1) * aspect;
                            continue;
                        }
                        CFG->CropY2 = CFG->CropY1 + dy;
                    }
                    break;
                }
            }
            break;

        default:
            return;
    }

    update_crop_ranges(data);
}

/* Modify current crop area so that it fits current aspect ratio */
static void set_new_aspect(preview_data *data)
{
    float cx, cy, dx, dy;

    if (data->AspectRatio == 0)
    {
        data->AspectRatio = ((float)data->UF->initialWidth) / data->UF->initialHeight;
        refresh_aspect (data);
    }

    /* Crop area center never changes */
    cx = (CFG->CropX1 + CFG->CropX2) / 2.0;
    cy = (CFG->CropY1 + CFG->CropY2) / 2.0;

    /* Adjust the current crop area width/height taking into account
     * the new aspect ratio. The rule is to keep one of the dimensions
     * and modify other dimension in such a way that the area of the
     * new crop area will be maximal.
     */
    dx = CFG->CropX2 - cx;
    dy = CFG->CropY2 - cy;
    if (dx / dy > data->AspectRatio)
        dy = dx / data->AspectRatio;
    else
        dx = dy * data->AspectRatio;

    if (dx > cx)
    {
        dx = cx;
        dy = dx / data->AspectRatio;
    }
    if (cx + dx > data->UF->initialWidth)
    {
        dx = data->UF->initialWidth - cx;
        dy = dx / data->AspectRatio;
    }

    if (dy > cy)
    {
        dy = cy;
        dx = dy * data->AspectRatio;
    }
    if (cy + dy > data->UF->initialHeight)
    {
        dy = data->UF->initialHeight - cy;
        dx = dy * data->AspectRatio;
    }

    CFG->CropX1 = rint (cx - dx);
    CFG->CropX2 = rint (cx + dx);
    CFG->CropY1 = rint (cy - dy);
    CFG->CropY2 = rint (cy + dy);

    update_crop_ranges(data);
}

static void lock_aspect(GtkWidget *widget)
{
    preview_data *data = get_preview_data(widget);
    data->LockAspect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void aspect_changed(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    if (data->FreezeDialog) return;
    (void)user_data;
    const gchar *text = gtk_entry_get_text(data->AspectEntry);
    if (text)
    {
        float aspect = 0.0, aspect_div, aspect_quot;
        if (sscanf(text, "%f : %f", &aspect_div, &aspect_quot) == 2)
        {
            if (aspect_quot)
                aspect = aspect_div / aspect_quot;
        }
        else
            sscanf(text, "%g", &aspect);

        if (aspect >= 0.1 && aspect <= 10.0)
            data->AspectRatio = aspect;
    }
    set_new_aspect (data);
}

static void set_darkframe_label(preview_data *data)
{
    if (CFG->darkframeFile[0] != '\0') {
	char *basename = g_path_get_basename(CFG->darkframeFile);
	gtk_label_set_text(GTK_LABEL(data->DarkFrameLabel), basename);
	g_free(basename);
    } else {
	// No darkframe file
	gtk_label_set_text(GTK_LABEL(data->DarkFrameLabel), _("None"));
    }
}

static void set_darkframe(preview_data *data)
{
    set_darkframe_label(data);
    create_base_image(data);
    render_preview(data);
}

static void free_darkframe(conf_data *conf)
{
    if (conf->darkframe != NULL) {
	ufraw_close(conf->darkframe);
	g_free(conf->darkframe);
	conf->darkframe = NULL;
	conf->darkframeFile[0] = '\0';
    }
}

static void load_darkframe(GtkWidget *widget, void *unused)
{
    preview_data *data = get_preview_data(widget);
    GtkFileChooser *fileChooser;
    GSList *list, *saveList;
    ufraw_data *uf;
    char *basedir;

    if (data->FreezeDialog) return;
    basedir = g_path_get_dirname(CFG->darkframeFile);
    fileChooser = ufraw_raw_chooser(CFG,
				    basedir,
				    _("Load dark frame"),
				    GTK_WINDOW(gtk_widget_get_toplevel(widget)),
				    GTK_STOCK_CANCEL,
				    FALSE);
    free(basedir);
    
    if (gtk_dialog_run(GTK_DIALOG(fileChooser))==GTK_RESPONSE_ACCEPT) {
	saveList = gtk_file_chooser_get_filenames(fileChooser);
	for (list = saveList; list != NULL; list = g_slist_next(list)) {
	    if ((uf = ufraw_load_darkframe(list->data)) != NULL) {
		free_darkframe(CFG);
		CFG->darkframe = uf;
		g_strlcpy(CFG->darkframeFile, list->data, max_path);
		set_darkframe(data);
	    }
	    // FIXME: handle errors?
            g_free(list->data);
        }
        g_slist_free(saveList);
    }
    ufraw_focus(fileChooser, FALSE);
    gtk_widget_destroy(GTK_WIDGET(fileChooser));
    (void)unused;
}

static void reset_darkframe(GtkWidget *widget, void *unused)
{
    preview_data *data = get_preview_data(widget);

    if (data->FreezeDialog) return;
    if (CFG->darkframe == NULL) return;
    
    free_darkframe(CFG);
    set_darkframe(data);
    (void)unused;
}

static GtkWidget *notebook_page_new(GtkNotebook *notebook, char *text,
    char *icon, GtkTooltips *tooltips)
{
    GtkWidget *page = gtk_vbox_new(FALSE, 0);
    if ( icon==NULL ) {
	GtkWidget *label = gtk_label_new(text);
	gtk_notebook_append_page(notebook, GTK_WIDGET(page), label);
    } else {
	GtkWidget *event_box = gtk_event_box_new();
	GtkWidget *image = gtk_image_new_from_stock(icon,
		GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(event_box), image);
        gtk_tooltips_set_tip(tooltips, event_box, text, NULL);
        gtk_widget_show_all(event_box);
	gtk_notebook_append_page(notebook, GTK_WIDGET(page), event_box);
    }
    return page;
}

static GtkWidget *table_with_frame(GtkWidget *box, char *label, gboolean expand)
{
    GtkWidget *frame, *expander;
    GtkWidget *table;

    if (label!=NULL) {
	table = gtk_table_new(10, 10, FALSE);
        expander = gtk_expander_new(label);
        gtk_expander_set_expanded(GTK_EXPANDER(expander), expand);
        gtk_box_pack_start(GTK_BOX(box), expander, FALSE, FALSE, 0);
        frame = gtk_frame_new(NULL);
        gtk_container_add(GTK_CONTAINER(expander), GTK_WIDGET(frame));
        gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(table));
    } else {
	table = gtk_table_new(10, 10, FALSE);
        frame = gtk_frame_new(NULL);
        gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(table));
    }
    return table;
}

static void button_update(GtkWidget *button, gpointer user_data)
{
    preview_data *data = get_preview_data(button);
    user_data = user_data;

    if (button==data->ResetWBButton) {
	int c;
	g_strlcpy(CFG->wb, data->initialWB, max_name);
	CFG->temperature = data->initialTemperature;
	CFG->green = data->initialGreen;
	for (c=0; c<4; c++) CFG->chanMul[c] = data->initialChanMul[c];
    }
    if (button==data->ResetGammaButton) {
	CFG->profile[0][CFG->profileIndex[0]].gamma =
		profile_default_gamma(&CFG->profile[0][CFG->profileIndex[0]]);
    }
    if (button==data->ResetLinearButton) {
	CFG->profile[0][CFG->profileIndex[0]].linear =
		profile_default_linear(&CFG->profile[0][CFG->profileIndex[0]]);
    }
    if (button==GTK_WIDGET(data->AutoExposureButton)) {
	CFG->autoExposure =
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    }
    if (button==data->ResetExposureButton) {
	CFG->exposure = conf_default.exposure;
	CFG->autoExposure = FALSE;
    }
    if (button==data->ResetThresholdButton) {
	CFG->threshold = conf_default.threshold;
    }
    if (button==data->ResetSaturationButton) {
	CFG->saturation = conf_default.saturation;
    }
    if (button==GTK_WIDGET(data->AutoBlackButton)) {
	CFG->autoBlack =
	    gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    }
    if (button==data->ResetBlackButton) {
	CurveDataSetPoint(&CFG->curve[CFG->curveIndex], 0,
		conf_default.black, 0);
	CFG->autoBlack = FALSE;
    }
    if (button==GTK_WIDGET(data->AutoCurveButton)) {
	CFG->curveIndex = manual_curve;
	ufraw_auto_curve(data->UF);
    }
    if (button==data->ResetBaseCurveButton) {
	if (CFG->BaseCurveIndex==manual_curve) {
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_numAnchors = 2;
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[0].x = 0.0;
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[0].y = 0.0;
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[1].x = 1.0;
	    CFG->BaseCurve[CFG->BaseCurveIndex].m_anchors[1].y = 1.0;
	} else {
	    CFG->BaseCurveIndex = linear_curve;
	}
	curveeditor_widget_set_curve(data->BaseCurveWidget,
		&CFG->BaseCurve[CFG->BaseCurveIndex]);
    }
    if (button==data->ResetCurveButton) {
	if (CFG->curveIndex==manual_curve) {
	    CFG->curve[CFG->curveIndex].m_numAnchors = 2;
	    CFG->curve[CFG->curveIndex].m_anchors[1].x = 1.0;
	    CFG->curve[CFG->curveIndex].m_anchors[1].y = 1.0;
	} else {
	    CFG->curveIndex = linear_curve;
	}
    }
    if (CFG->autoExposure==enabled_state) CFG->autoExposure = apply_state;
    if (CFG->autoBlack==enabled_state) CFG->autoBlack = apply_state;
    update_scales(data);
    return;
}

static void restore_details_button_set(GtkButton *button, preview_data *data)
{
#if !GTK_CHECK_VERSION(2,6,0)
    GtkWidget *lastImage = gtk_bin_get_child(GTK_BIN(button));
    if ( lastImage!=NULL )
	gtk_container_remove(GTK_CONTAINER(button), lastImage);
    GtkWidget *image;
#endif
    const char *state;
    switch (CFG->restoreDetails) {
    case clip_details:
#if GTK_CHECK_VERSION(2,6,0)
	gtk_button_set_image(button, gtk_image_new_from_stock(
		GTK_STOCK_CUT, GTK_ICON_SIZE_BUTTON));
#else
	image = gtk_image_new_from_stock(GTK_STOCK_CUT, GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_widget_show(image);
#endif
	state = _("clip");
	break;
    case restore_lch_details:
#if GTK_CHECK_VERSION(2,6,0)
	gtk_button_set_image(button, gtk_image_new_from_stock(
		"restore-highlights-lch", GTK_ICON_SIZE_BUTTON));
#else
	image = gtk_image_new_from_stock("restore-highlights-lch",
		GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_widget_show(image);
#endif
	state = _("restore in LCH space for soft details");
	break;
    case restore_hsv_details:
#if GTK_CHECK_VERSION(2,6,0)
	gtk_button_set_image(button, gtk_image_new_from_stock(
		"restore-highlights-hsv", GTK_ICON_SIZE_BUTTON));
#else
	image = gtk_image_new_from_stock("restore-highlights-hsv",
		GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_widget_show(image);
#endif
	state = _("restore in HSV space for sharp details");
	break;
    default:
	state = "Error";
    }
    char *text = g_strdup_printf(_("Restore details for negative EV\n"
	    "Current state: %s"), state);
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(button), text, NULL);
    g_free(text);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
}

static void clip_highlights_button_set(GtkButton *button, preview_data *data)
{
#if !GTK_CHECK_VERSION(2,6,0)
    GtkWidget *lastImage = gtk_bin_get_child(GTK_BIN(button));
    if ( lastImage!=NULL )
	gtk_container_remove(GTK_CONTAINER(button), lastImage);
    GtkWidget *image;
#endif
    const char *state;
    switch (CFG->clipHighlights) {
    case digital_highlights:
#if GTK_CHECK_VERSION(2,6,0)
	gtk_button_set_image(button, gtk_image_new_from_stock(
		"clip-highlights-digital", GTK_ICON_SIZE_BUTTON));
#else
	image = gtk_image_new_from_stock("clip-highlights-digital",
		GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_widget_show(image);
#endif
	state = _("digital linear");
	break;
    case film_highlights:
#if GTK_CHECK_VERSION(2,6,0)
	gtk_button_set_image(button, gtk_image_new_from_stock(
		"clip-highlights-film", GTK_ICON_SIZE_BUTTON));
#else
	image = gtk_image_new_from_stock("clip-highlights-film",
		GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(button), image);
	gtk_widget_show(image);
#endif
	state = _("soft film like");
	break;
    default:
	state = "Error";
    }
    char *text = g_strdup_printf(_("Clip highlights for positive EV\n"
	    "Current state: %s"), state);
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(button), text, NULL);
    g_free(text);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
}

static void toggle_button_update(GtkToggleButton *button, gboolean *valuep)
{
    preview_data *data = get_preview_data(button);
    if (valuep==&CFG->restoreDetails) {
	/* Our untoggling of the button creates a redundant event. */
	if ( gtk_toggle_button_get_active(button) ) {
	    /* Scroll through the settings. */
	    CFG->restoreDetails =
		    (CFG->restoreDetails+1) % restore_types;
	    restore_details_button_set(GTK_BUTTON(button), data);
	    update_scales(data);
	}
    } else if (valuep==&CFG->clipHighlights) {
	/* Our untoggling of the button creates a redundant event. */
	if ( gtk_toggle_button_get_active(button) ) {
	    /* Scroll through the settings. */
	    CFG->clipHighlights =
		    (CFG->clipHighlights+1) % highlights_types;
	    clip_highlights_button_set(GTK_BUTTON(button), data);
	    update_scales(data);
	}
    } else {
	*valuep = gtk_toggle_button_get_active(button);
	if (valuep==&data->UF->useMatrix) {
	    CFG->profile[0][CFG->profileIndex[0]].useMatrix = *valuep;
	    if (CFG->autoExposure==enabled_state)
		CFG->autoExposure = apply_state;
	}
	if ( valuep==&CFG->overExp || valuep==&CFG->underExp ) {
	    if (CFG->overExp || CFG->underExp)
		start_blink(data);
	} else {
	    render_preview(data);
	}
    }
}

static void toggle_button(GtkTable *table, int x, int y, char *label,
	gboolean *valuep)
{
    GtkWidget *widget, *align;

    widget = gtk_check_button_new_with_label(label);
    if (label==NULL) {
        align = gtk_alignment_new(1, 0, 0, 1);
        gtk_container_add(GTK_CONTAINER(align), widget);
    } else align = widget;
    gtk_table_attach(table, align, x, x+1, y, y+1, 0, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), *valuep);
    g_signal_connect(G_OBJECT(widget), "toggled",
            G_CALLBACK(toggle_button_update), valuep);
}

static void adjustment_update(GtkAdjustment *adj, double *valuep)
{
    preview_data *data = get_preview_data(adj);
    if (data->FreezeDialog) return;

    if (valuep==&CFG->profile[0][0].gamma)
        valuep = (void *)&CFG->profile[0][CFG->profileIndex[0]].gamma;
    if (valuep==&CFG->profile[0][0].linear)
        valuep = (void *)&CFG->profile[0][CFG->profileIndex[0]].linear;

    if ( (int *)valuep==&CFG->CropX1 || (int *)valuep==&CFG->CropX2 ||
	 (int *)valuep==&CFG->CropY1 || (int *)valuep==&CFG->CropY2 ) {
	*((int *)valuep) = (int) gtk_adjustment_get_value(adj);
	CursorType cursor = left_cursor;
	if ( (int *)valuep==&CFG->CropX1) cursor = left_cursor;
	if ( (int *)valuep==&CFG->CropX2) cursor = right_cursor;
	if ( (int *)valuep==&CFG->CropY1) cursor = top_cursor;
	if ( (int *)valuep==&CFG->CropY2) cursor = bottom_cursor;
	fix_crop_aspect(data, cursor);
        update_crop_ranges(data);
	if (!data->LockAspect) refresh_aspect(data);
	return;
    }
    /* Do noting if value didn't really change */
    long accuracy =
	(long)g_object_get_data(G_OBJECT(adj), "Adjustment-Accuracy");
    float change = fabs(*valuep-gtk_adjustment_get_value(adj));
    float min_change = pow(10,-accuracy)/2;
    if ( change < min_change )
	return;
    else
	*valuep = gtk_adjustment_get_value(adj);

    if (valuep==&CFG->temperature || valuep==&CFG->green) {
	g_strlcpy(CFG->wb, manual_wb, max_name);
	ufraw_set_wb(data->UF);
    }
    if (valuep>=&CFG->chanMul[0] && valuep<=&CFG->chanMul[3]) {
	g_strlcpy(CFG->wb, spot_wb, max_name);
	ufraw_set_wb(data->UF);
    }
    if (valuep==&CFG->WBTuning) {
	if ( strcmp(data->UF->conf->wb, auto_wb)==0 ||
	     strcmp(data->UF->conf->wb, camera_wb)==0 ) {
	    /* Prevent recalculation of Camera/Auto WB on WBTuning events */
	    data->UF->conf->WBTuning = 0;
	} else {
	    ufraw_set_wb(data->UF);
	}
    }
    if (valuep==&CFG->exposure) {
	CFG->autoExposure = FALSE;
    }
    if (valuep==&CFG->Zoom) {
	CFG->Scale = 0;
	create_base_image(data);
    } else if (valuep==&CFG->threshold) {
	data->fromPhase = ufraw_denoise_phase;
    } else {
	if (CFG->autoExposure==enabled_state) CFG->autoExposure = apply_state;
	if (CFG->autoBlack==enabled_state) CFG->autoBlack = apply_state;
    }
    int croppedWidth = CFG->CropX2 - CFG->CropX1;
    int croppedHeight = CFG->CropY2 - CFG->CropY1;
    if ( valuep==&data->shrink ) {
        data->height = croppedHeight / data->shrink;
        data->width = croppedWidth / data->shrink;
    }
    if ( valuep==&data->height ) {
        data->width = data->height * croppedWidth / croppedHeight;
        data->shrink = (double)croppedHeight / data->height;
    }
    if ( valuep==&data->width ) {
        data->height = data->width * croppedHeight / croppedWidth;
        data->shrink = (double)croppedWidth / data->width;
    }
    update_scales(data);
}

static GtkAdjustment *adjustment_scale(GtkTable *table,
    int x, int y, char *label, double value, double *valuep,
    double min, double max, double step, double jump, long accuracy, char *tip)
{
    preview_data *data = get_preview_data(table);
    GtkAdjustment *adj;
    GtkWidget *w, *l, *icon;

    w = gtk_event_box_new();
    if ( strcmp(label, "exposure")==0 ) {
	icon = gtk_image_new_from_stock(label, GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(w), icon);
    } else {
	l = gtk_label_new(label);
	gtk_misc_set_alignment(GTK_MISC(l), 1, 0.5);
	gtk_container_add(GTK_CONTAINER(w), l);
    }
    gtk_tooltips_set_tip(data->ToolTips, w, tip, NULL);
    gtk_table_attach(table, w, x, x+1, y, y+1, GTK_SHRINK|GTK_FILL, 0, 0, 0);
    adj = GTK_ADJUSTMENT(gtk_adjustment_new(value, min, max, step, jump, 0));
    g_object_set_data(G_OBJECT(adj), "Adjustment-Accuracy",(gpointer)accuracy);

    w = gtk_hscale_new(adj);
    g_object_set_data(G_OBJECT(adj), "Parent-Widget", w);
    gtk_scale_set_draw_value(GTK_SCALE(w), FALSE);
    gtk_tooltips_set_tip(data->ToolTips, w, tip, NULL);
    gtk_table_attach(table, w, x+1, x+5, y, y+1, GTK_EXPAND|GTK_FILL, 0, 0, 0);
    g_signal_connect(G_OBJECT(adj), "value-changed",
            G_CALLBACK(adjustment_update), valuep);

    w = gtk_spin_button_new(adj, step, accuracy);
    gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(w), FALSE);
    gtk_spin_button_set_update_policy(GTK_SPIN_BUTTON(w), GTK_UPDATE_IF_VALID);
    gtk_tooltips_set_tip(data->ToolTips, w, tip, NULL);
    gtk_table_attach(table, w, x+5, x+7, y, y+1, GTK_SHRINK|GTK_FILL, 0, 0, 0);
    return adj;
}

static void combo_update(GtkWidget *combo, gint *valuep)
{
    preview_data *data = get_preview_data(combo);
    if (data->FreezeDialog) return;
    if ((char *)valuep==CFG->wb) {
	int i = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	g_strlcpy(CFG->wb, g_list_nth_data(data->WBPresets, i), max_name);
    } else {
	*valuep = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    }
    if (valuep==&CFG->BaseCurveIndex) {
        if (!CFG_cameraCurve && CFG->BaseCurveIndex>camera_curve-2)
            CFG->BaseCurveIndex += 2;
	curveeditor_widget_set_curve(data->BaseCurveWidget,
		&CFG->BaseCurve[CFG->BaseCurveIndex]);
    } else if (valuep==&CFG->curveIndex) {
	curveeditor_widget_set_curve(data->CurveWidget,
		&CFG->curve[CFG->curveIndex]);
    } else if ((char *)valuep==CFG->wb) {
        ufraw_set_wb(data->UF);
    }
    if ( valuep==&data->Interpolation ) {
	CFG->interpolation = data->InterpolationTable[data->Interpolation];
    } else {
	if (CFG->autoExposure==enabled_state) CFG->autoExposure = apply_state;
	if (CFG->autoBlack==enabled_state) CFG->autoBlack = apply_state;
        update_scales(data);
    }
}

static void radio_menu_update(GtkWidget *item, gint *valuep)
{
    if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item))) {
	preview_data *data = get_preview_data(item);
	*valuep = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),
		    "Radio-Value"));
	render_preview(data);
    }
}

static void container_remove(GtkWidget *widget, gpointer user_data)
{
    GtkContainer *container = GTK_CONTAINER(user_data);
    gtk_container_remove(container, widget);
}

static void delete_from_list(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    GtkDialog *dialog;
    long type, index;

    dialog = GTK_DIALOG(gtk_widget_get_ancestor(widget, GTK_TYPE_DIALOG));
    type = (long)g_object_get_data(G_OBJECT(widget), "Type");
    index = (long)user_data;
    if (type<profile_types) {
        gtk_combo_box_remove_text(data->ProfileCombo[type], index);
        CFG->profileCount[type]--;
	if ( CFG->profileIndex[type]==index )
            CFG->profileIndex[type] = conf_default.profileIndex[type];
	else if ( CFG->profileIndex[type]>index )
            CFG->profileIndex[type]--;
        for (; index<CFG->profileCount[type]; index++)
            CFG->profile[type][index] = CFG->profile[type][index+1];
	gtk_combo_box_set_active(data->ProfileCombo[type],
		CFG->profileIndex[type]);
    } else if (type==profile_types+base_curve) {
        if (CFG_cameraCurve)
            gtk_combo_box_remove_text(data->BaseCurveCombo, index);
        else
            gtk_combo_box_remove_text(data->BaseCurveCombo, index-2);
        CFG->BaseCurveCount--;
	if ( CFG->BaseCurveIndex==index )
            CFG->BaseCurveIndex = conf_default.BaseCurveIndex;
	else if ( CFG->BaseCurveIndex>index )
            CFG->BaseCurveIndex--;
	if ( CFG->BaseCurveIndex==camera_curve && !CFG_cameraCurve )
	    CFG->BaseCurveIndex = linear_curve;
        for (; index<CFG->BaseCurveCount; index++)
            CFG->BaseCurve[index] = CFG->BaseCurve[index+1];
	if ( CFG->BaseCurveIndex>camera_curve && !CFG_cameraCurve )
	    gtk_combo_box_set_active(data->BaseCurveCombo,
		    CFG->BaseCurveIndex-2);
	else
	    gtk_combo_box_set_active(data->BaseCurveCombo,
		    CFG->BaseCurveIndex);
	curveeditor_widget_set_curve(data->BaseCurveWidget,
		&CFG->BaseCurve[CFG->BaseCurveIndex]);
    } else if (type==profile_types+luminosity_curve) {
        gtk_combo_box_remove_text(data->CurveCombo, index);
        CFG->curveCount--;
	if ( CFG->curveIndex==index )
            CFG->curveIndex = conf_default.curveIndex;
	else if ( CFG->curveIndex>index )
            CFG->curveIndex--;
        for (; index<CFG->curveCount; index++)
            CFG->curve[index] = CFG->curve[index+1];
	gtk_combo_box_set_active(data->CurveCombo, CFG->curveIndex);
	curveeditor_widget_set_curve(data->CurveWidget,
	    &CFG->curve[CFG->curveIndex]);
    }
    data->OptionsChanged = TRUE;
    gtk_dialog_response(dialog, GTK_RESPONSE_APPLY);
}

static void configuration_save(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    user_data = user_data;
    conf_save(CFG, NULL, NULL);
    data->SaveConfig = *data->UF->conf;
}

static void gimp_entry_changed(GtkEntry *entry, char text[])
{
    g_strlcpy(text, gtk_entry_get_text(entry), max_path);
}

static void gimp_reset_clicked(GtkWidget *widget, GtkEntry *entry)
{
    (void)widget;
    gtk_entry_set_text(entry, conf_default.remoteGimpCommand);
}

static void options_combo_update(GtkWidget *combo, gint *valuep)
{
    GtkDialog *dialog;

    *valuep = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    dialog = GTK_DIALOG(gtk_widget_get_ancestor(combo, GTK_TYPE_DIALOG));
    gtk_dialog_response(dialog, GTK_RESPONSE_APPLY);
}

static void options_dialog(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    GtkWidget *optionsDialog, *profileTable[profile_types];
    GtkComboBox *confCombo;
    GtkWidget *notebook, *label, *page, *button, *text, *box, *image, *event;
    GtkTable *baseCurveTable, *curveTable, *table;
    GtkTextBuffer *confBuffer, *buffer;
    char txt[max_name], *buf;
    long i, j, response;

    user_data = user_data;
    if (data->FreezeDialog) return;
    data->OptionsChanged = FALSE;
    int saveBaseCurveIndex = CFG->BaseCurveIndex;
    int saveCurveIndex = CFG->curveIndex;
    int saveProfileIndex[profile_types];
    for (i=0; i<profile_types; i++)
	saveProfileIndex[i] = CFG->profileIndex[i];
    optionsDialog = gtk_dialog_new_with_buttons(_("UFRaw options"),
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
    g_object_set_data(G_OBJECT(optionsDialog), "Preview-Data", data);
    ufraw_focus(optionsDialog, TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(optionsDialog),
            GTK_RESPONSE_OK);
    notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(optionsDialog)->vbox),
            notebook);

    label =gtk_label_new(_("Settings"));
    box = gtk_vbox_new(FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, label);
    profileTable[in_profile] =
	    table_with_frame(box, _("Input color profiles"), TRUE);
    profileTable[out_profile] =
	    table_with_frame(box, _("Output color profiles"), TRUE);
    profileTable[display_profile] =
	    table_with_frame(box, _("Display color profiles"), TRUE);
    baseCurveTable = GTK_TABLE(table_with_frame(box, _("Base Curves"), TRUE));
    curveTable = GTK_TABLE(table_with_frame(box, _("Luminosity Curves"), TRUE));
    GtkTable *settingsTable =
	    GTK_TABLE(table_with_frame(box, _("Settings"), TRUE));
    label = gtk_label_new(_("Remote Gimp command"));
    gtk_table_attach(settingsTable, label, 0, 1, 0, 1, 0, 0, 0, 0);
    GtkEntry *gimpEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_max_length(gimpEntry, max_path);
    gtk_entry_set_text(gimpEntry, data->UF->conf->remoteGimpCommand);
    gtk_table_attach(settingsTable, GTK_WIDGET(gimpEntry), 1, 2, 0, 1,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);
    g_signal_connect(G_OBJECT(gimpEntry), "changed",
            G_CALLBACK(gimp_entry_changed), data->UF->conf->remoteGimpCommand);
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, button,
	    _("Reset command to default"), NULL);
    gtk_table_attach(settingsTable, button, 2, 3, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(gimp_reset_clicked), gimpEntry);

    label = gtk_label_new(_("Configuration"));
    box = gtk_vbox_new(FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, label);

    page = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(box), page, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    text = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(page), text);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    confBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));

    table = GTK_TABLE(gtk_table_new(10, 10, FALSE));
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(table), FALSE, FALSE, 0);
    label = gtk_label_new(_("Save image defaults "));
    event = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event), label);
    gtk_tooltips_set_tip(data->ToolTips, event,
	    _("Save current image manipulation parameters as defaults.\nThe output parameters in this window are always saved."), NULL);
    gtk_table_attach(GTK_TABLE(table), event, 0, 2, 0, 1, 0, 0, 0, 0);
    confCombo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    gtk_combo_box_append_text(confCombo, _("Never again"));
    gtk_combo_box_append_text(confCombo, _("Always"));
    gtk_combo_box_append_text(confCombo, _("Just this once"));
    gtk_combo_box_set_active(confCombo, CFG->saveConfiguration);
    g_signal_connect(G_OBJECT(confCombo), "changed",
	    G_CALLBACK(options_combo_update), &CFG->saveConfiguration);
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(confCombo), 2, 4, 0, 1,
	    0, 0, 0, 0);

    label = gtk_label_new(_("Save full configuration "));
    event = gtk_event_box_new();
    gtk_tooltips_set_tip(data->ToolTips, event,
	    _("Save resource file ($HOME/.ufrawrc)"), NULL);
    gtk_container_add(GTK_CONTAINER(event), label);
    gtk_table_attach(GTK_TABLE(table), event, 0, 2, 1, 2, 0, 0, 0, 0);
    button = gtk_button_new_from_stock(GTK_STOCK_SAVE);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(configuration_save), NULL);
    gtk_table_attach(table, button, 2, 3, 1, 2, 0, 0, 0, 0);

    label = gtk_label_new(_("Log"));
    page = gtk_scrolled_window_new(NULL, NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(page),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    text = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(page), text);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
    char *log = ufraw_message(UFRAW_GET_LOG, NULL);
    if (log!=NULL) {
	char *utf8_log = g_filename_to_utf8(log, -1, NULL, NULL, NULL);
	if (utf8_log==NULL) utf8_log = g_strdup(_("Encoding conversion failed"));
	gtk_text_buffer_set_text(buffer, utf8_log, -1);
	g_free(utf8_log);
    }
    label = gtk_label_new(_("About"));
    box = gtk_vbox_new(FALSE, 0);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), box, label);
    image = gtk_image_new_from_stock("ufraw", GTK_ICON_SIZE_DIALOG);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label),
            "<span size='xx-large'><b>UFRaw " VERSION "</b></span>\n");
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), _(
            "The <b>U</b>nidentified <b>F</b>lying <b>Raw</b> "
            "(<b>UFRaw</b>) is a utility to\n"
            "read and manipulate raw images from digital cameras.\n"
	    "UFRaw relies on <b>D</b>igital <b>C</b>amera "
	    "<b>Raw</b> (<b>DCRaw</b>)\n"
	    "for the actual encoding of the raw images.\n\n"
            "Author: Udi Fuchs\n"
            "Homepage: http://ufraw.sourceforge.net/\n\n"));
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    while (1) {
        for (j=0; j<profile_types; j++) {
            gtk_container_foreach(GTK_CONTAINER(profileTable[j]),
                    (GtkCallback)(container_remove), profileTable[j]);
            table = GTK_TABLE(profileTable[j]);
            for (i=1; i<CFG->profileCount[j]; i++) {
                snprintf(txt, max_name, "%s (%s)",
                        CFG->profile[j][i].name,
                        CFG->profile[j][i].productName);
                label = gtk_label_new(txt);
                gtk_table_attach_defaults(table, label, 0, 1, i, i+1);
                button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
                g_object_set_data(G_OBJECT(button), "Type", (void*)j);
                g_signal_connect(G_OBJECT(button), "clicked",
                        G_CALLBACK(delete_from_list), (gpointer)i);
                gtk_table_attach(table, button, 1, 2, i, i+1, 0, 0, 0, 0);
            }
        }
        gtk_container_foreach(GTK_CONTAINER(baseCurveTable),
                (GtkCallback)(container_remove), baseCurveTable);
        table = baseCurveTable;
        for (i=camera_curve+1; i<CFG->BaseCurveCount; i++) {
            label = gtk_label_new(CFG->BaseCurve[i].name);
            gtk_table_attach_defaults(table, label, 0, 1, i, i+1);
            button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
            g_object_set_data(G_OBJECT(button), "Type",
		    (gpointer)profile_types+base_curve);
            g_signal_connect(G_OBJECT(button), "clicked",
                    G_CALLBACK(delete_from_list), (gpointer)i);
            gtk_table_attach(table, button, 1, 2, i, i+1, 0, 0, 0, 0);
        }
        gtk_container_foreach(GTK_CONTAINER(curveTable),
                (GtkCallback)(container_remove), curveTable);
        table = curveTable;
        for (i=linear_curve+1; i<CFG->curveCount; i++) {
            label = gtk_label_new(CFG->curve[i].name);
            gtk_table_attach_defaults(table, label, 0, 1, i, i+1);
            button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
            g_object_set_data(G_OBJECT(button), "Type",
		    (gpointer)profile_types+luminosity_curve);
            g_signal_connect(G_OBJECT(button), "clicked",
                    G_CALLBACK(delete_from_list), (gpointer)i);
            gtk_table_attach(table, button, 1, 2, i, i+1, 0, 0, 0, 0);
        }
        conf_save(CFG, NULL, &buf);
        gtk_text_buffer_set_text(confBuffer, buf, -1);
	g_free(buf);
        gtk_widget_show_all(optionsDialog);
        response = gtk_dialog_run(GTK_DIALOG(optionsDialog));
	/* APPLY only marks that something changed and we need to refresh */
	if (response==GTK_RESPONSE_APPLY)
	    continue;
	if ( !data->OptionsChanged ) {
	    /* If nothing changed there is nothing to do */
    	} else if ( response==GTK_RESPONSE_OK ) {
	    /* Copy profiles and curves from CFG to RC and save .ufrawrc */
	    if ( memcmp(&RC.BaseCurve[RC.BaseCurveIndex],
		    &CFG->BaseCurve[RC.BaseCurveIndex], sizeof(CurveData))!=0 )
		RC.BaseCurveIndex = CFG->BaseCurveIndex;
	    for (i=0; i<CFG->BaseCurveCount; i++)
		RC.BaseCurve[i] = CFG->BaseCurve[i];
	    RC.BaseCurveCount = CFG->BaseCurveCount;

	    if ( memcmp(&RC.curve[RC.curveIndex],
		    &CFG->curve[RC.curveIndex], sizeof(CurveData))!=0 )
		RC.curveIndex = CFG->curveIndex;
	    for (i=0; i<CFG->curveCount; i++)
		RC.curve[i] = CFG->curve[i];
	    RC.curveCount = CFG->curveCount;

	    for (i=0; i<profile_types; i++) {
		if ( memcmp(&RC.profile[i][RC.profileIndex[i]],
			&CFG->profile[i][RC.profileIndex[i]],
			sizeof(profile_data))!=0 )
		    RC.profileIndex[i] = CFG->profileIndex[i];
		for (j=0; j<CFG->profileCount[i]; j++)
		    RC.profile[i][j] = CFG->profile[i][j];
		RC.profileCount[i] = CFG->profileCount[i];
	    }
	    conf_save(&RC, NULL, NULL);
	} else { /* response==GTK_RESPONSE_CANCEL or window closed */
	    /* Copy profiles and curves from RC to CFG */

	    /* We might remove 'too much' here, but it causes no harm */
	    for (i=0; i<CFG->BaseCurveCount; i++)
		gtk_combo_box_remove_text(data->BaseCurveCombo, 0);
	    for (i=0; i<RC.BaseCurveCount; i++) {
		CFG->BaseCurve[i] = RC.BaseCurve[i];
		if ( (i==custom_curve || i==camera_curve) && !CFG_cameraCurve )
		    continue;
		if ( i<=camera_curve )
		    gtk_combo_box_append_text(data->BaseCurveCombo,
			    _(CFG->BaseCurve[i].name));
		else
		    gtk_combo_box_append_text(data->BaseCurveCombo,
			    CFG->BaseCurve[i].name);
	    }
	    CFG->BaseCurveCount = RC.BaseCurveCount;
	    CFG->BaseCurveIndex = saveBaseCurveIndex;
	    if (CFG->BaseCurveIndex>camera_curve && !CFG_cameraCurve)
		gtk_combo_box_set_active(data->BaseCurveCombo,
			CFG->BaseCurveIndex-2);
	    else
		gtk_combo_box_set_active(data->BaseCurveCombo,
			CFG->BaseCurveIndex);

	    for (i=0; i<CFG->curveCount; i++)
		gtk_combo_box_remove_text(data->CurveCombo, 0);
	    for (i=0; i<RC.curveCount; i++) {
		CFG->curve[i] = RC.curve[i];
		if ( i<=linear_curve )
		    gtk_combo_box_append_text(data->CurveCombo,
			    _(CFG->curve[i].name));
		else
		    gtk_combo_box_append_text(data->CurveCombo,
			    CFG->curve[i].name);
	    }
	    CFG->curveCount = RC.curveCount;
	    CFG->curveIndex = saveCurveIndex;
	    gtk_combo_box_set_active(data->CurveCombo, CFG->curveIndex);

	    for (j=0; j<profile_types; j++) {
		for (i=0; i<CFG->profileCount[j]; i++)
		    gtk_combo_box_remove_text(data->ProfileCombo[j], 0);
		for (i=0; i<RC.profileCount[j]; i++) {
		    CFG->profile[j][i] = RC.profile[j][i];
		    if ( i==0 )
			gtk_combo_box_append_text(data->ProfileCombo[j],
				_(CFG->profile[j][i].name));
		    else
			gtk_combo_box_append_text(data->ProfileCombo[j],
				CFG->profile[j][i].name);
		}
		CFG->profileCount[j] = RC.profileCount[j];
		CFG->profileIndex[j] = saveProfileIndex[j];
		gtk_combo_box_set_active(data->ProfileCombo[j],
			CFG->profileIndex[j]);
	    }
	}
	ufraw_focus(optionsDialog, FALSE);
        gtk_widget_destroy(optionsDialog);
	render_preview(data);
        return;
    }
}

static gboolean window_delete_event(GtkWidget *widget, GdkEvent *event,
	gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    user_data = user_data;
    event = event;
    if (data->FreezeDialog) return TRUE;
    g_object_set_data(G_OBJECT(widget), "WindowResponse",
            (gpointer)GTK_RESPONSE_CANCEL);
    gtk_main_quit();
    return TRUE;
}

static void expander_state(GtkWidget *widget, gpointer user_data)
{
    preview_data *data = get_preview_data(widget);
    const char *text;
    GtkWidget *label;
    int i;

    user_data = user_data;
    if (!GTK_IS_EXPANDER(widget)) return;
    label = gtk_expander_get_label_widget(GTK_EXPANDER(widget));
    text = gtk_label_get_text(GTK_LABEL(label));
    for (i=0; expanderText[i]!=NULL; i++)
        if (!strcmp(text, _(expanderText[i])))
            CFG->expander[i] = gtk_expander_get_expanded(GTK_EXPANDER(widget));
}

static gboolean histogram_menu(GtkMenu *menu, GdkEventButton *event)
{
    preview_data *data = get_preview_data(menu);
    if (data->FreezeDialog) return FALSE;
    if (event->button!=3) return FALSE;
    gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event->button, event->time);
    return TRUE;
}

void preview_progress(void *widget, char *text, double progress)
{
    if (widget==NULL) return;
    preview_data *data = get_preview_data(widget);
    if (data->ProgressBar==NULL) return;
    gtk_progress_bar_set_text(data->ProgressBar, text);
    gtk_progress_bar_set_fraction(data->ProgressBar, progress);
    while (gtk_events_pending()) gtk_main_iteration();
}

static void control_button_event(GtkWidget *widget, long type)
{
    preview_data *data = get_preview_data(widget);
    if (data->FreezeDialog==TRUE) return;
    data->FreezeDialog = TRUE;
    GtkWindow *window = GTK_WINDOW(gtk_widget_get_toplevel(widget));
    gtk_widget_set_sensitive(data->Controls, FALSE);
    long response = UFRAW_NO_RESPONSE;
    int status = UFRAW_SUCCESS;

    switch (type) {
    case cancel_button:
	response = GTK_RESPONSE_CANCEL;
	break;
    case delete_button:
	if ( ufraw_delete(widget, data->UF)==UFRAW_SUCCESS )
	    response = UFRAW_RESPONSE_DELETE;
	break;
    case save_as_button:
	status = ufraw_save_as(data->UF, widget);
	response = GTK_RESPONSE_OK;
	break;
    case save_button:
	status = ufraw_save_now(data->UF, widget);
	response = GTK_RESPONSE_OK;
	break;
    case gimp_button:
	if ( ufraw_send_to_gimp(data->UF)==UFRAW_SUCCESS )
	    response = GTK_RESPONSE_OK;
	break;
    case ok_button:
	status = (*data->SaveFunc)(data->UF, widget);
	response = GTK_RESPONSE_OK;
    }
    // cases that set error status require redrawing of the preview image
    if ( status!=UFRAW_SUCCESS ) {
	preview_progress(widget, "", 0);
	create_base_image(data);
    } else if ( response!=UFRAW_NO_RESPONSE ) {
	g_object_set_data(G_OBJECT(window), "WindowResponse",
		(gpointer)response);
	gtk_main_quit();
    }
    data->FreezeDialog = FALSE;
    gtk_widget_set_sensitive(data->Controls, TRUE);
}

static gboolean control_button_key_press_event(
    GtkWidget *widget, GdkEventKey *event, preview_data *data)
{
    if (data->FreezeDialog==TRUE) return FALSE;
    (void)widget;
    // Check that the Alt key is pressed
    if ( !(event->state&GDK_MOD1_MASK) ) return FALSE;
    int i;
    for (i=0; i<num_buttons; i++) {
	if ( data->ButtonMnemonic[i]==0 ) continue;
	if ( gdk_keyval_to_lower(event->keyval)==data->ButtonMnemonic[i] ) {
	    control_button_event(data->ControlButton[i], i);
	    return TRUE;
	}
    }
    return FALSE;
}

static GtkWidget *control_button(const char *stockImage, const char *tip,
    ControlButtons buttonEnum, preview_data *data)
{
    GtkWidget *button = gtk_button_new();
#if GTK_CHECK_VERSION(2,8,0)
    gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_stock(
		stockImage, GTK_ICON_SIZE_BUTTON));
#else
    GtkWidget *image = gtk_image_new_from_stock(stockImage,
	    GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(button), image);
    gtk_widget_show(image);
#endif
    g_signal_connect(G_OBJECT(button), "clicked",
	    G_CALLBACK(control_button_event), (gpointer)buttonEnum);
    char **tipParts = g_strsplit(tip, "_", 2);
    if ( tipParts[0]==NULL || tipParts[1]==NULL ) {
	// No mnemonic
	gtk_tooltips_set_tip(data->ToolTips, button, tip, NULL);
	return button;
    }
    char *tooltip = g_strdup_printf(_("%s%s (Alt-%c)"),
	    tipParts[0], tipParts[1], tipParts[1][0]);
    gtk_tooltips_set_tip(data->ToolTips, button, tooltip, NULL);
    g_free(tooltip);
    data->ButtonMnemonic[buttonEnum] = gdk_keyval_to_lower(
	    gdk_unicode_to_keyval(tipParts[1][0]));
    data->ControlButton[buttonEnum] = button;
    g_strfreev(tipParts);
    return button;
}

static void notebook_switch_page(GtkNotebook *notebook, GtkNotebookPage *page,
	guint page_num, gpointer user_data)
{
    (void)page;
    (void)user_data;
    preview_data *data = get_preview_data(notebook);
    if (data->FreezeDialog==TRUE) return;

    GtkWidget *event_box =
	    gtk_widget_get_ancestor(data->PreviewWidget, GTK_TYPE_EVENT_BOX);
#ifdef HAVE_GTKIMAGEVIEW
    if ( page_num==0 ) {
	gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), TRUE);
	gdk_window_set_cursor(event_box->window, data->Cursor[spot_cursor]);
	draw_spot(data, TRUE);
    } else if ( page_num==4 ) {
	gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), TRUE);
	gdk_window_set_cursor(event_box->window, data->Cursor[crop_cursor]);
	draw_spot(data, FALSE);
    } else {
	gtk_event_box_set_above_child(GTK_EVENT_BOX(event_box), FALSE);
	draw_spot(data, TRUE);
    }
#else
    if ( page_num==4 ) {
	gdk_window_set_cursor(event_box->window, data->Cursor[crop_cursor]);
	draw_spot(data, FALSE);
    } else {
	gdk_window_set_cursor(event_box->window, data->Cursor[spot_cursor]);
	draw_spot(data, TRUE);
    }
#endif
    data->PageNum = page_num;
}

static void list_store_add(GtkListStore *store, char *name, char *var)
{
    if ( var!=NULL && strlen(var)>0 )
    {
	GtkTreeIter iter;
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, name, 1, var, -1);
    }
}

int ufraw_preview(ufraw_data *uf, int plugin, long (*save_func)())
{
    GtkWidget *previewWindow, *previewVBox;
    GtkTable *table, *subTable;
    GtkBox *previewHBox, *box, *hbox;
    GtkComboBox *combo;
    GtkWidget *button, *saveButton, *saveAsButton=NULL, *event_box, *align,
	    *label, *vBox, *page, *menu, *menu_item, *frame, *entry;
    GSList *group;
    GdkPixbuf *pixbuf;
    GdkRectangle screen;
    int max_preview_width, max_preview_height;
    int preview_width, preview_height, i, c;
    long j;
    int status, rowstride, curveeditorHeight;
    guint8 *pixies;
    preview_data PreviewData;
    preview_data *data = &PreviewData;

    data->UF = uf;
    data->SaveFunc = save_func;

    data->SaveConfig = *uf->conf;
    data->SpotX1 = -1;
    data->SpotX2 = -1;
    data->SpotY1 = -1;
    data->SpotY2 = -1;
    data->SpotDraw = FALSE;
    data->FreezeDialog = TRUE;
    data->PageNum = 0;
    data->DrawnCropX1 = 0;
    data->DrawnCropX2 = 99999;
    data->DrawnCropY1 = 0;
    data->DrawnCropY2 = 99999;

    data->AspectRatio = 0.0;
    data->LockAspect = FALSE;
    data->BlinkTimer = 0;
    for (i=0; i<num_buttons; i++) {
	data->ControlButton[i] = NULL;
	data->ButtonMnemonic[i] = 0;
    }
    data->ToolTips = gtk_tooltips_new();
#if GTK_CHECK_VERSION(2,10,0)
    g_object_ref_sink(GTK_OBJECT(data->ToolTips));
#else
    g_object_ref(data->ToolTips);
    gtk_object_sink(GTK_OBJECT(data->ToolTips));
#endif
    previewWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    char *utf8_filename = g_filename_to_utf8(uf->filename,
	    -1, NULL, NULL, NULL);
    if (utf8_filename==NULL) utf8_filename = g_strdup("Unknown file name");
    char *previewHeader = g_strdup_printf(_("%s - UFRaw Photo Loader"),
            utf8_filename);
    gtk_window_set_title(GTK_WINDOW(previewWindow), previewHeader);
    g_free(previewHeader);
    g_free(utf8_filename);

    ufraw_icons_init();
#if GTK_CHECK_VERSION(2,6,0)
    gtk_window_set_icon_name(GTK_WINDOW(previewWindow), "ufraw");
#else
    gtk_window_set_icon(GTK_WINDOW(previewWindow),
	    gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
		    "ufraw", 48, GTK_ICON_LOOKUP_USE_BUILTIN, NULL));
#endif
#ifndef HAVE_GTKIMAGEVIEW
    gtk_window_set_resizable(GTK_WINDOW(previewWindow), FALSE);
#endif
    g_signal_connect(G_OBJECT(previewWindow), "delete-event",
            G_CALLBACK(window_delete_event), NULL);
    g_signal_connect(G_OBJECT(previewWindow), "map-event",
		     G_CALLBACK(window_map_event), NULL);
    g_signal_connect(G_OBJECT(previewWindow), "unmap-event",
		     G_CALLBACK(window_unmap_event), NULL);
    g_signal_connect(G_OBJECT(previewWindow), "key-press-event",
		     G_CALLBACK(control_button_key_press_event), data);
    g_object_set_data(G_OBJECT(previewWindow), "Preview-Data", data);
    ufraw_focus(previewWindow, TRUE);
    uf->widget = previewWindow;

    gdk_screen_get_monitor_geometry(gdk_screen_get_default(), 0, &screen);
    max_preview_width = MIN(def_preview_width, screen.width-416);
    max_preview_height = MIN(def_preview_height, screen.height-120);
    CFG->Scale = MAX((uf->initialWidth-1)/max_preview_width,
            (uf->initialHeight-1)/max_preview_height)+1;
    CFG->Scale = MAX(2, CFG->Scale);
    CFG->Zoom = 100.0 / CFG->Scale;
    preview_width = uf->initialWidth / CFG->Scale;
    preview_height = uf->initialHeight / CFG->Scale;
    /* With the following guesses the window usually fits into the screen.
     * There should be more intelegent settings to widget sizes. */
    curveeditorHeight = 256;
    if (screen.height<801) {
	if (CFG->liveHistogramHeight==conf_default.liveHistogramHeight)
	    CFG->liveHistogramHeight = 96;
	if (CFG->rawHistogramHeight==conf_default.rawHistogramHeight)
	    CFG->rawHistogramHeight = 96;
	if (screen.height<800) curveeditorHeight = 192;
    }
    previewHBox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_container_add(GTK_CONTAINER(previewWindow), GTK_WIDGET(previewHBox));
    previewVBox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(previewHBox, previewVBox, FALSE, FALSE, 2);

    table = GTK_TABLE(table_with_frame(previewVBox,
	    _(expanderText[raw_expander]), CFG->expander[raw_expander]));
    event_box = gtk_event_box_new();
    gtk_table_attach(table, event_box, 0, 1, 1, 2, GTK_EXPAND, 0, 0, 0);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
            raw_his_size+2, CFG->rawHistogramHeight+2);
    data->RawHisto = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    gtk_container_add(GTK_CONTAINER(event_box), data->RawHisto);
    pixies = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    memset(pixies, 0, (gdk_pixbuf_get_height(pixbuf)-1)* rowstride +
            gdk_pixbuf_get_width(pixbuf)*gdk_pixbuf_get_n_channels(pixbuf));
    menu = gtk_menu_new();
    g_object_set_data(G_OBJECT(menu), "Parent-Widget", event_box);
    g_signal_connect_swapped(G_OBJECT(event_box), "button_press_event",
            G_CALLBACK(histogram_menu), menu);
    group = NULL;
    menu_item = gtk_radio_menu_item_new_with_label(group, _("Linear"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 6, 7);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->rawHistogramScale==linear_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)linear_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->rawHistogramScale);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("Logarithmic"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 7, 8);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->rawHistogramScale==log_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)log_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->rawHistogramScale);

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 8, 9);

    group = NULL;
    menu_item = gtk_radio_menu_item_new_with_label(group, _("96 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 9, 10);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->rawHistogramHeight==96);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)96);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->rawHistogramHeight);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("128 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 10, 11);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->rawHistogramHeight==128);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)128);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->rawHistogramHeight);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("192 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 11, 12);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->rawHistogramHeight==192);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)192);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->rawHistogramHeight);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("256 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 12, 13);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->rawHistogramHeight==256);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)256);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->rawHistogramHeight);
    gtk_widget_show_all(menu);

    // Spot values:
    data->SpotTable = GTK_TABLE(table_with_frame(previewVBox, NULL, FALSE));
    data->SpotLabels = color_labels_new(data->SpotTable, 0, 0,
	    _("Spot values:"), pixel_format, with_zone, data);
    data->SpotPatch = GTK_LABEL(gtk_label_new(NULL));
    gtk_table_attach_defaults(data->SpotTable, GTK_WIDGET(data->SpotPatch),
	    6, 7, 0, 1);

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_container_add(GTK_CONTAINER(button),
	    gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU));
    gtk_table_attach(data->SpotTable, button, 7, 8, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
	    G_CALLBACK(close_spot), NULL);

    // Exposure:
    table = GTK_TABLE(table_with_frame(previewVBox, NULL, FALSE));
    data->ExposureAdjustment = adjustment_scale(table, 0, 0, "exposure",
            CFG->exposure, &CFG->exposure,
            -3, 3, 0.01, 1.0/6, 2, _("Exposure compensation in EV"));

    button = gtk_toggle_button_new();
    restore_details_button_set(GTK_BUTTON(button), data);
    gtk_table_attach(table, button, 7, 8, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "toggled",
            G_CALLBACK(toggle_button_update), &CFG->restoreDetails);

    button = gtk_toggle_button_new();
    clip_highlights_button_set(GTK_BUTTON(button), data);
    gtk_table_attach(table, button, 8, 9, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "toggled",
            G_CALLBACK(toggle_button_update), &CFG->clipHighlights);

    data->AutoExposureButton = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_container_add(GTK_CONTAINER(data->AutoExposureButton),
	    gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(data->AutoExposureButton),
	    _("Auto adjust exposure"), NULL);
    gtk_table_attach(table, GTK_WIDGET(data->AutoExposureButton), 9, 10, 0, 1,
	    0, 0, 0, 0);
    gtk_toggle_button_set_active(data->AutoExposureButton, CFG->autoExposure);
    g_signal_connect(G_OBJECT(data->AutoExposureButton), "clicked",
            G_CALLBACK(button_update), NULL);

    data->ResetExposureButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetExposureButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, data->ResetExposureButton,
	    _("Reset exposure to default"), NULL);
    gtk_table_attach(table, data->ResetExposureButton, 10, 11, 0, 1, 0,0,0,0);
    g_signal_connect(G_OBJECT(data->ResetExposureButton), "clicked",
            G_CALLBACK(button_update), NULL);

    GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());
    g_signal_connect(G_OBJECT(notebook), "switch-page",
            G_CALLBACK(notebook_switch_page), NULL);
    data->Controls = GTK_WIDGET(notebook);
    gtk_box_pack_start(GTK_BOX(previewVBox), GTK_WIDGET(notebook),
	    FALSE, FALSE, 0);

    /* Start of White Balance setting page */
    page = notebook_page_new(notebook, _("White balance"),
	    "white-balance", data->ToolTips);
    /* Set this page to be the opening page. */
    int openingPage = gtk_notebook_page_num(notebook, page);

    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    subTable = GTK_TABLE(gtk_table_new(10, 1, FALSE));
    gtk_table_attach(table, GTK_WIDGET(subTable), 0, 1, 0 , 1,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);
//    label = gtk_label_new("White balance");
//    gtk_table_attach(subTable, label, 0, 1, 0 , 1, 0, 0, 0, 0);
    data->WBCombo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    data->WBPresets = NULL;
    const wb_data *lastPreset = NULL;
    gboolean make_model_match = FALSE, make_model_fine_tuning = FALSE;
    char model[max_name];
    if ( strcmp(uf->conf->make,"MINOLTA")==0 &&
            ( strncmp(uf->conf->model, "ALPHA", 5)==0 ||
              strncmp(uf->conf->model, "MAXXUM", 6)==0 ) ) {
        /* Canonize Minolta model names (copied from dcraw) */
        g_snprintf(model, max_name, "DYNAX %s",
        uf->conf->model+6+(uf->conf->model[0]=='M'));
    } else {
        g_strlcpy(model, uf->conf->model, max_name);
    }
    for (i=0; i<wb_preset_count; i++) {
        if (strcmp(wb_preset[i].make, "")==0) {
	    /* Common presets */
	    data->WBPresets = g_list_append(data->WBPresets,
		    (void*)wb_preset[i].name);
	    gtk_combo_box_append_text(data->WBCombo, _(wb_preset[i].name));
        } else if ( (strcmp(wb_preset[i].make, uf->conf->make)==0 ) &&
                    (strcmp(wb_preset[i].model, model)==0)) {
	    /* Camera specific presets */
            make_model_match = TRUE;
	    if ( lastPreset==NULL ||
		strcmp(wb_preset[i].name,lastPreset->name)!=0 ) {
		data->WBPresets = g_list_append(data->WBPresets,
			(void*)wb_preset[i].name);
		gtk_combo_box_append_text(data->WBCombo, _(wb_preset[i].name));
	    } else {
		/* Fine tuning presets */
		make_model_fine_tuning = TRUE;
	    }
	    lastPreset = &wb_preset[i];
        }
    }
    GList *l;
    gtk_combo_box_set_active(data->WBCombo, 0);
    for (i=0, l=data->WBPresets; g_list_next(l)!=NULL; i++, l=g_list_next(l))
	if (!strcmp(CFG->wb, l->data))
	    gtk_combo_box_set_active(data->WBCombo, i);
    g_signal_connect(G_OBJECT(data->WBCombo), "changed",
            G_CALLBACK(combo_update), CFG->wb);
    event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(event_box), GTK_WIDGET(data->WBCombo));
    gtk_tooltips_set_tip(data->ToolTips, event_box, _("White Balance"), NULL);
    if ( make_model_fine_tuning || !make_model_match)
	gtk_table_attach(subTable, event_box, 0, 6, 0, 1, GTK_FILL, 0, 0, 0);
    else
	gtk_table_attach(subTable, event_box, 0, 7, 0, 1, GTK_FILL, 0, 0, 0);

    data->WBTuningAdjustment = NULL;
    if (make_model_fine_tuning) {
	data->WBTuningAdjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
		CFG->WBTuning, -9, 9, 1, 1, 0));
	g_object_set_data(G_OBJECT(data->WBTuningAdjustment),
		"Adjustment-Accuracy",(gpointer)0);
	button = gtk_spin_button_new(data->WBTuningAdjustment, 1, 0);
    	g_object_set_data(G_OBJECT(data->WBTuningAdjustment),
		"Parent-Widget", button);
	gtk_table_attach(subTable, button, 6, 7, 0, 1, GTK_SHRINK, 0, 0, 0);
	g_signal_connect(G_OBJECT(data->WBTuningAdjustment), "value-changed",
		G_CALLBACK(adjustment_update), &CFG->WBTuning);
    } else if (!make_model_match) {
	event_box = gtk_event_box_new();
	label = gtk_image_new_from_stock(GTK_STOCK_DIALOG_WARNING,
		GTK_ICON_SIZE_BUTTON);
	gtk_container_add(GTK_CONTAINER(event_box), label);
	gtk_table_attach(subTable, event_box, 6, 7, 0, 1, GTK_FILL, 0, 0, 0);
	gtk_tooltips_set_tip(data->ToolTips, event_box,
	    _("There are no white balance presets for your camera model.\nCheck UFRaw's webpage for information on how to get your\ncamera supported."), NULL);
    }
    data->ResetWBButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetWBButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, data->ResetWBButton,
	    _("Reset white balance to initial value"), NULL);
    gtk_table_attach(subTable, data->ResetWBButton, 7, 8, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->ResetWBButton), "clicked",
            G_CALLBACK(button_update), NULL);

    data->TemperatureAdjustment = adjustment_scale(subTable, 0, 1,
            _("Temperature"), CFG->temperature, &CFG->temperature,
            2000, 12000, 50, 200, 0,
            _("White balance color temperature (K)"));
    data->GreenAdjustment = adjustment_scale(subTable, 0, 2, _("Green"),
            CFG->green, &CFG->green, 0.2, 2.5, 0.010, 0.050, 3,
            _("Green component"));
    // Spot WB button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_COLOR_PICKER, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, button,
	    _("Select a spot on the preview image to apply spot white balance"),
	    NULL);
    gtk_table_attach(subTable, button, 7, 8, 1, 3, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(spot_wb_event), NULL);

    GtkBox *subbox = GTK_BOX(gtk_hbox_new(0,0));
    gtk_table_attach(table, GTK_WIDGET(subbox), 0, 1, 1, 2, 0, 0, 0, 0);
    label = gtk_label_new(_("Chan. multipliers:"));
    gtk_box_pack_start(subbox, label, 0, 0, 0);
    for (i=0; i<data->UF->colors; i++) {
	data->ChannelAdjustment[i] = GTK_ADJUSTMENT(gtk_adjustment_new(
		CFG->chanMul[i], 0.5, 9.0, 0.001, 0.001, 0));
	g_object_set_data(G_OBJECT(data->ChannelAdjustment[i]),
		"Adjustment-Accuracy",(gpointer)3);
	button = gtk_spin_button_new(data->ChannelAdjustment[i], 0.001, 3);
    	g_object_set_data(G_OBJECT(data->ChannelAdjustment[i]),
		"Parent-Widget", button);
	gtk_box_pack_start(subbox, button, 0, 0, 0);
	g_signal_connect(G_OBJECT(data->ChannelAdjustment[i]), "value-changed",
		G_CALLBACK(adjustment_update), &CFG->chanMul[i]);
    }
    /* Interpolation is temporeraly in the WB page */
    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
//    box = GTK_BOX(gtk_hbox_new(FALSE, 6));
//    gtk_table_attach(table, GTK_WIDGET(box), 0, 1, 0, 1,
//	    GTK_EXPAND|GTK_FILL, 0, 0, 0);
    event_box = gtk_event_box_new();
    GtkWidget *icon = gtk_image_new_from_stock("interpolation",
	    GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_container_add(GTK_CONTAINER(event_box), icon);
    gtk_tooltips_set_tip(data->ToolTips, event_box,
	    _("Bayer pattern interpolation"), NULL);
    gtk_table_attach(table, event_box, 0, 1, 0, 1, 0, 0, 0, 0);
    combo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    data->Interpolation = 0;
    if ( data->UF->HaveFilters ) {
	int i = 0;
	if ( data->UF->colors==4 ) {
	    gtk_combo_box_append_text(combo, _("VNG four color interpolation"));
	    data->InterpolationTable[i++] = four_color_interpolation;
	    gtk_combo_box_append_text(combo, _("Bilinear interpolation"));
	    data->InterpolationTable[i++] = bilinear_interpolation;
	} else {
	    gtk_combo_box_append_text(combo, _("EAHD interpolation"));
	    data->InterpolationTable[i++] = eahd_interpolation;
	    gtk_combo_box_append_text(combo, _("AHD interpolation"));
	    data->InterpolationTable[i++] = ahd_interpolation;
	    gtk_combo_box_append_text(combo, _("VNG interpolation"));
	    data->InterpolationTable[i++] = vng_interpolation;
	    gtk_combo_box_append_text(combo, _("VNG four color interpolation"));
	    data->InterpolationTable[i++] = four_color_interpolation;
	    gtk_combo_box_append_text(combo, _("PPG interpolation"));
	    data->InterpolationTable[i++] = ppg_interpolation;
	    gtk_combo_box_append_text(combo, _("Bilinear interpolation"));
	    data->InterpolationTable[i++] = bilinear_interpolation;
	}
	if ( plugin==1 ) {
	    gtk_combo_box_append_text(combo, _("Half-size interpolation"));
	    data->InterpolationTable[i++] = half_interpolation;
	}
	for (i--; i>=0; i--)
	    if ( data->InterpolationTable[i]==CFG->interpolation )
		data->Interpolation = i;
	/* Make sure that CFG->interpolation is set to a valid one */
	CFG->interpolation = data->InterpolationTable[data->Interpolation];
	gtk_combo_box_set_active(combo, data->Interpolation);
	g_signal_connect(G_OBJECT(combo), "changed",
		G_CALLBACK(combo_update), &data->Interpolation);
    } else {
	gtk_combo_box_append_text(combo, _("No Bayer pattern"));
	gtk_combo_box_set_active(combo, 0);
	gtk_widget_set_sensitive(GTK_WIDGET(combo), FALSE);
    }
    gtk_table_attach(table, GTK_WIDGET(combo), 1, 2, 0, 1,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);
//    gtk_box_pack_start(box, GTK_WIDGET(combo), FALSE, FALSE, 0);

    /* Denoising is temporeraly in the WB page */
    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    data->ThresholdAdjustment = adjustment_scale(table, 0, 0, _("Threshold"),
	    CFG->threshold, &CFG->threshold, 0.0, 1000.0, 10, 50, 0,
	    _("Threshold for wavelet denoising"));
    data->ResetThresholdButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetThresholdButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, data->ResetThresholdButton,
	    _("Reset denoise threshold to default"), NULL);
    gtk_table_attach(table, data->ResetThresholdButton, 7, 8, 0, 1, 0,0,0,0);
    g_signal_connect(G_OBJECT(data->ResetThresholdButton), "clicked",
            G_CALLBACK(button_update), NULL);

    /* Without GtkImageView, zoom controls cannot be bellow the image because
     * if the image is zoomed in too much the controls will be out of
     * the screen and it would be impossible to zoom out again. */
#ifndef HAVE_GTKIMAGEVIEW
    // Zoom controls:
    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    label = gtk_label_new(_("Zoom:"));
    gtk_table_attach(table, label, 0, 1, 0, 1, 0, 0, 0, 0);

    // Zoom out button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_ZOOM_OUT, GTK_ICON_SIZE_BUTTON));
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(zoom_out_event), NULL);
    gtk_table_attach(table, button, 1, 2, 0, 1, 0, 0, 0, 0);

    // Zoom percentage spin button:
    data->ZoomAdjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
		CFG->Zoom, 5, 50, 1, 1, 0));
    g_object_set_data(G_OBJECT(data->ZoomAdjustment),
		"Adjustment-Accuracy", (gpointer)0);
    button = gtk_spin_button_new(data->ZoomAdjustment, 1, 0);
    g_object_set_data(G_OBJECT(data->ZoomAdjustment), "Parent-Widget", button);
    g_signal_connect(G_OBJECT(data->ZoomAdjustment), "value-changed",
		G_CALLBACK(adjustment_update), &CFG->Zoom);
    gtk_tooltips_set_tip(data->ToolTips, button,
	    _("Zoom percentage"), NULL);
    gtk_table_attach(table, button, 2, 3, 0, 1, 0, 0, 0, 0);

    // Zoom in button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_ZOOM_IN, GTK_ICON_SIZE_BUTTON));
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(zoom_in_event), NULL);
    gtk_table_attach(table, button, 3, 4, 0, 1, 0, 0, 0, 0);
#endif // !HAVE_GTKIMAGEVIEW

    // Dark frame controls:
    box = GTK_BOX(gtk_hbox_new(FALSE, 0));
    frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(page), frame, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(box));

    label = gtk_label_new(_("Dark Frame:"));
    gtk_box_pack_start(box, label, FALSE, FALSE, 0);

    label = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start(box, label, TRUE, TRUE, 6);
    data->DarkFrameLabel = GTK_LABEL(label);
    set_darkframe_label(data);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
            GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON));
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);
    gtk_tooltips_set_tip(data->ToolTips, button, _("Load dark frame"), NULL);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(load_darkframe), NULL);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
            GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);
    gtk_tooltips_set_tip(data->ToolTips, button, _("Reset dark frame"), NULL);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(reset_darkframe), NULL);

    /* End of White Balance setting page */

    /* Start of Base Curve page */
    page = notebook_page_new(notebook, _("Base curve"),
	    "base-curve", data->ToolTips);

    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    box = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(box), 0, 9, 0, 1,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);

    data->BaseCurveCombo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    /* Fill in the curve names, skipping custom and camera curves if there is
     * no cameraCurve. This will make some mess later with the counting */
    for (i=0; i<CFG->BaseCurveCount; i++) {
	if ( (i==custom_curve || i==camera_curve) && !CFG_cameraCurve )
	    continue;
	if ( i<=camera_curve )
	    gtk_combo_box_append_text(data->BaseCurveCombo,
		    _(CFG->BaseCurve[i].name));
	else
	    gtk_combo_box_append_text(data->BaseCurveCombo,
		    CFG->BaseCurve[i].name);
    }
    /* This is part of the mess with the combo_box counting */
    if (CFG->BaseCurveIndex>camera_curve && !CFG_cameraCurve)
        gtk_combo_box_set_active(data->BaseCurveCombo, CFG->BaseCurveIndex-2);
    else
        gtk_combo_box_set_active(data->BaseCurveCombo, CFG->BaseCurveIndex);
    g_signal_connect(G_OBJECT(data->BaseCurveCombo), "changed",
            G_CALLBACK(combo_update), &CFG->BaseCurveIndex);
    gtk_box_pack_start(box, GTK_WIDGET(data->BaseCurveCombo), TRUE, TRUE, 0);
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
            GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON));
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);
    gtk_tooltips_set_tip(data->ToolTips, button, _("Load base curve"), NULL);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(load_curve), (gpointer)base_curve);
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
            GTK_STOCK_SAVE_AS, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, button, _("Save base curve"), NULL);
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(save_curve), (gpointer)base_curve);

    box = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(box), 0, 9, 1, 2,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);
    data->BaseCurveWidget = curveeditor_widget_new(curveeditorHeight, 256,
	    curve_update, (gpointer)base_curve);
    curveeditor_widget_set_curve(data->BaseCurveWidget,
	    &CFG->BaseCurve[CFG->BaseCurveIndex]);
    align = gtk_alignment_new(1, 0, 0, 1);
    gtk_container_add(GTK_CONTAINER(align), GTK_WIDGET(data->BaseCurveWidget));
    gtk_box_pack_start(box, align, TRUE, TRUE, 0);

    data->ResetBaseCurveButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetBaseCurveButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(data->ResetBaseCurveButton),
	    _("Reset base curve to default"),NULL);
    align = gtk_alignment_new(0, 1, 1, 0);
    gtk_container_add(GTK_CONTAINER(align),
	    GTK_WIDGET(data->ResetBaseCurveButton));
    gtk_box_pack_start(box, align, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(data->ResetBaseCurveButton), "clicked",
            G_CALLBACK(button_update), NULL);
    /* End of Base Curve page */

    /* Start of Color management page */
    page = notebook_page_new(notebook, _("Color management"),
	    "color-management", data->ToolTips);

    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    for (j=0; j<profile_types; j++) {
	event_box = gtk_event_box_new();
	icon = gtk_image_new_from_stock(
		j==in_profile ? "icc-profile-camera" :
                j==out_profile ? "icc-profile-output" :
                j==display_profile ? "icc-profile-display" : "error",
		GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(event_box), icon);
	gtk_tooltips_set_tip(data->ToolTips, event_box,
		j==in_profile ? _("Input ICC profile") :
                j==out_profile ? _("Output ICC profile") :
                j==display_profile ? _("Display ICC profile") : "Error", NULL);
        gtk_table_attach(table, event_box, 0, 1, 4*j+1, 4*j+2, 0, 0, 0, 0);
        data->ProfileCombo[j] = GTK_COMBO_BOX(gtk_combo_box_new_text());
        for (i=0; i<CFG->profileCount[j]; i++)
            if ( i==0 )
		gtk_combo_box_append_text(data->ProfileCombo[j],
			_(CFG->profile[j][i].name));
	    else
		gtk_combo_box_append_text(data->ProfileCombo[j],
			CFG->profile[j][i].name);
        gtk_combo_box_set_active(data->ProfileCombo[j], CFG->profileIndex[j]);
        g_signal_connect(G_OBJECT(data->ProfileCombo[j]), "changed",
                G_CALLBACK(combo_update), &CFG->profileIndex[j]);
        gtk_table_attach(table, GTK_WIDGET(data->ProfileCombo[j]),
                1, 8, 4*j+1, 4*j+2, GTK_FILL, GTK_FILL, 0, 0);
        button = gtk_button_new();
        gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON));
        gtk_table_attach(table, button, 8, 9, 4*j+1, 4*j+2,
                GTK_SHRINK, GTK_FILL, 0, 0);
        g_signal_connect(G_OBJECT(button), "clicked",
                G_CALLBACK(load_profile), (void *)j);
    }
    data->UseMatrixButton = gtk_check_button_new_with_label(
	    _("Use color matrix"));
    align = gtk_alignment_new(0, 0, 0, 0);
    gtk_container_add(GTK_CONTAINER(align), data->UseMatrixButton);
    gtk_table_attach(table, align, 1, 8, 2, 3, GTK_EXPAND|GTK_FILL, 0, 0, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->UseMatrixButton),
	    data->UF->useMatrix);
    g_signal_connect(G_OBJECT(data->UseMatrixButton), "toggled",
            G_CALLBACK(toggle_button_update), &data->UF->useMatrix);
    if (data->UF->raw_color || data->UF->colors==4)
	gtk_widget_set_sensitive(data->UseMatrixButton, FALSE);

    data->GammaAdjustment = adjustment_scale(table, 1, 3, _("Gamma"),
            CFG->profile[0][CFG->profileIndex[0]].gamma,
            &CFG->profile[0][0].gamma, 0.1, 1.0, 0.01, 0.05, 2,
            _("Gamma correction for the input profile"));
    data->ResetGammaButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetGammaButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, data->ResetGammaButton,
	    _("Reset gamma to default"), NULL);
    gtk_table_attach(table, data->ResetGammaButton, 8, 9, 3, 4, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->ResetGammaButton), "clicked",
            G_CALLBACK(button_update), NULL);

    data->LinearAdjustment = adjustment_scale(table, 1, 4, _("Linearity"),
            CFG->profile[0][CFG->profileIndex[0]].linear,
            &CFG->profile[0][0].linear, 0.0, 1.0, 0.01, 0.05, 2,
            _("Linear part of the gamma correction"));
    data->ResetLinearButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetLinearButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, data->ResetLinearButton,
	    _("Reset linearity to default"), NULL);
    gtk_table_attach(table, data->ResetLinearButton, 8, 9, 4, 5, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->ResetLinearButton), "clicked",
            G_CALLBACK(button_update), NULL);

    label = gtk_label_new(_("Output intent"));
    gtk_table_attach(table, label, 0, 3, 6, 7, 0, 0, 0, 0);
    combo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    gtk_combo_box_append_text(combo, _("Perceptual"));
    gtk_combo_box_append_text(combo, _("Relative colorimetric"));
    gtk_combo_box_append_text(combo, _("Saturation"));
    gtk_combo_box_append_text(combo, _("Absolute colorimetric"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), CFG->intent[out_profile]);
    g_signal_connect(G_OBJECT(combo), "changed",
            G_CALLBACK(combo_update), &CFG->intent[out_profile]);
    gtk_table_attach(table, GTK_WIDGET(combo), 3, 8, 6, 7, GTK_FILL, 0, 0, 0);

    label = gtk_label_new(_("Display intent"));
    gtk_table_attach(table, label, 0, 3, 10, 11, 0, 0, 0, 0);
    combo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    gtk_combo_box_append_text(combo, _("Perceptual"));
    gtk_combo_box_append_text(combo, _("Relative colorimetric"));
    gtk_combo_box_append_text(combo, _("Saturation"));
    gtk_combo_box_append_text(combo, _("Absolute colorimetric"));
    gtk_combo_box_append_text(combo, _("Disable soft proofing"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo),CFG->intent[display_profile]);
    g_signal_connect(G_OBJECT(combo), "changed",
            G_CALLBACK(combo_update), &CFG->intent[display_profile]);
    gtk_table_attach(table, GTK_WIDGET(combo), 3, 8, 10, 11, GTK_FILL, 0, 0, 0);
    /* End of Color management page */

    /* Start of Corrections page */
    page = notebook_page_new(notebook, _("Correct luminosity, saturation"),
	    "color-corrections", data->ToolTips);

    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    data->SaturationAdjustment = adjustment_scale(table, 0, 1, _("Saturation"),
            CFG->saturation, &CFG->saturation,
            0.0, 3.0, 0.01, 0.1, 2, _("Saturation"));
    data->ResetSaturationButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetSaturationButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, data->ResetSaturationButton,
	    _("Reset saturation to default"), NULL);
    gtk_table_attach(table, data->ResetSaturationButton, 9, 10, 1, 2,
	    0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->ResetSaturationButton), "clicked",
            G_CALLBACK(button_update), NULL);

    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    box = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_table_attach(table, GTK_WIDGET(box), 0, 9, 0, 1,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);
    data->CurveCombo = GTK_COMBO_BOX(gtk_combo_box_new_text());
    /* Fill in the curve names */
    for (i=0; i<CFG->curveCount; i++)
	if ( i<=linear_curve )
	    gtk_combo_box_append_text(data->CurveCombo, _(CFG->curve[i].name));
	else
	    gtk_combo_box_append_text(data->CurveCombo, CFG->curve[i].name);
    gtk_combo_box_set_active(data->CurveCombo, CFG->curveIndex);
    g_signal_connect(G_OBJECT(data->CurveCombo), "changed",
            G_CALLBACK(combo_update), &CFG->curveIndex);
    gtk_box_pack_start(box, GTK_WIDGET(data->CurveCombo), TRUE, TRUE, 0);
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
            GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON));
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);
    gtk_tooltips_set_tip(data->ToolTips, button, _("Load curve"), NULL);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(load_curve), (gpointer)luminosity_curve);
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
            GTK_STOCK_SAVE_AS, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, button, _("Save curve"), NULL);
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(save_curve), (gpointer)luminosity_curve);

    subTable = GTK_TABLE(gtk_table_new(9, 9, FALSE));
    gtk_table_attach(table, GTK_WIDGET(subTable), 0, 1, 1 , 2,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);
    data->CurveWidget = curveeditor_widget_new(curveeditorHeight, 256,
	    curve_update, (gpointer)luminosity_curve);
    curveeditor_widget_set_curve(data->CurveWidget,
	    &CFG->curve[CFG->curveIndex]);
    gtk_table_attach(subTable, data->CurveWidget, 1, 8, 1, 8,
	    GTK_EXPAND|GTK_FILL, 0, 0, 0);

    data->AutoCurveButton = GTK_BUTTON(gtk_button_new());
    gtk_container_add(GTK_CONTAINER(data->AutoCurveButton),
	    gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(data->AutoCurveButton),
	    _("Auto adjust curve\n(Flatten histogram)"), NULL);
    gtk_table_attach(subTable, GTK_WIDGET(data->AutoCurveButton), 8, 9, 6, 7,
	    0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->AutoCurveButton), "clicked",
            G_CALLBACK(button_update), NULL);

    data->ResetCurveButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetCurveButton),
	    gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(data->ResetCurveButton),
	    _("Reset curve to default"),NULL);
    gtk_table_attach(subTable, GTK_WIDGET(data->ResetCurveButton), 8, 9, 7, 8,
		0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->ResetCurveButton), "clicked",
            G_CALLBACK(button_update), NULL);

    data->BlackLabel = gtk_label_new(_("Black point: 0.000"));
#if GTK_CHECK_VERSION(2,6,0)
    gtk_misc_set_alignment(GTK_MISC(data->BlackLabel), 0.5, 1.0);
    gtk_label_set_angle(GTK_LABEL(data->BlackLabel), 90);
    gtk_table_attach(subTable, data->BlackLabel, 0, 1, 5, 6,
	    0, GTK_FILL|GTK_EXPAND, 0, 0);
#else
    button = gtk_alignment_new(0, 0, 0, 0);
    gtk_table_attach(subTable, button, 0, 1, 5, 6,
	    0, GTK_FILL|GTK_EXPAND, 0, 0);
    gtk_misc_set_alignment(GTK_MISC(data->BlackLabel), 0.0, 0.5);
    gtk_table_attach(subTable, data->BlackLabel, 1, 8, 8, 9, GTK_FILL, 0, 0, 0);
#endif
    data->ResetBlackButton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(data->ResetBlackButton),
	gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(data->ResetBlackButton),
	    _("Reset black-point to default"),NULL);
    gtk_table_attach(subTable, GTK_WIDGET(data->ResetBlackButton), 0, 1, 7, 8,
	    0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->ResetBlackButton), "clicked",
            G_CALLBACK(button_update), NULL);

    data->AutoBlackButton = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_container_add(GTK_CONTAINER(data->AutoBlackButton),
	    gtk_image_new_from_stock(GTK_STOCK_EXECUTE, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(data->AutoBlackButton),
	    _("Auto adjust black-point"), NULL);
    gtk_table_attach(subTable, GTK_WIDGET(data->AutoBlackButton), 0, 1, 6, 7,
	    0, GTK_SHRINK, 0, 0);
    gtk_toggle_button_set_active(data->AutoBlackButton, CFG->autoBlack);
    g_signal_connect(G_OBJECT(data->AutoBlackButton), "clicked",
            G_CALLBACK(button_update), NULL);
    /* End of Corrections page */

    /* Start of transformations page */
    page = notebook_page_new(notebook, _("Crop and rotate"),
	    "crop", data->ToolTips);

    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));

    /* Start of Crop controls */
    label = gtk_label_new(_("Left:"));
    gtk_table_attach(table, label, 0, 1, 1, 2, 0, 0, 0, 0);

    data->CropX1Adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
	CFG->CropX1, 0, data->UF->initialWidth, 1, 1, 0));
    data->CropX1Spin = GTK_SPIN_BUTTON(gtk_spin_button_new(
	data->CropX1Adjustment, 1, 0));
    g_object_set_data(G_OBJECT(data->CropX1Adjustment),
	"Parent-Widget", data->CropX1Spin);
    gtk_table_attach(table,
	GTK_WIDGET(data->CropX1Spin), 1, 2, 1, 2, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->CropX1Adjustment), "value-changed",
	G_CALLBACK(adjustment_update), &CFG->CropX1);

    label = gtk_label_new(_("Top:"));
    gtk_table_attach(table, label, 1, 2, 0, 1, 0, 0, 0, 0);

    data->CropY1Adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
	CFG->CropY1, 0, data->UF->initialHeight, 1, 1, 0));
    data->CropY1Spin = GTK_SPIN_BUTTON(gtk_spin_button_new(
	data->CropY1Adjustment, 1, 0));
    g_object_set_data(G_OBJECT(data->CropY1Adjustment),
	"Parent-Widget", data->CropY1Spin);
    gtk_table_attach(table,
	GTK_WIDGET(data->CropY1Spin), 2, 3, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->CropY1Adjustment), "value-changed",
	G_CALLBACK(adjustment_update), &CFG->CropY1);

    label = gtk_label_new(_("Right:"));
    gtk_table_attach(table, label, 2, 3, 1, 2, 0, 0, 0, 0);

    data->CropX2Adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
	CFG->CropX2, 0, data->UF->initialWidth, 1, 1, 0));
    data->CropX2Spin = GTK_SPIN_BUTTON(gtk_spin_button_new(
	data->CropX2Adjustment, 1, 0));
    g_object_set_data(G_OBJECT(data->CropX2Adjustment),
	"Parent-Widget", data->CropX2Spin);
    gtk_table_attach(table,
	GTK_WIDGET(data->CropX2Spin), 3, 4, 1, 2, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->CropX2Adjustment), "value-changed",
	G_CALLBACK(adjustment_update), &CFG->CropX2);

    label = gtk_label_new(_("Bottom:"));
    gtk_table_attach(table, label, 1, 2, 2, 3, 0, 0, 0, 0);

    data->CropY2Adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
	CFG->CropY2, 0, data->UF->initialHeight, 1, 1, 0));
    data->CropY2Spin = GTK_SPIN_BUTTON(gtk_spin_button_new(
	data->CropY2Adjustment, 1, 0));
    g_object_set_data(G_OBJECT(data->CropY2Adjustment),
	"Parent-Widget", data->CropY2Spin);
    gtk_table_attach(table,
	GTK_WIDGET(data->CropY2Spin), 2, 3, 2, 3, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(data->CropY2Adjustment), "value-changed",
	G_CALLBACK(adjustment_update), &CFG->CropY2);

    // Crop reset button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
	GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON));
    gtk_tooltips_set_tip(data->ToolTips, button,
	_("Reset the crop region"),
	NULL);
    gtk_table_attach(table, button, 4, 5, 1, 2, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
	G_CALLBACK(crop_reset), NULL);

    /* Aspect ratio controls */
    table = GTK_TABLE(table_with_frame(page, NULL, FALSE));

    label = gtk_label_new(_("Aspect ratio:"));
    gtk_table_attach(table, label, 0, 1, 0, 1, 0, 0, 0, 0);

    entry = gtk_combo_box_entry_new_text();
    data->AspectEntry = GTK_ENTRY (GTK_BIN (entry)->child);
    gtk_tooltips_set_tip(data->ToolTips, GTK_WIDGET(data->AspectEntry),
	    _("Crop area aspect ratio.\n"
	      "Can be entered in decimal notation (1.273)\n"
	      "or as a ratio of two numbers (14:11)"), NULL);
    gtk_entry_set_width_chars (data->AspectEntry, 6);
    gtk_entry_set_alignment (data->AspectEntry, 0.5);
    gtk_table_attach(table, GTK_WIDGET (entry), 1, 2, 0, 1, 0, 0, 5, 0);

    size_t s;
    for (s = 0; s < sizeof (predef_aspects) / sizeof (predef_aspects [0]); s++)
    {
	gtk_combo_box_append_text (GTK_COMBO_BOX(entry),
				   predef_aspects [s].text);
    }

    g_signal_connect(G_OBJECT(entry), "changed",
                     G_CALLBACK(aspect_changed), NULL);

    button = gtk_check_button_new();
    gtk_tooltips_set_tip(data->ToolTips, button, _("Lock aspect ratio"), NULL);
    gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(button), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), FALSE);
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                "object-lock", GTK_ICON_SIZE_BUTTON));
    //gtk_box_pack_start (hbox, button, FALSE, FALSE, 0);
    gtk_table_attach(table, button, 2, 3, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(lock_aspect), 0);

    /* Set current aspect ratio */
    set_new_aspect (data);

    /* Size/shrink controls */
    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));
    label = gtk_label_new(_("Shrink factor"));
    gtk_table_attach(GTK_TABLE(table), label, 0, 1, 1, 2, 0, 0, 0, 0);
    data->ShrinkAdjustment = GTK_ADJUSTMENT(gtk_adjustment_new(CFG->shrink,
            1, 100, 1, 2, 3));
    g_object_set_data(G_OBJECT(data->ShrinkAdjustment),
		"Adjustment-Accuracy", (gpointer)0);
    data->ShrinkSpin = GTK_SPIN_BUTTON(gtk_spin_button_new(
	    data->ShrinkAdjustment, 1, 3));
    g_object_set_data(G_OBJECT(data->ShrinkAdjustment), "Parent-Widget",
	    data->ShrinkSpin);
    g_signal_connect(G_OBJECT(data->ShrinkAdjustment), "value-changed",
            G_CALLBACK(adjustment_update), &data->shrink);
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(data->ShrinkSpin),
	    1, 2, 1, 2, 0, 0, 0, 0);

    label = gtk_label_new(_("Height"));
    gtk_table_attach(GTK_TABLE(table), label, 2, 3, 1, 2, 0, 0, 0, 0);
    data->HeightAdjustment = GTK_ADJUSTMENT(gtk_adjustment_new(data->height,
            10, 0, 10, 100, 0));
    g_object_set_data(G_OBJECT(data->HeightAdjustment),
		"Adjustment-Accuracy", (gpointer)0);
    data->HeightSpin = GTK_SPIN_BUTTON(gtk_spin_button_new(
	    data->HeightAdjustment, 10, 0));
    g_object_set_data(G_OBJECT(data->HeightAdjustment), "Parent-Widget",
	    data->HeightSpin);
    g_signal_connect(G_OBJECT(data->HeightAdjustment), "value-changed",
            G_CALLBACK(adjustment_update), &data->height);
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(data->HeightSpin),
		3, 4, 1, 2, 0, 0, 0, 0);

    label = gtk_label_new(_("Width"));
    gtk_table_attach(GTK_TABLE(table), label, 4, 5, 1, 2, 0, 0, 0, 0);
    data->WidthAdjustment = GTK_ADJUSTMENT(gtk_adjustment_new(data->width,
            10, 0, 10, 100, 0));
    g_object_set_data(G_OBJECT(data->WidthAdjustment),
		"Adjustment-Accuracy", (gpointer)0);
    data->WidthSpin = GTK_SPIN_BUTTON(gtk_spin_button_new(
	    data->WidthAdjustment, 10, 0));
    g_object_set_data(G_OBJECT(data->WidthAdjustment), "Parent-Widget",
	    data->WidthSpin);
    g_signal_connect(G_OBJECT(data->WidthAdjustment), "value-changed",
            G_CALLBACK(adjustment_update), &data->width);
    gtk_table_attach(GTK_TABLE(table), GTK_WIDGET(data->WidthSpin),
	    5, 6, 1, 2, 0, 0, 0, 0);

    /* Orientation controls */
    table = GTK_TABLE(table_with_frame(page, NULL, TRUE));

    label = gtk_label_new(_("Orientation:"));
    gtk_table_attach(table, label, 0, 1, 0, 1, 0, 0, 0, 0);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                "object-rotate-right", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_table_attach(table, button, 1, 2, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(flip_image), (gpointer)6);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                "object-rotate-left", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_table_attach(table, button, 2, 3, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(flip_image), (gpointer)5);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                "object-flip-horizontal", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_table_attach(table, button, 3, 4, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(flip_image), (gpointer)1);

    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                "object-flip-vertical", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_table_attach(table, button, 4, 5, 0, 1, 0, 0, 0, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
		     G_CALLBACK(flip_image), (gpointer)2);
    /* End of transformation page */

    /* Start of EXIF page */
    page = notebook_page_new(notebook, _("EXIF"), "exif", data->ToolTips);

    frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(page), frame, TRUE, TRUE, 0);
    vBox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(vBox));

    GtkListStore *store;
    store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

    align = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX (vBox), align, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(align),
            GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *treeview = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));
    gtk_tree_view_set_search_column (GTK_TREE_VIEW (treeview), 0);
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (treeview), TRUE);
    // does not work???
    gtk_tree_view_set_headers_clickable (GTK_TREE_VIEW (treeview), FALSE);
    g_object_unref (store);

    gtk_container_add(GTK_CONTAINER (align), treeview);

    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes (
        _("Tag"), gtk_cell_renderer_text_new (), "text", 0, NULL);
    gtk_tree_view_column_set_sort_column_id (column, 0);
    gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

    column = gtk_tree_view_column_new_with_attributes (
        _("Value"), gtk_cell_renderer_text_new (), "text", 1, NULL);
    gtk_tree_view_column_set_sort_column_id (column, 1);
    gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

    // Fill table with EXIF tags
    list_store_add(store, _("Camera maker"), CFG->make);
    list_store_add(store, _("Camera model"), CFG->model);
    list_store_add(store, _("Timestamp"), CFG->timestamp);
    list_store_add(store, _("Exposure"), CFG->shutterText);
    list_store_add(store, _("Aperture"), CFG->apertureText);
    list_store_add(store, _("ISO speed"), CFG->isoText);
    list_store_add(store, _("Focal length"), CFG->focalLenText);
    list_store_add(store, _("35mm focal length"), CFG->focalLen35Text);
    list_store_add(store, _("Lens"), CFG->lensText);
    list_store_add(store, _("Flash"), CFG->flashText);

    label = gtk_label_new(NULL);
    GString *message = g_string_new("");
    g_string_append_printf(message, _("EXIF data read by %s"), CFG->exifSource);
    gtk_label_set_markup(GTK_LABEL(label), message->str);
    gtk_box_pack_start(GTK_BOX (vBox), label, FALSE, FALSE, 0);

    if (uf->exifBuf==NULL)
    {
        label = gtk_label_new(NULL);
        char *text = g_strdup_printf("<span foreground='red'>%s</span>",
			_("Warning: EXIF data will not be sent to output"));
        gtk_label_set_markup(GTK_LABEL(label), text);
	g_free(text);
        gtk_box_pack_start(GTK_BOX (vBox), label, FALSE, FALSE, 0);
    }
    /* End of EXIF page */

    table = GTK_TABLE(table_with_frame(previewVBox,
	    _(expanderText[live_expander]), CFG->expander[live_expander]));
    event_box = gtk_event_box_new();
    gtk_table_attach(table, event_box, 0, 7, 1, 2, 0, 0, 0, 0);
    pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
            live_his_size+2, CFG->liveHistogramHeight+2);
    data->LiveHisto = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    gtk_container_add(GTK_CONTAINER(event_box), data->LiveHisto);
    pixies = gdk_pixbuf_get_pixels(pixbuf);
    rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    memset(pixies, 0, (gdk_pixbuf_get_height(pixbuf)-1)* rowstride +
            gdk_pixbuf_get_width(pixbuf)*gdk_pixbuf_get_n_channels(pixbuf));
    menu = gtk_menu_new();
    g_object_set_data(G_OBJECT(menu), "Parent-Widget", event_box);
    g_signal_connect_swapped(G_OBJECT(event_box), "button_press_event",
            G_CALLBACK(histogram_menu), menu);
    group = NULL;
    menu_item = gtk_radio_menu_item_new_with_label(group, _("RGB histogram"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 0, 1);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->histogram==rgb_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)rgb_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->histogram);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("R+G+B histogram"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 1, 2);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->histogram==r_g_b_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)r_g_b_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->histogram);
    menu_item = gtk_radio_menu_item_new_with_label(group,
	    _("Luminosity histogram"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 2, 3);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->histogram==luminosity_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)luminosity_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->histogram);
    menu_item = gtk_radio_menu_item_new_with_label(group,
	    _("Value (maximum) histogram"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 3, 4);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->histogram==value_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)value_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->histogram);
    menu_item = gtk_radio_menu_item_new_with_label(group,
	    _("Saturation histogram"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 4, 5);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->histogram==saturation_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)saturation_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->histogram);

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 5, 6);

    group = NULL;
    menu_item = gtk_radio_menu_item_new_with_label(group, _("Linear"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 6, 7);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->liveHistogramScale==linear_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)linear_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->liveHistogramScale);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("Logarithmic"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 7, 8);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->liveHistogramScale==log_histogram);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)log_histogram);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->liveHistogramScale);

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 8, 9);

    group = NULL;
    menu_item = gtk_radio_menu_item_new_with_label(group, _("96 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 9, 10);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->liveHistogramHeight==96);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)96);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->liveHistogramHeight);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("128 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 10, 11);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->liveHistogramHeight==128);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)128);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->liveHistogramHeight);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("192 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 11, 12);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->liveHistogramHeight==192);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)192);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->liveHistogramHeight);
    menu_item = gtk_radio_menu_item_new_with_label(group, _("256 Pixels"));
    gtk_menu_attach(GTK_MENU(menu), menu_item, 0, 1, 12, 13);
    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menu_item));
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),
	    CFG->liveHistogramHeight==256);
    g_object_set_data(G_OBJECT(menu_item), "Radio-Value",
	    (gpointer)256);
    g_signal_connect(G_OBJECT(menu_item), "toggled",
            G_CALLBACK(radio_menu_update), &CFG->liveHistogramHeight);
    gtk_widget_show_all(menu);

    i = 2;
    data->AvrLabels = color_labels_new(table, 0, i++, _("Average:"),
	    pixel_format, without_zone, data);
    data->DevLabels = color_labels_new(table, 0, i++, _("Std. deviation:"),
            pixel_format, without_zone, data);
    data->OverLabels = color_labels_new(table, 0, i,
	    _("Overexposed:"), percent_format, without_zone, data);
    toggle_button(table, 4, i, NULL, &CFG->overExp);
    button = gtk_button_new_with_label(_("Indicate"));
    gtk_table_attach(table, button, 6, 7, i, i+1,
            GTK_SHRINK|GTK_FILL, GTK_FILL, 0, 0);
    g_signal_connect(G_OBJECT(button), "pressed",
            G_CALLBACK(render_special_mode), (void *)render_overexposed);
    g_signal_connect(G_OBJECT(button), "released",
            G_CALLBACK(render_special_mode), (void *)render_default);
    i++;
    data->UnderLabels = color_labels_new(table, 0, i, _("Underexposed:"),
            percent_format, without_zone, data);
    toggle_button(table, 4, i, NULL, &CFG->underExp);
    button = gtk_button_new_with_label(_("Indicate"));
    gtk_table_attach(table, button, 6, 7, i, i+1,
            GTK_SHRINK|GTK_FILL, GTK_FILL, 0, 0);
    g_signal_connect(G_OBJECT(button), "pressed",
            G_CALLBACK(render_special_mode), (void *)render_underexposed);
    g_signal_connect(G_OBJECT(button), "released",
            G_CALLBACK(render_special_mode), (void *)render_default);
    i++;

    /* Right side of the preview window */
    vBox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(previewHBox, vBox, TRUE, TRUE, 2);
    data->PreviewPixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
            preview_width, preview_height);
    GtkWidget *PreviewEventBox = gtk_event_box_new();
#ifdef HAVE_GTKIMAGEVIEW
    data->PreviewWidget = gtk_image_view_new();
    gtk_image_view_set_pixbuf(GTK_IMAGE_VIEW(data->PreviewWidget),
	    data->PreviewPixbuf, FALSE);
    gtk_image_view_set_zoom(GTK_IMAGE_VIEW(data->PreviewWidget), 1.0);
    gtk_event_box_set_above_child(GTK_EVENT_BOX(PreviewEventBox), TRUE);

    GtkWidget *scroll = gtk_image_scroll_win_new(
	    GTK_IMAGE_VIEW(data->PreviewWidget));
    gtk_widget_set_size_request(scroll, preview_width, preview_height);
    GtkWidget *container =
	    gtk_widget_get_ancestor(data->PreviewWidget, GTK_TYPE_TABLE);
    g_object_ref(G_OBJECT(data->PreviewWidget));
    gtk_container_remove(GTK_CONTAINER(container), data->PreviewWidget);
    gtk_container_add(GTK_CONTAINER(PreviewEventBox), data->PreviewWidget);
    g_object_unref(G_OBJECT(data->PreviewWidget));
    gtk_table_attach(GTK_TABLE(container), PreviewEventBox, 0, 1, 0, 1,
	    GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
    gtk_box_pack_start(GTK_BOX(vBox), scroll, TRUE, TRUE, 0);
#else
    align = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_box_pack_start(GTK_BOX(vBox), align, TRUE, TRUE, 0);
    box = GTK_BOX(gtk_vbox_new(FALSE, 0));
    gtk_container_add(GTK_CONTAINER(align), GTK_WIDGET(box));
    gtk_box_pack_start(box, PreviewEventBox, FALSE, FALSE, 0);
    data->PreviewWidget = gtk_image_new_from_pixbuf(data->PreviewPixbuf);
    gtk_misc_set_alignment(GTK_MISC(data->PreviewWidget), 0, 0);
    gtk_container_add(GTK_CONTAINER(PreviewEventBox), data->PreviewWidget);
#endif
    data->PreviewButtonPressed = FALSE;
    g_signal_connect(G_OBJECT(PreviewEventBox), "button-press-event",
	    G_CALLBACK(preview_button_press), NULL);
    g_signal_connect(G_OBJECT(PreviewEventBox), "button-release-event",
	    G_CALLBACK(preview_button_release), NULL);
    g_signal_connect(G_OBJECT(PreviewEventBox), "motion-notify-event",
	    G_CALLBACK(preview_motion_notify), NULL);
    gtk_widget_add_events(PreviewEventBox, GDK_POINTER_MOTION_MASK);
    g_object_unref(data->PreviewPixbuf);

    data->ProgressBar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_box_pack_start(GTK_BOX(vBox), GTK_WIDGET(data->ProgressBar),
	    FALSE, FALSE, 0);

    /* Control buttons at the bottom */
    GtkBox *ControlsBox = GTK_BOX(gtk_hbox_new(FALSE, 6));
    gtk_box_pack_start(GTK_BOX(vBox), GTK_WIDGET(ControlsBox), FALSE, FALSE, 6);
    // Zoom buttons are centered:
    GtkBox *ZoomBox = GTK_BOX(gtk_hbox_new(FALSE, 0));
    gtk_box_pack_start(ControlsBox, GTK_WIDGET(ZoomBox), TRUE, FALSE, 0);
#ifdef HAVE_GTKIMAGEVIEW
    // Zoom out button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_ZOOM_OUT, GTK_ICON_SIZE_BUTTON));
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(zoom_out_event), NULL);
    gtk_box_pack_start(ZoomBox, button, FALSE, FALSE, 0);

    // Zoom percentage spin button:
    data->ZoomAdjustment = GTK_ADJUSTMENT(gtk_adjustment_new(
		CFG->Zoom, 5, 50, 1, 1, 0));
    g_object_set_data(G_OBJECT(data->ZoomAdjustment),
		"Adjustment-Accuracy", (gpointer)0);
    button = gtk_spin_button_new(data->ZoomAdjustment, 1, 0);
    g_object_set_data(G_OBJECT(data->ZoomAdjustment), "Parent-Widget", button);
    g_signal_connect(G_OBJECT(data->ZoomAdjustment), "value-changed",
		G_CALLBACK(adjustment_update), &CFG->Zoom);
    gtk_tooltips_set_tip(data->ToolTips, button,
	    _("Zoom percentage"), NULL);
    gtk_box_pack_start(ZoomBox, button, FALSE, FALSE, 0);

    // Zoom in button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_ZOOM_IN, GTK_ICON_SIZE_BUTTON));
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(zoom_in_event), NULL);
    gtk_box_pack_start(ZoomBox, button, FALSE, FALSE, 0);

    // Zoom fit button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_ZOOM_FIT, GTK_ICON_SIZE_BUTTON));
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(zoom_fit_event), NULL);
    gtk_box_pack_start(ZoomBox, button, FALSE, FALSE, 0);

    // Zoom max button:
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button), gtk_image_new_from_stock(
                GTK_STOCK_ZOOM_100, GTK_ICON_SIZE_BUTTON));
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(zoom_max_event), NULL);
    gtk_box_pack_start(ZoomBox, button, FALSE, FALSE, 0);
#endif // HAVE_GTKIMAGEVIEW

    // The rest of the control button are aligned to the right
    box = GTK_BOX(gtk_hbox_new(FALSE, 6));
    gtk_box_pack_start(GTK_BOX(ControlsBox), GTK_WIDGET(box), FALSE, FALSE, 0);
    /* Options button */
    button = gtk_button_new();
    hbox = GTK_BOX(gtk_hbox_new(FALSE, 6));
    gtk_container_add(GTK_CONTAINER(button), GTK_WIDGET(hbox));
    gtk_box_pack_start(hbox, gtk_image_new_from_stock(
            GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_BUTTON), FALSE, FALSE, 0);
    gtk_box_pack_start(hbox, gtk_label_new(_("Options")),
            FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(options_dialog), previewWindow);
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);

    if (!plugin) {
	// Comment to translator: All control buttons
	// "_Delete", "_Cancel", "_Save", "Save _As", "Send to _Gimp"
	// should have unique mnemonics.
	// Delete button:
	button = control_button(GTK_STOCK_DELETE, _("_Delete"),
		delete_button, data);
	gtk_box_pack_start(box, button, FALSE, FALSE, 0);
    }
    // Cancel button:
    button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
    g_signal_connect(G_OBJECT(button), "clicked",
            G_CALLBACK(control_button_event), cancel_button);
    gtk_box_pack_start(box, button, FALSE, FALSE, 0);

    /* plugin=0 : Normal stand-alone
     * plugin=1 : Gimp plug-in
     * plugin=2 : Stand-alone with --output option */
    if ( plugin==1 ) {
	// OK button for the plug-in
	saveButton = gtk_button_new_from_stock(GTK_STOCK_OK);
        gtk_box_pack_start(box, saveButton, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(saveButton), "clicked",
		G_CALLBACK(control_button_event), (gpointer)ok_button);
        gtk_widget_grab_focus(saveButton);
    } else {
	// Save button for the stand-alone tool
	saveButton = gtk_button_new_from_stock(GTK_STOCK_SAVE);
        gtk_box_pack_start(box, saveButton, FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(saveButton), "clicked",
		G_CALLBACK(control_button_event), (gpointer)save_button);

	char *absFilename = uf_file_set_absolute(CFG->outputFilename);
        char *utf8 = g_filename_to_utf8(absFilename, -1, NULL, NULL, NULL);
        if (utf8==NULL) utf8 = g_strdup("Unknown file name");
        char *text = g_strdup_printf(_("Filename: %s%s"), utf8,
                CFG->createID==also_id ? _("\nCreate also ID file") :
                CFG->createID==only_id ? _("\nCreate only ID file") : "");
        g_free(utf8);
        g_free(absFilename);
	gtk_tooltips_set_tip(data->ToolTips, saveButton, text, NULL);
	g_free(text);
    }
    if ( plugin==0 ) {
	// Save as button
	saveAsButton = control_button(GTK_STOCK_SAVE_AS,
		_("Save _As"), save_as_button, data);
        gtk_box_pack_start(box, saveAsButton, FALSE, FALSE, 0);
        gtk_widget_grab_focus(saveAsButton);

	// Send to Gimp button
	GtkWidget *gimpButton = control_button("gimp",
		_("Send image to _Gimp"), gimp_button, data);
        gtk_box_pack_start(box, gimpButton, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(previewWindow);
    gtk_widget_hide(GTK_WIDGET(data->SpotTable));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), openingPage);
#ifdef HAVE_GTKIMAGEVIEW
    /* After window size was set, the user may want to shrink it */
    gtk_widget_set_size_request(scroll, -1, -1);
#endif
    for (i=0; i<cursor_num; i++)
	data->Cursor[i] = gdk_cursor_new(Cursors[i]);
    gdk_window_set_cursor(PreviewEventBox->window, data->Cursor[spot_cursor]);
    gtk_widget_set_sensitive(data->Controls, FALSE);
    preview_progress(previewWindow, _("Loading preview"), 0.2);
    ufraw_load_raw(uf);
    gtk_widget_set_sensitive(data->Controls, TRUE);

    create_base_image(data);

    /* Collect raw histogram data */
    memset(data->raw_his, 0, sizeof(data->raw_his));
    ufraw_image_data *image = &data->UF->Images[ufraw_first_phase];
    for (i=0; i<image->height*image->width; i++) {
	    guint16 *buf = (guint16*)(image->buffer+i*image->depth);
            for (c=0; c<data->UF->colors; c++)
                data->raw_his[MIN( buf[c] *
                        (raw_his_size-1) / data->UF->rgbMax,
                        raw_his_size-1) ][c]++;
    }
    /* Save initial WB data for the sake of "Reset WB" */
    g_strlcpy(data->initialWB, CFG->wb, max_name);
    data->initialTemperature = CFG->temperature;
    data->initialGreen = CFG->green;
    for (i=0; i<4; i++) data->initialChanMul[i] = CFG->chanMul[i];

    /* Update the curve editor in case ufraw_convert_image() modified it. */
    curveeditor_widget_set_curve(data->CurveWidget,
	    &CFG->curve[CFG->curveIndex]);

    data->FreezeDialog = FALSE;
    data->RenderMode = render_default;
    update_crop_ranges(data);
    update_scales(data);

    data->OverUnderTicker = 0;

    gtk_main();
    status = (long)g_object_get_data(G_OBJECT(previewWindow),
            "WindowResponse");
    gtk_container_foreach(GTK_CONTAINER(previewVBox),
            (GtkCallback)(expander_state), NULL);
    ufraw_focus(previewWindow, FALSE);
    gtk_widget_destroy(previewWindow);
    for (i=0; i<cursor_num; i++)
	gdk_cursor_unref(data->Cursor[i]);
    /* Make sure that there are no preview idle task remaining */
    g_idle_remove_by_data(data);
    if (data->BlinkTimer) {
	g_source_remove(data->BlinkTimer);
	data->BlinkTimer = 0;
    }
    /* In interactive mode outputPath is taken into account only once */
    strcpy(uf->conf->outputPath, "");

    if ( status==GTK_RESPONSE_OK ) {
	if ( CFG->saveConfiguration==enabled_state ) {
	    /* Keep output path in saved configuration. */
	    char *cp = g_path_get_dirname(CFG->outputFilename);
	    strcpy(CFG->outputPath, cp);
	    g_free(cp);
	    /* Save configuration from CFG, but not the output filename. */
	    strcpy(CFG->outputFilename, "");
	    conf_save(CFG, NULL, NULL);
	/* If save 'only this once' was chosen, then so be it */
	} else if ( CFG->saveConfiguration==apply_state ) {
	    CFG->saveConfiguration = disabled_state;
	    conf_save(CFG, NULL, NULL);
	} else if ( CFG->saveConfiguration==disabled_state ) {
	    /* If save 'never again' was set in this session, we still
	     * need to save this setting */
	    if ( RC.saveConfiguration!=disabled_state ) {
		RC.saveConfiguration = disabled_state;
		conf_save(&RC, NULL, NULL);
	    }
	    strcpy(RC.inputFilename, "");
	    strcpy(RC.outputFilename, "");
	    *CFG = RC;
	}
    } else {
	strcpy(RC.inputFilename, "");
	strcpy(RC.outputFilename, "");
	*CFG = RC;
    }
    // UFRAW_RESPONSE_DELETE requires no special action
    ufraw_close(data->UF);
    g_list_free(data->WBPresets);
    g_free(data->SpotLabels);
    g_free(data->AvrLabels);
    g_free(data->DevLabels);
    g_free(data->OverLabels);
    g_free(data->UnderLabels);

    if (status!=GTK_RESPONSE_OK) return UFRAW_CANCEL;
    return UFRAW_SUCCESS;
}

/* vim:set shiftwidth=4: */
/* vim:set softtabstop=4: */
/* emacs: -*- c-basic-offset: 4 -*- */
