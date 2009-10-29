/*
 * UFRaw - Unidentified Flying Raw converter for digital camera images
 *
 * ufraw_ufraw.c - program interface to all the components
 * Copyright 2004-2009 by Udi Fuchs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/stat.h> /* for fstat() */
#include <math.h>
#include <errno.h>
#ifdef HAVE_LIBZ
#include <zlib.h>
#endif
#ifdef HAVE_LIBBZ2
#include <bzlib.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "uf_glib.h"
#include <glib/gi18n.h>
#include "dcraw_api.h"
#include "nikon_curve.h"
#include "ufraw.h"

static void ufraw_convert_image_lensfun(ufraw_data *uf, UFRawPhase phase);
static void ufraw_convert_reverse_wb(ufraw_data *uf, UFRawPhase phase);
static void ufraw_convert_import_buffer(ufraw_data *uf, UFRawPhase phase, dcraw_image_data *dcimg);
static void ufraw_convert_prepare_buffers(ufraw_data *uf);

static int make_temporary(char *basefilename, char **tmpfilename)
{
    int fd;
    char *basename = g_path_get_basename(basefilename);
    char *template = g_strconcat(basename, ".tmp.XXXXXX", NULL);
    fd = g_file_open_tmp(template, tmpfilename, NULL);
    g_free(template);
    g_free(basename);
    return fd;
}

static ssize_t writeall(int fd, const char *buf, ssize_t size)
{
  ssize_t written;
  ssize_t wr;
  for (written = 0; size > 0; size -= wr, written += wr, buf += wr)
	if ((wr = write(fd, buf, size)) < 0)
	    break;
  return written;
}

static char *decompress_gz(char *origfilename)
{
#ifdef HAVE_LIBZ
    char *tempfilename;
    int tmpfd;
    gzFile gzfile;
    char buf[8192];
    ssize_t size;
    if ( (tmpfd = make_temporary(origfilename, &tempfilename))==-1 )
	return NULL;
    char *filename = uf_win32_locale_filename_from_utf8(origfilename);
    gzfile = gzopen(filename, "rb");
    uf_win32_locale_filename_free(filename);
    if ( gzfile!=NULL ) {
	while ((size = gzread(gzfile, buf, sizeof buf)) > 0) {
	    if (writeall(tmpfd, buf, size) != size)
		break;
	}
	gzclose(gzfile);
	if (size == 0)
	    if (close(tmpfd) == 0)
		return tempfilename;
    }
    close(tmpfd);
    g_unlink(tempfilename);
    g_free(tempfilename);
    return NULL;
#else
    (void)origfilename;
    ufraw_message(UFRAW_SET_ERROR,
		  "Cannot open gzip compressed images.\n");
    return NULL;
#endif
}

static char *decompress_bz2(char *origfilename)
{
#ifdef HAVE_LIBBZ2
    char *tempfilename;
    int tmpfd;
    FILE *compfile;
    BZFILE *bzfile;
    int bzerror;
    char buf[8192];
    ssize_t size;

    if ( (tmpfd = make_temporary(origfilename, &tempfilename))==-1 )
	return NULL;
    compfile = g_fopen(origfilename, "rb");
    if ( compfile!=NULL ) {
	if ((bzfile = BZ2_bzReadOpen(&bzerror, compfile, 0, 0, 0, 0)) != 0) {
	    while ((size = BZ2_bzRead(&bzerror, bzfile, buf, sizeof buf)) > 0)
		if (writeall(tmpfd, buf, size) != size)
		    break;
	    BZ2_bzReadClose(&bzerror, bzfile);
	    fclose(compfile);
	    if (size == 0) {
		close(tmpfd);
		return tempfilename;
	    }
	}
    }
    close(tmpfd);
    g_unlink(tempfilename);
    g_free(tempfilename);
    return NULL;
#else
    (void)origfilename;
    ufraw_message(UFRAW_SET_ERROR,
		  "Cannot open bzip2 compressed images.\n");
    return NULL;
#endif
}

ufraw_data *ufraw_open(char *filename)
{
    int status;
    ufraw_data *uf;
    dcraw_data *raw;
    ufraw_message(UFRAW_CLEAN, NULL);
    conf_data *conf = NULL;
    char *fname, *hostname;
    char *origfilename;
    gchar *unzippedBuf = NULL;
    gsize unzippedBufLen = 0;

    fname = g_filename_from_uri(filename, &hostname, NULL);
    if (fname!=NULL) {
	if (hostname!=NULL) {
	    ufraw_message(UFRAW_SET_ERROR, _("Remote URI is not supported"));
	    g_free(hostname);
	    g_free(fname);
	    return NULL;
	}
	g_strlcpy(filename, fname, max_path);
	g_free(fname);
    }
    /* First handle ufraw ID files. */
    if ( strcasecmp(filename + strlen(filename) - 6, ".ufraw")==0 ) {
	conf = g_new(conf_data, 1);
	status = conf_load(conf, filename);
	if (status!=UFRAW_SUCCESS) {
	    g_free(conf);
	    return NULL;
	}

	/* If inputFilename and outputFilename have the same path,
	 * then inputFilename is searched for in the path of the ID file.
	 * This allows moving raw and ID files together between folders. */
	char *inPath = g_path_get_dirname(conf->inputFilename);
	char *outPath = g_path_get_dirname(conf->outputFilename);
	if ( strcmp(inPath, outPath)==0 ) {
	    char *path = g_path_get_dirname(filename);
	    char *inName = g_path_get_basename(conf->inputFilename);
	    char *inFile = g_build_filename(path, inName , NULL);
	    if ( g_file_test(inFile, G_FILE_TEST_EXISTS) ) {
		g_strlcpy(conf->inputFilename, inFile, max_path);
	    }
	    g_free(path);
	    g_free(inName);
	    g_free(inFile);
	}
	g_free(inPath);
	g_free(outPath);

	/* Output image should be created in the path of the ID file */
	char *path = g_path_get_dirname(filename);
	g_strlcpy(conf->outputPath, path, max_path);
	g_free(path);

	filename = conf->inputFilename;
    }
    origfilename = filename;
    if (!strcasecmp(filename + strlen(filename) - 3, ".gz"))
	filename = decompress_gz(filename);
    else if (!strcasecmp(filename + strlen(filename) - 4, ".bz2"))
	filename = decompress_bz2(filename);
    if (filename == 0) {
	ufraw_message(UFRAW_SET_ERROR,
		      "Error creating temporary file for compressed data.");
	return NULL;
    }
    raw = g_new(dcraw_data, 1);
    status = dcraw_open(raw, filename);
    if (filename != origfilename) {
	g_file_get_contents(filename, &unzippedBuf, &unzippedBufLen, NULL);
	g_unlink(filename);
	g_free(filename);
	filename = origfilename;
    }
    if ( status!=DCRAW_SUCCESS) {
	/* Hold the message without displaying it */
	ufraw_message(UFRAW_SET_WARNING, raw->message);
	if ( status!=DCRAW_WARNING) {
	    g_free(raw);
	    g_free(unzippedBuf);
	    return NULL;
	}
    }
    uf = g_new0(ufraw_data, 1);
    ufraw_message_init(uf);
    uf->unzippedBuf = unzippedBuf;
    uf->unzippedBufLen = unzippedBufLen;
    uf->conf = conf;
    g_strlcpy(uf->filename, filename, max_path);
    int i;
    for (i=ufraw_raw_phase; i<ufraw_phases_num; i++) {
	uf->Images[i].buffer = NULL;
	uf->Images[i].invalidate_event = TRUE;
    }
    uf->thumb.buffer = NULL;
    uf->raw = raw;
    uf->colors = raw->colors;
    uf->raw_color = raw->raw_color;
    uf->developer = NULL;
    uf->AutoDeveloper = NULL;
    uf->widget = NULL;
    uf->RawHistogram = NULL;
    uf->HaveFilters = raw->filters!=0;
    ufraw_message(UFRAW_SET_LOG, "ufraw_open: w:%d h:%d curvesize:%d\n",
	    raw->width, raw->height, raw->toneCurveSize);

    return uf;
}

int ufraw_load_darkframe(ufraw_data *uf)
{
    if ( strlen(uf->conf->darkframeFile)==0 )
	return UFRAW_SUCCESS;
    if ( uf->conf->darkframe!=NULL ) {
	// If the same file was already openned, there is nothing to do.
	if ( strcmp(uf->conf->darkframeFile, uf->conf->darkframe->filename)==0 )
	    return UFRAW_SUCCESS;
	// Otherwise we need to close the previous darkframe
	ufraw_close(uf->conf->darkframe);
    }
    ufraw_data *dark = uf->conf->darkframe =
	    ufraw_open(uf->conf->darkframeFile);
    if ( dark==NULL ){
	ufraw_message(UFRAW_ERROR, _("darkframe error: %s is not a raw file\n"),
		    uf->conf->darkframeFile);
	uf->conf->darkframeFile[0] = '\0';
	return UFRAW_ERROR;
    }
    dark->conf = g_new(conf_data, 1);
    conf_init (dark->conf);
    /* disable all auto settings on darkframe */
    dark->conf->autoExposure = disabled_state;
    dark->conf->autoBlack = disabled_state;
    if (ufraw_load_raw(dark)!=UFRAW_SUCCESS) {
	ufraw_message(UFRAW_ERROR, _("error loading darkframe '%s'\n"),
		uf->conf->darkframeFile);
	ufraw_close(dark);
	g_free(dark);
	uf->conf->darkframe = NULL;
	uf->conf->darkframeFile[0] = '\0';
	return UFRAW_ERROR;
    }
    // Make sure the darkframe matches the main data
    dcraw_data *raw = uf->raw;
    dcraw_data *darkRaw = dark->raw;
    if ( raw->width!=darkRaw->width ||
	 raw->height!=darkRaw->height ||
	 raw->colors!=darkRaw->colors ) {
	ufraw_message(UFRAW_WARNING,
		_("Darkframe '%s' is incompatible with main image"),
		uf->conf->darkframeFile);
	ufraw_close(dark);
	g_free(dark);
	uf->conf->darkframe = NULL;
	uf->conf->darkframeFile[0] = '\0';
	return UFRAW_ERROR;
    }
    ufraw_message(UFRAW_BATCH_MESSAGE, _("using darkframe '%s'\n"),
	    uf->conf->darkframeFile);
    /* Calculate dark frame hot pixel thresholds as the 99.99th percentile
     * value.  That is, the value at which 99.99% of the pixels are darker.
     * Pixels below this threshold are considered to be bias noise, and
     * those above are "hot". */
    int color;
    int i;
    long frequency[65536];
    long sum;
    long point = darkRaw->raw.width * darkRaw->raw.height / 10000;

    for (color = 0; color < darkRaw->raw.colors; ++color) {
	memset(frequency, 0, sizeof frequency);
	for (i = 0; i < darkRaw->raw.width * darkRaw->raw.height; ++i)
	    frequency[darkRaw->raw.image[i][color]]++;
	for (sum = 0, i = 65535; i > 1; --i) {
	    sum += frequency[i];
	    if (sum >= point)
		break;
	}
	darkRaw->thresholds[color] = i + 1;
    }
    return UFRAW_SUCCESS;
}

void ufraw_get_image_dimensions(dcraw_data *raw, ufraw_data *uf)
{
    dcraw_image_dimensions(raw, uf->conf->orientation,
	    &uf->initialHeight, &uf->initialWidth);

    // update rotated dimensions
    double rotationRadians = (uf->conf->rotationAngle * 2 * M_PI) / 360;
    uf->rotatedWidth = ceil((uf->initialHeight * sin(rotationRadians))
	+ (uf->initialWidth * cos(rotationRadians)));
    uf->rotatedHeight = ceil((uf->initialWidth * sin(rotationRadians))
	+ (uf->initialHeight * cos(rotationRadians)));

    if (uf->conf->CropX1 < 0) uf->conf->CropX1 = 0;
    if (uf->conf->CropY1 < 0) uf->conf->CropY1 = 0;
    if (uf->conf->CropX2 < 0) uf->conf->CropX2 = uf->rotatedWidth;
    if (uf->conf->CropY2 < 0) uf->conf->CropY2 = uf->rotatedHeight;
}

int ufraw_config(ufraw_data *uf, conf_data *rc, conf_data *conf, conf_data *cmd)
{
    int status;

    if (strcmp(rc->wb, spot_wb)) rc->chanMul[0] = -1.0;
    if (rc->autoExposure==enabled_state) rc->autoExposure = apply_state;
    if (rc->autoBlack==enabled_state) rc->autoBlack = apply_state;

    g_assert(uf!=NULL);

    /* Check if we are loading an ID file */
    if (uf->conf!=NULL) {
	/* ID file configuration is put "on top" of the rc data */
	uf->LoadingID = TRUE;
	conf_data tmp = *rc;
	conf_copy_image(&tmp, uf->conf);
	conf_copy_transform(&tmp, uf->conf);
	conf_copy_save(&tmp, uf->conf);
	g_strlcpy(tmp.outputFilename, uf->conf->outputFilename, max_path);
	g_strlcpy(tmp.outputPath, uf->conf->outputPath, max_path);
	*uf->conf = tmp;
    } else {
	uf->LoadingID = FALSE;
	uf->conf = g_new(conf_data, 1);
	*uf->conf = *rc;
    }
    if (conf!=NULL && conf->version!=0) {
	conf_copy_image(uf->conf, conf);
	conf_copy_save(uf->conf, conf);
	if (strcmp(uf->conf->wb, spot_wb)) uf->conf->chanMul[0] = -1.0;
	if (uf->conf->autoExposure==enabled_state)
	    uf->conf->autoExposure = apply_state;
	if (uf->conf->autoBlack==enabled_state)
	    uf->conf->autoBlack = apply_state;
    }
    if (cmd!=NULL) {
	status = conf_set_cmd(uf->conf, cmd);
	if (status!=UFRAW_SUCCESS) return status;
    }

    dcraw_data *raw = uf->raw;
    char *absname = uf_file_set_absolute(uf->filename);
    g_strlcpy(uf->conf->inputFilename, absname, max_path);
    g_free(absname);
    if (!uf->LoadingID) {
	g_snprintf(uf->conf->inputURI, max_path, "file://%s",
		uf->conf->inputFilename);
	struct stat s;
	fstat(fileno(raw->ifp), &s);
	g_snprintf(uf->conf->inputModTime, max_name, "%d", (int)s.st_mtime);
    }
    if (strlen(uf->conf->outputFilename)==0) {
	/* If output filename wasn't specified use input filename */
	char *filename = uf_file_set_type(uf->filename,
			file_type[uf->conf->type]);
	if (strlen(uf->conf->outputPath)>0) {
	    char *cp = g_path_get_basename(filename);
	    g_free(filename);
	    filename = g_build_filename(uf->conf->outputPath, cp , NULL);
	    g_free(cp);
	}
	g_strlcpy(uf->conf->outputFilename, filename, max_path);
	g_free(filename);
    }
    /*Reset EXIF data text fields to avoid spill over between images.*/
    strcpy(uf->conf->isoText, "");
    strcpy(uf->conf->shutterText, "");
    strcpy(uf->conf->apertureText, "");
    strcpy(uf->conf->focalLenText, "");
    strcpy(uf->conf->focalLen35Text, "");
    strcpy(uf->conf->lensText, "");
    strcpy(uf->conf->flashText, "");
    if ( !uf->conf->embeddedImage ) {
	if ( ufraw_exif_read_input(uf)!=UFRAW_SUCCESS ) {
	    ufraw_message(UFRAW_SET_LOG, "Error reading EXIF data from %s\n",
		    uf->filename);
	}
    }
    g_free(uf->unzippedBuf);
    uf->unzippedBuf = NULL;
    /* If we switched cameras, ignore channel multipliers and
     * change spot_wb to manual_wb */
    if ( !uf->LoadingID &&
	 ( strcmp(uf->conf->make, raw->make)!=0 ||
	   strcmp(uf->conf->model, raw->model)!=0 ) ) {
	uf->conf->chanMul[0] = -1.0;
	if (strcmp(uf->conf->wb, spot_wb)==0)
	    g_strlcpy(uf->conf->wb, manual_wb, max_name);
    }
    /* Set the EXIF data */
#ifdef __MINGW32__
    /* MinG32 does not have ctime_r(). */
    g_strlcpy(uf->conf->timestampText, ctime(&raw->timestamp), max_name);
#elif defined(__sun) && !defined(_POSIX_PTHREAD_SEMANTICS) /* Solaris */
    /*
     * Some versions of Solaris followed a draft POSIX.1c standard
     * where ctime_r took a third length argument.
     */
    ctime_r(&raw->timestamp, uf->conf->timestampText,
	    sizeof(uf->conf->timestampText));
#else
    /* POSIX.1c version of ctime_r() */
    ctime_r(&raw->timestamp, uf->conf->timestampText);
#endif
    if (uf->conf->timestampText[strlen(uf->conf->timestampText)-1]=='\n')
	uf->conf->timestampText[strlen(uf->conf->timestampText)-1] = '\0';
    g_strlcpy(uf->conf->make, raw->make, max_name);
    g_strlcpy(uf->conf->model, raw->model, max_name);

    uf->conf->timestamp = raw->timestamp;

    uf->conf->CameraOrientation = raw->flip;

    if ( !uf->conf->rotate ) {
	uf->conf->orientation = 0;
	uf->conf->rotationAngle = 0;
    } else {
	if ( !uf->LoadingID || uf->conf->orientation<0 )
	    uf->conf->orientation = uf->conf->CameraOrientation;
	// Normalise rotations to a flip, then rotation of 0 < a < 90 degrees.
	ufraw_normalize_rotation(uf);
    }

    if (uf->inputExifBuf==NULL) {
	g_strlcpy(uf->conf->exifSource, "DCRaw", max_name);
	uf->conf->iso_speed = raw->iso_speed;
	g_snprintf(uf->conf->isoText, max_name, "%d", (int)uf->conf->iso_speed);
	uf->conf->shutter = raw->shutter;
	if (uf->conf->shutter>0 && uf->conf->shutter<1)
	    g_snprintf(uf->conf->shutterText, max_name, "1/%0.1f s",
		    1/uf->conf->shutter);
	else
	    g_snprintf(uf->conf->shutterText, max_name, "%0.1f s",
		    uf->conf->shutter);
	uf->conf->aperture = raw->aperture;
	g_snprintf(uf->conf->apertureText, max_name, "F/%0.1f",
		uf->conf->aperture);
	uf->conf->focal_len = raw->focal_len;
	g_snprintf(uf->conf->focalLenText, max_name, "%0.1f mm",
		uf->conf->focal_len);
    }

    /* If there is an embeded curve we "turn on" the custom/camera curve
     * mechanism */
    if (raw->toneCurveSize!=0) {
	CurveData nc;
	long pos = ftell(raw->ifp);
	if (RipNikonNEFCurve(raw->ifp, raw->toneCurveOffset, &nc, NULL)
		!=UFRAW_SUCCESS) {
	    ufraw_message(UFRAW_ERROR, _("Error reading NEF curve"));
	    return UFRAW_WARNING;
	}
	fseek(raw->ifp, pos, SEEK_SET);
	if (nc.m_numAnchors<2) nc = conf_default.BaseCurve[0];

	g_strlcpy(nc.name, uf->conf->BaseCurve[custom_curve].name, max_name);
	uf->conf->BaseCurve[custom_curve] = nc;

	int use_custom_curve = 0;
	if (raw->toneModeSize) {
	    // "AUTO    " "HIGH    " "CS      " "MID.L   " "MID.H   "NORMAL  " "LOW     "
	    long pos = ftell(raw->ifp);
	    char buf[9];
	    fseek(raw->ifp, raw->toneModeOffset, SEEK_SET);
	    // read it in.
	    size_t num = fread(&buf, 9, 1, raw->ifp);
	    if ( num!=1 )
		// Maybe this should be a UFRAW_WARNING
		ufraw_message(UFRAW_SET_LOG,
			"Warning: tone mode fread %d != %d\n", num, 1);
	    fseek(raw->ifp, pos, SEEK_SET);

	    if (!strncmp(buf, "CS      ", sizeof(buf)))  use_custom_curve=1;

	    // down the line, we need to translate the other values into
	    // tone curves!
	}

	if (use_custom_curve) {
	    uf->conf->BaseCurve[camera_curve] =
		uf->conf->BaseCurve[custom_curve];
	    g_strlcpy(uf->conf->BaseCurve[camera_curve].name,
		    conf_default.BaseCurve[camera_curve].name, max_name);
	} else {
	    uf->conf->BaseCurve[camera_curve] =
		conf_default.BaseCurve[camera_curve];
	}
    } else {
	/* If there is no embeded curve we "turn off" the custom/camera curve
	 * mechanism */
	uf->conf->BaseCurve[camera_curve].m_numAnchors = 0;
	uf->conf->BaseCurve[custom_curve].m_numAnchors = 0;
	if (uf->conf->BaseCurveIndex==custom_curve ||
		uf->conf->BaseCurveIndex==camera_curve)
	    uf->conf->BaseCurveIndex = linear_curve;
    }
    ufraw_load_darkframe(uf);

    ufraw_get_image_dimensions(raw, uf);

    return UFRAW_SUCCESS;
}

static void ufraw_load_raw_progress(void *user_data, double progress)
{
    preview_progress(user_data, _("Loading preview"), progress);
}

/* Scale pixel values: occupy 16 bits to get more precision. In addition
 * this normalizes the pixel values which is good for non-linear algorithms
 * which forget to check rgbMax or assume a particular value. */
static unsigned ufraw_scale_raw(dcraw_data *raw)
{
    guint16 *p, *end;
    int scale;

    scale = 0;
    while ((raw->rgbMax << 1) <= 0xffff) {
	raw->rgbMax <<= 1;
	++scale;
    }
    if (scale) {
	end = (guint16 *)(raw->raw.image + raw->raw.width * raw->raw.height);
	/* OpenMP overhead appears to be too large in this case */
	/* We are assuming here that there will be no overflow since all
	 * pixel values should be below rgbMax. dcraw's wavelet_denoise()
	 * makes the same assumption, so it seems like a safe bet. */
	for (p = (guint16 *)raw->raw.image; p < end; ++p)
	    *p <<= scale;
	raw->black <<= scale;
    }
    return 1 << scale;
}

int ufraw_load_raw(ufraw_data *uf)
{
    int status;
    dcraw_data *raw = uf->raw;

    if (uf->conf->embeddedImage) {
	dcraw_image_data thumb;
	if ( (status=dcraw_load_thumb(raw, &thumb))!=DCRAW_SUCCESS ) {
	    ufraw_message(status, raw->message);
	    return status;
	}
	uf->thumb.height = thumb.height;
	uf->thumb.width = thumb.width;
	return ufraw_read_embedded(uf);
    }
    dcraw_set_progress_handle(raw, ufraw_load_raw_progress, uf->widget);
    if ( (status=dcraw_load_raw(raw))!=DCRAW_SUCCESS ) {
	ufraw_message(UFRAW_SET_LOG, raw->message);
	ufraw_message(status, raw->message);
	if (status!=DCRAW_WARNING) return status;
    }
    uf->raw_multiplier = ufraw_scale_raw(raw);
    /* Canon EOS cameras require special exposure normalization */
    if ( strcmp(uf->conf->make, "Canon")==0 &&
	 strncmp(uf->conf->model, "EOS", 3)==0 ) {
	int c, max = raw->cam_mul[0];
	for (c=1; c<raw->colors; c++) max = MAX(raw->cam_mul[c], max);
	/* Camera multipliers in DNG file are normalized to 1.
	 * Therefore, they can not be used to normalize exposure.
	 * Also, for some Canon DSLR cameras dcraw cannot read the
	 * camera multipliers (1D for example). */
	if ( max < 100 ) {
	    uf->conf->ExposureNorm = 0;
	    ufraw_message(UFRAW_SET_LOG, "Failed to normalizing exposure\n");
	} else {
	    /* Convert exposure value from old ID files from before
	     * ExposureNorm */
	    if (uf->LoadingID && uf->conf->ExposureNorm==0)
		uf->conf->exposure -= log(1.0*raw->rgbMax/max)/log(2);
	    uf->conf->ExposureNorm = max * raw->rgbMax / 4095;
	    ufraw_message(UFRAW_SET_LOG,
		    "Exposure Normalization set to %d (%.2f EV)\n",
		    uf->conf->ExposureNorm,
		    log(1.0*raw->rgbMax/uf->conf->ExposureNorm)/log(2));
	}
    } else {
	uf->conf->ExposureNorm = 0;
    }
    uf->rgbMax = raw->rgbMax - raw->black;
    memcpy(uf->rgb_cam, raw->rgb_cam, sizeof uf->rgb_cam);

    /* Foveon image dimensions are knows only after load_raw()*/
    ufraw_get_image_dimensions(raw, uf);
    if (uf->conf->CropX2 > uf->rotatedWidth)
	uf->conf->CropX2 = uf->rotatedWidth;
    if (uf->conf->CropY2 > uf->rotatedHeight)
	uf->conf->CropY2 = uf->rotatedHeight;

    /* chanMul[0]<0 signals that we need to recalculate the WB */
    if (uf->conf->chanMul[0]<0) ufraw_set_wb(uf);
    else {
	/* Otherwise we just normalize the channels and recalculate
	 * the temperature/green */
	int WBTuning = uf->conf->WBTuning;
	char wb[max_name];
	g_strlcpy(wb, uf->conf->wb, max_name);
	g_strlcpy(uf->conf->wb, spot_wb, max_name);
	ufraw_set_wb(uf);
	g_strlcpy(uf->conf->wb, wb, max_name);
	uf->conf->WBTuning = WBTuning;
    }
    ufraw_auto_expose(uf);
    ufraw_auto_black(uf);
    return UFRAW_SUCCESS;
}

void ufraw_close(ufraw_data *uf)
{
    dcraw_close(uf->raw);
    g_free(uf->unzippedBuf);
    g_free(uf->raw);
    /* We cannot free uf->conf since it might be accessed after the close */
//    g_free(uf->conf);
    g_free(uf->inputExifBuf);
    g_free(uf->outputExifBuf);
    int i;
    for (i=ufraw_raw_phase; i<ufraw_phases_num; i++)
	g_free(uf->Images[i].buffer);
    g_free(uf->thumb.buffer);
    developer_destroy(uf->developer);
    developer_destroy(uf->AutoDeveloper);
    g_free(uf->RawHistogram);
    if ( uf->conf->darkframe!=NULL ) {
	ufraw_close(uf->conf->darkframe);
	g_free(uf->conf->darkframe);
    }
    ufraw_message_reset(uf);
    ufraw_message(UFRAW_CLEAN, NULL);
}

/* Return the coordinates and the size of given image subarea.
 * There are always 32 subareas, numbered 0 to 31, ordered in a 4x8 matrix.
 */
void ufraw_img_get_subarea_coord (
    ufraw_image_data *img, unsigned saidx, int *x, int *y, int *w, int *h)
{
    int saw = (img->width + 3) / 4;
    int sah = (img->height + 7) / 8;
    int sax = saidx % 4;
    int say = saidx / 4;
    *x = saw * sax;
    *y = sah * say;
    *w = (sax < 3) ? saw : (img->width - saw * 3);
    *h = (say < 7) ? sah : (img->height - sah * 7);
}

/* Return the subarea index given some X,Y image coordinates.
 */
unsigned ufraw_img_get_subarea_idx (
    ufraw_image_data *img, int x, int y)
{
    int saw = (img->width + 3) / 4;
    int sah = (img->height + 7) / 8;
    return (x / saw) + (y / sah) * 4;
}

void ufraw_developer_prepare(ufraw_data *uf, DeveloperMode mode)
{
    int useMatrix = uf->conf->profileIndex[0] == 1 || uf->colors==4;

    if ( mode==auto_developer ) {
	if ( uf->AutoDeveloper==NULL )
	    uf->AutoDeveloper = developer_init();
	developer_prepare(uf->AutoDeveloper, uf->conf,
	    uf->rgbMax, uf->rgb_cam, uf->colors, useMatrix, mode);
    } else {
	if ( uf->developer==NULL )
	    uf->developer = developer_init();
	developer_prepare(uf->developer, uf->conf,
	    uf->rgbMax, uf->rgb_cam, uf->colors, useMatrix, mode);
    }
}

int ufraw_convert_image(ufraw_data *uf)
{
    uf->mark_hotpixels = FALSE;
    ufraw_developer_prepare(uf, file_developer);
    ufraw_convert_image_raw(uf, ufraw_raw_phase);
    ufraw_convert_image_first(uf, ufraw_first_phase);
    ufraw_convert_image_lensfun(uf, ufraw_first_phase);
    return UFRAW_SUCCESS;
}

#ifdef HAVE_LENSFUN

/* What about LF_MODIFY_ALL? */
#define UF_LF_ALL (LF_MODIFY_TCA | LF_MODIFY_VIGNETTING | \
	LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE)

/* Lanczos kernel is precomputed in a table with this resolution
 * The value below seems to be enough for HQ upscaling up to eight times
 */
#define LANCZOS_TABLE_RES  256
/* A support of 3 gives an overall sharper looking image, but
 * it is a) slower b) gives more sharpening artefacts
 */
#define LANCZOS_SUPPORT    2
/* Define this to use a floating-point implementation of Lanczos interpolation.
 * The integer implementation is a little bit less accurate, but MUCH faster
 * (even on machines with FPU - ~2.5 times faster on Core2); besides, it will
 * run a hell lot faster on computers without a FPU (e.g. PDAs).
 */
//#define LANCZOS_DATA_FLOAT
#ifdef LANCZOS_DATA_FLOAT
#define LANCZOS_DATA_TYPE float
#define LANCZOS_DATA_ONE 1.0
#else
#define LANCZOS_DATA_TYPE int
#define LANCZOS_DATA_ONE 4096
#endif

void ufraw_lensfun_modify (
    dcraw_image_data *img, lfModifier *modifier, int modflags)
{
    /* Apply vignetting correction first, before distorting the image */
    if (modflags & LF_MODIFY_VIGNETTING)
        lf_modifier_apply_color_modification (
            modifier, img->image, 0.0, 0.0, img->width, img->height,
            LF_CR_4 (RED, GREEN, BLUE, UNKNOWN),
            img->width * sizeof (dcraw_image_type));

    /* Now apply distortion, TCA and geometry in a single pass */
    if (modflags & (UF_LF_ALL & ~LF_MODIFY_VIGNETTING))
    {
        dcraw_image_type *image2 = g_new (dcraw_image_type, img->height * img->width);
        dcraw_image_type *cur = image2;
        dcraw_image_type *tmp;
        int i, x, y, c;

        /* Precompute the Lanczos kernel */
        LANCZOS_DATA_TYPE lanczos_func [LANCZOS_SUPPORT * LANCZOS_SUPPORT * LANCZOS_TABLE_RES];
        for (i = 0; i < LANCZOS_SUPPORT * LANCZOS_SUPPORT * LANCZOS_TABLE_RES; i++)
            if (i == 0)
                lanczos_func [i] = LANCZOS_DATA_ONE;
            else
            {
                float d = sqrt (((float)i) / LANCZOS_TABLE_RES);
                lanczos_func [i] = (LANCZOS_DATA_TYPE)
                    ((LANCZOS_DATA_ONE * LANCZOS_SUPPORT *
                      sin (M_PI * d) * sin ((M_PI / LANCZOS_SUPPORT) * d)) /
                     (M_PI * M_PI * d * d));
            }

        float *buff = g_new (float, img->width * 3 * 2);
        for (y = 0; y < img->height; y++)
        {
            if (!lf_modifier_apply_subpixel_geometry_distortion (
                modifier, 0, y, img->width, 1, buff))
                goto no_distortion; /* should not happen */

            float *modcoord = buff;
            for (x = 0; x < img->width; x++, cur++)
                for (c = 0; c < 3; c++, modcoord += 2)
                {
#ifdef LANCZOS_DATA_FLOAT
                    float xs = ceilf  (modcoord [0]) - LANCZOS_SUPPORT;
                    float xe = floorf (modcoord [0]) + LANCZOS_SUPPORT;
                    float ys = ceilf  (modcoord [1]) - LANCZOS_SUPPORT;
                    float ye = floorf (modcoord [1]) + LANCZOS_SUPPORT;
                    int dsrc = img->width - (xe - xs) - 1;
                    int r = 0;
                    if (xs >= 0 && ys >= 0 && xe < img->width && ye < img->height)
                    {
                        dcraw_image_type *src = img->image +
                            (((long)ys) * img->width + ((long)xs));

                        float norm = 0.0;
                        float sum = 0.0;

                        float _dx = modcoord [0] - xs;
                        float dy = modcoord [1] - ys;
                        for (; ys <= ye; ys += 1.0, dy -= 1.0)
                        {
                            float xc, dx = _dx;
                            for (xc = xs; xc <= xe; xc += 1.0, dx -= 1.0, src++)
                            {
                                float d = dx * dx + dy * dy;
                                if (d >= LANCZOS_SUPPORT * LANCZOS_SUPPORT)
                                    continue;

                                d = lanczos_func [(int)(d * LANCZOS_TABLE_RES)];
                                norm += d;
                                sum += d * src [0][c];
                            }
                            src += dsrc;
                        }

                        if (norm != 0.0)
                        {
                            r = (int)sum / norm;
                            if (r > 0xffff)
                                r = 0xffff;
                            else if (r < 0)
                                r = 0;
                        }
                    }
                    cur [0][c] = r;
#else
                    /* Do it in integer arithmetic, it's faster */
                    int xx = (int)modcoord [0];
                    int yy = (int)modcoord [1];
                    int xs = xx + 1 - LANCZOS_SUPPORT;
                    int xe = xx     + LANCZOS_SUPPORT;
                    int ys = yy + 1 - LANCZOS_SUPPORT;
                    int ye = yy     + LANCZOS_SUPPORT;
                    int dsrc = img->width - (xe - xs) - 1;
                    int r = 0;
                    if (xs >= 0 && ys >= 0 && xe < img->width && ye < img->height)
                    {
                        dcraw_image_type *src = img->image + (ys * img->width + xs);

                        int norm = 0;
                        int sum = 0;

                        int _dx = (int)(modcoord [0] * 4096.0) - (xs << 12);
                        int dy = (int)(modcoord [1] * 4096.0) - (ys << 12);
                        for (; ys <= ye; ys++, dy -= 4096)
                        {
                            int xc, dx = _dx;
                            for (xc = xs; xc <= xe; xc++, src++, dx -= 4096)
                            {
                                int d = (dx * dx + dy * dy) >> 12;
                                if (d >= 4096 * LANCZOS_SUPPORT * LANCZOS_SUPPORT)
                                    continue;

                                d = lanczos_func [(d * LANCZOS_TABLE_RES) >> 12];
                                norm += d;
                                sum += d * src [0][c];
                            }
                            src += dsrc;
                        }

                        if (norm != 0)
                        {
                            r = sum / norm;
                            if (r > 0xffff)
                                r = 0xffff;
                            else if (r < 0)
                                r = 0;
                        }
                    }
                    cur [0][c] = r;
#endif
                }
        }

        /* Exchange the original image and the modified one */
        tmp = img->image;
        img->image = image2;
        image2 = tmp;

no_distortion:
        /* Intermediate buffers not needed anymore */
        g_free (buff);
        g_free (image2);
    }
}

#endif /* HAVE_LENSFUN */

/*
 * A pixel with a significantly larger value than all of its four direct
 * neighbours is considered "hot". It will be replaced by the maximum value
 * of its neighbours. For simplicity border pixels are not considered.
 *
 * Reasonable values for uf->conf->hotpixel are in the range 0.5-10.
 *
 * Note that the algorithm uses pixel values from previous (processed) and
 * next (unprocessed) row and whether or not pixels are marked may make a
 * difference for the hot pixel count.
 *
 * Cleanup issue:
 * -	change prototype to void x(ufraw_data *uf, UFRawPhase phase)
 * -	use ufraw_image_format()
 * -	use uf->rgbMax (check, must be about 64k)
 */
static void ufraw_shave_hotpixels(ufraw_data *uf, dcraw_image_type *img,
	int width, int height, int colors, unsigned rgbMax)
{
    int w, h, c, i, count;
    unsigned delta, t, v, hi;
    dcraw_image_type *p;

    uf->hotpixels = 0;
    if (uf->conf->hotpixel <= 0.0)
	return;
    delta = rgbMax / (uf->conf->hotpixel + 1.0);
    count = 0;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) default(none) \
  shared(uf,img,width,height,colors,rgbMax,delta) \
  reduction(+:count) \
  private(h,p,w,c,t,v,hi,i)
#endif
    for (h = 1; h < height - 1; ++h) {
	p = img + 1 + h * width;
	for (w = 1; w < width - 1; ++w, ++p) {
	    for (c = 0; c < colors; ++c) {
		t = p[0][c];
		if (t <= delta)
		    continue;
		t -= delta;
		v = p[-1][c];
		if (v > t)
		    continue;
		hi = v;
		v = p[1][c];
		if (v > t)
		    continue;
		if (v > hi)
		    hi = v;
		v = p[-width][c];
		if (v > t)
		    continue;
		if (v > hi)
		    hi = v;
		v = p[width][c];
		if (v > t)
		    continue;
		if (v > hi)
		    hi = v;
		/* mark the pixel using the original hot value */
		if (uf->mark_hotpixels) {
		    for (i = -10; i >= -20 && w + i >= 0; --i)
			memcpy(p[i], p[0], sizeof (p[i]));
		    for (i = 10; i <= 20 && w + i < width; ++i)
			memcpy(p[i], p[0], sizeof (p[i]));
		}
		p[0][c] = hi;
		++count;
	    }
	}
    }
    uf->hotpixels = count;
}

static void ufraw_convertshrink(ufraw_data *uf, dcraw_image_data *final, dcraw_data *raw)
{
    int scale = 1;

    /* We can do a simple interpolation in the following cases:
     * We shrink by an integer value.
     * If pixel_aspect<1 (e.g. NIKON D1X) shrink must be at least 4. */
    if (uf->conf->size==0 && uf->conf->shrink>1) {
	scale = uf->conf->shrink * MIN(raw->pixel_aspect, 1/raw->pixel_aspect);
    } else if (uf->conf->interpolation==half_interpolation) {
	scale = 2;
    /* Wanted size is smaller than raw size (size is after a raw->shrink)
     * (assuming there are filters). */
    } else if (uf->conf->size>0 && uf->HaveFilters) {
	int cropHeight = uf->conf->CropY2 - uf->conf->CropY1;
	int cropWidth = uf->conf->CropX2 - uf->conf->CropX1;
	int cropSize = MAX(cropHeight, cropWidth);
	if (cropSize/uf->conf->size >= 2)
	    scale = 2;
    }

    if (uf->HaveFilters && scale == 1)
	dcraw_finalize_interpolate(final, raw, uf->conf->interpolation,
	    uf->conf->smoothing);
    else
	dcraw_finalize_shrink(final, raw, scale);

    dcraw_image_stretch(final, raw->pixel_aspect);
    if (uf->conf->size==0 && uf->conf->shrink>1) {
	dcraw_image_resize(final,
	    scale * MAX(final->height, final->width)/uf->conf->shrink);
    }
    if (uf->conf->size>0) {
	int cropHeight = uf->conf->CropY2 - uf->conf->CropY1;
	int cropWidth = uf->conf->CropX2 - uf->conf->CropX1;
	int cropSize = MAX(cropHeight, cropWidth);
	if ( uf->conf->size > cropSize ) {
	    ufraw_message(UFRAW_ERROR, _("Can not downsize from %d to %d."),
		    cropSize, uf->conf->size);
	} else {
	    /* uf->conf->size holds the size of the cropped image.
	     * We need to calculate from it the desired size of
	     * the uncropped image. */
	    int finalSize = scale * MAX(final->height, final->width);
	    dcraw_image_resize(final, uf->conf->size * finalSize / cropSize);
	}
    }
}

/*
 * Interface of ufraw_shave_hotpixels(), dcraw_finalize_raw() and preferably
 * dcraw_wavelet_denoise() too should change to accept a phase argument and
 * no longer require type casts.
 */
void ufraw_convert_image_raw(ufraw_data *uf, UFRawPhase phase)
{
    ufraw_image_data *img = &uf->Images[phase];
    dcraw_data *dark = uf->conf->darkframe ? uf->conf->darkframe->raw : NULL;
    dcraw_data *raw = uf->raw;
    dcraw_image_type *rawimage;

    ufraw_convert_import_buffer(uf, phase, &raw->raw);
    img->rgbg = raw->raw.colors == 4;
    ufraw_shave_hotpixels(uf, (dcraw_image_type *)(img->buffer), img->width,
	    img->height, raw->raw.colors, raw->rgbMax);
    rawimage = raw->raw.image;
    raw->raw.image = (dcraw_image_type *)img->buffer;
    /* The threshold is scaled for compatibility */
    dcraw_wavelet_denoise(raw, uf->conf->threshold * sqrt(uf->raw_multiplier));
    dcraw_finalize_raw(raw, dark, uf->developer->rgbWB);
    raw->raw.image = rawimage;
}

/*
 * Interface of ufraw_convertshrink() and dcraw_flip_image() should change
 * to accept a phase argument and no longer require type casts.
 */
void ufraw_convert_image_first(ufraw_data *uf, UFRawPhase phase)
{
    ufraw_image_data *in = &uf->Images[phase - 1];
    ufraw_image_data *out = &uf->Images[phase];
    dcraw_data *raw = uf->raw;
    dcraw_image_type *rawimage;
    dcraw_image_data final;

    final.image = (image_type *)out->buffer;
    final.width = out->width;
    final.height = out->height;

    rawimage = raw->raw.image;
    raw->raw.image = (dcraw_image_type *)in->buffer;
    ufraw_convertshrink(uf, &final, raw);
    raw->raw.image = rawimage;
    dcraw_flip_image(&final, uf->conf->orientation);

    out->height = final.height;
    out->width = final.width;
    out->depth = sizeof (dcraw_image_type);
    out->rowstride = out->width * out->depth;
    out->buffer = (guint8 *)final.image;

    ufraw_convert_reverse_wb(uf, phase);
}

static void ufraw_convert_reverse_wb(ufraw_data *uf, UFRawPhase phase)
{
    ufraw_image_data *img = &uf->Images[phase];
    guint32 mul[3], px;
    guint16 *p16;
    int i, size, c;

    ufraw_image_format(NULL, NULL, img, "6", G_STRFUNC);
    /* The speedup trick is to keep the non-constant (or ugly constant)
     * divider out of the pixel iteration. If you really have to then
     * use double division (can be much faster, apparently). */
    for (i = 0; i < 3; ++i)
	mul[i] = (guint64)0x10000 * 0x10000 / uf->developer->rgbWB[i];
    size = img->height * img->width;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) default(none) \
  shared(uf,phase,img,mul,size) \
  private(i,p16,c,px)
#endif
    for (i = 0; i < size; ++i) {
	p16 = (guint16 *)&img->buffer[i * img->depth];
	for (c = 0; c < 3; ++c) {
	    px = p16[c] * (guint64)mul[c] / 0x10000;
	    if (px > 0xffff)
		px = 0xffff;
	    p16[c] = px;
	}
    }
}

static void ufraw_convert_image_lensfun(ufraw_data *uf, UFRawPhase phase)
{
#ifdef HAVE_LENSFUN
    conf_data *conf = uf->conf;
    ufraw_image_data *img = &uf->Images[phase];
    lfModifier *modifier;
    float real_scale;
    int finmodflags;

    if (conf->camera == NULL || conf->lens == NULL)
	return;

    modifier = lf_modifier_new(conf->lens, conf->camera->CropFactor,
	    img->width, img->height);
    if (modifier == NULL)
	return;

    real_scale = pow(2.0, conf->lens_scale);
    finmodflags = lf_modifier_initialize(modifier, conf->lens, LF_PF_U16,
	    conf->focal_len, conf->aperture, conf->subject_distance,
	    real_scale, conf->cur_lens_type, UF_LF_ALL, FALSE);
    if (finmodflags & UF_LF_ALL) {
	dcraw_image_data args;
	args.image = (image_type *)img->buffer;
	args.width = img->width;
	args.height = img->height;
	ufraw_lensfun_modify(&args, modifier, finmodflags);
	img->buffer = (guint8 *)args.image;
    }
    lf_modifier_destroy(modifier);
#else
    (void)uf;
    (void)phase;
#endif /* HAVE_LENSFUN */
}

static void ufraw_convert_import_buffer(ufraw_data *uf, UFRawPhase phase, dcraw_image_data *dcimg)
{
    ufraw_image_data *img = &uf->Images[phase];

    img->height = dcimg->height;
    img->width = dcimg->width;
    img->depth = sizeof (dcraw_image_type);
    img->rowstride = img->width * img->depth;
    if (img->buffer)
	g_free(img->buffer);
    img->buffer = g_memdup(dcimg->image, img->height * img->rowstride);
}

/*
 * This function does not set img->invalidate_event because the
 * invalidation here is a secondary effect of the need to resize
 * buffers. The invalidate events should all have been set already.
 */
static void ufraw_convert_prepare_buffers(ufraw_data *uf)
{
    ufraw_image_data *FirstImage = &uf->Images[ufraw_first_phase];
    ufraw_image_data *img;

    /* Development image layer */
    img = &uf->Images[ufraw_develop_phase];
    if (img->height != FirstImage->height ||
        img->width != FirstImage->width ||
        !img->buffer)
        img->valid = 0;

    img->height = FirstImage->height;
    img->width = FirstImage->width;
    img->depth = 3;
    img->rowstride = img->width * img->depth;
    img->buffer = g_realloc(img->buffer, img->height * img->rowstride);

#ifdef HAVE_LENSFUN
    /* Postprocessing image layer */
    img = &uf->Images[ufraw_lensfun_phase];
    if (img->height != FirstImage->height ||
        img->width != FirstImage->width ||
        !img->buffer)
        img->valid = 0;

    img->height = FirstImage->height;
    img->width = FirstImage->width;
    img->depth = 3;
    img->rowstride = img->width * img->depth;
    /* ignore uf->modifier to avoid a call dependency */
    img->buffer = g_realloc(img->buffer, img->height * img->rowstride);
#endif /* HAVE_LENSFUN */
}

#ifdef HAVE_LENSFUN
int ufraw_prepare_lensfun(ufraw_data *uf)
{
    ufraw_image_data *img = &uf->Images[ufraw_lensfun_phase];
    conf_data *conf = uf->conf;
    float real_scale;

    if (uf->modifier)
        lf_modifier_destroy(uf->modifier);
    uf->modifier = lf_modifier_new(conf->lens, conf->camera->CropFactor,
	    img->width, img->height);

    real_scale = pow(2.0, conf->lens_scale);
    uf->postproc_ops = lf_modifier_initialize(uf->modifier, conf->lens,
	    LF_PF_U8, conf->focal_len, conf->aperture, conf->subject_distance,
	    real_scale, conf->cur_lens_type, UF_LF_ALL, FALSE);
    if (!(uf->postproc_ops & UF_LF_ALL)) {
        lf_modifier_destroy(uf->modifier);
        uf->modifier = NULL;
        img->valid = 0;
    }
    return UFRAW_SUCCESS;
}
#endif /* HAVE_LENSFUN */

/*
 * This function is very permissive in accepting NULL pointers but it does
 * so to make it easy to call this function: consider it documentation with
 * a free consistency check. It is not necessarily good to change existing
 * algorithms all over the place to accept more image formats: replacing
 * constants by variables may turn off some compiler optimizations.
 */
void ufraw_image_format(int *colors, int *bytes, ufraw_image_data *img,
	const char *formats, const char *caller)
{
    int b, c;

    switch (img->depth) {
    case 3:
	    c = 3;
	    b = 1;
	    break;
    case 4:
	    c = img->rgbg ? 4 : 3;
	    b = 1;
	    break;
    case 6:
	    c = 3;
	    b = 2;
	    break;
    case 8:
	    c = img->rgbg ? 4 : 3;
	    b = 2;
	    break;
    default:
	    g_error("%s -> %s: unsupported depth %d\n", caller, G_STRFUNC, img->depth);
    }
    if (!strchr(formats, '0' + c * b))
	g_error("%s: unsupported depth %d (rgbg=%d)\n", caller, img->depth, img->rgbg);
    if (colors)
	*colors = c;
    if (bytes)
	*bytes = b;
}

/*
 * Some algorithms need access to the RGB data before color adjustments
 * such as white-balance, hue, saturation have been applied.
 */
ufraw_image_data *ufraw_rgb_image(ufraw_data *uf, gboolean bufferok,
	const char *dbg)
{
    const UFRawPhase phase = ufraw_first_phase;
    int i;

    if (dbg && uf->Images[phase].valid != (long)0xffffffff)
	g_warning("%s->%s: conversion necessary (suboptimal).\n", dbg,
		G_STRFUNC);
    for (i = 0; i < 32; ++i) {
	ufraw_convert_image_area(uf, i, phase);
	if (!bufferok) {
	    /* the first tile should yield the image dimensions already */
	    break;
	}
    }
    return &uf->Images[ufraw_first_phase];
}

ufraw_image_data *ufraw_final_image(ufraw_data *uf, gboolean bufferok)
{
    UFRawPhase phase;
    int i;

    phase = ufraw_develop_phase;
#ifdef HAVE_LENSFUN
    if (uf->modifier &&
	(uf->postproc_ops & (UF_LF_ALL & ~LF_MODIFY_VIGNETTING)))
	phase = ufraw_lensfun_phase;
#endif /* HAVE_LENSFUN */
    if (bufferok) {
	    /* It should never be necessary to actually finish the conversion
	     * because it can break render_preview_image() which uses the
	     * final image "valid" mask for deciding what to update in the
	     * pixbuf. That can be fixed but is suboptimal anyway. The best
	     * we can do is print a warning in case we need to finish the
	     * conversion and finish it here. */
	if (uf->Images[phase].valid != (long)0xffffffff) {
	    g_warning("%s: fixing unfinished conversion.\n", G_STRFUNC);
	    for (i = 0; i < 32; ++i)
		ufraw_convert_image_area(uf, i, phase);
	}
    } else {
	if (uf->Images[phase].valid == 0) {
	    g_warning("%s: starting conversion.\n", G_STRFUNC);
	    /* this will update all buffer sizes (e.g. due to rotate) */
	    ufraw_convert_image_area(uf, 0, ufraw_first_phase);
	}
    }
    return &uf->Images[phase];
}

ufraw_image_data *ufraw_convert_image_area(ufraw_data *uf, unsigned saidx,
	UFRawPhase phase)
{
    int x = 0, y = 0, w = 0, h = 0;
    ufraw_image_data *out = &uf->Images [phase];

    if (out->valid & (1 << saidx))
        return out; // the subarea has been already computed

    /* Get the subarea image for previous phase */
    ufraw_image_data *in = NULL;
    if (phase > ufraw_raw_phase)
	in = ufraw_convert_image_area(uf, saidx, phase - 1);

    /* Get subarea coordinates */
    ufraw_img_get_subarea_coord(out, saidx, &x, &y, &w, &h);

    switch (phase)
    {
        case ufraw_raw_phase:
	    if (out->valid != (long)0xffffffff)
	    {
		if (out->producer)
		    out->producer(uf, phase);
		else
		    ufraw_convert_image_raw(uf, phase);
		out->valid = 0xffffffff;
	    }
	    return out;

        case ufraw_first_phase:
	    if (out->valid != (long)0xffffffff)
	    {
		if (out->producer)
		    out->producer(uf, phase);
		else
		    ufraw_convert_image_first(uf, phase);
		ufraw_convert_prepare_buffers(uf);
		out->valid = 0xffffffff;
	    }
	    return out;

        case ufraw_develop_phase:
            {
                int yy;
                for (yy = y; yy < y + h; yy++)
                {
                    guint8 *dest = out->buffer + (yy * out->width + x) * out->depth;
                    develop(
                        dest, (void *)(in->buffer + (yy * in->width + x) * in->depth),
                        uf->developer, 8, w);

#ifdef HAVE_LENSFUN
                    if (uf->modifier &&
                        (uf->postproc_ops & LF_MODIFY_VIGNETTING))
                    {
                        /* Apply de-vignetting filter here, to avoid
                         * creating a separate pass for it which will
                         * take a lot of memory ... */
                        lf_modifier_apply_color_modification (
                            uf->modifier, dest, x, yy, w, 1,
                            LF_CR_3 (RED, GREEN, BLUE),
                            out->width * out->depth);
                    }
#endif /* HAVE_LENSFUN */
                }
            }
            break;

        case ufraw_lensfun_phase:
#ifdef HAVE_LENSFUN
            {
                if (!uf->modifier ||
                    !(uf->postproc_ops & (UF_LF_ALL & ~LF_MODIFY_VIGNETTING))) {
		    out->valid = in->valid;	/* for invalidate_event */
		    return in;
		}

                int yy;
                float *buff = g_new (float, (w < 8) ? 8 * 2 * 3 : w * 2 * 3);

                // Compute the previous stage subareas, if needed
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x, y, 1, 1, buff);
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x + w/2, y, 1, 1, buff + 2 * 3);
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x + w-1, y, 1, 1, buff + 4 * 3);
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x, y + h/2, 1, 1, buff + 6 * 3);
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x + w-1, y + h/2, 1, 1, buff + 8 * 3);
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x, y + h-1, 1, 1, buff + 10 * 3);
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x + w/2, y + h-1, 1, 1, buff + 12 * 3);
                lf_modifier_apply_subpixel_geometry_distortion (
                    uf->modifier, x + w-1, y + h-1, 1, 1, buff + 14 * 3);

                for (yy = 0; yy < 8 * 2 * 3; yy += 2)
                {
                    int idx = ufraw_img_get_subarea_idx (in, buff [yy], buff [yy + 1]);
                    if (idx <= 31)
                        ufraw_convert_image_area (uf, idx, phase - 1);
                }

                for (yy = 0; yy < h; yy++)
                {
                    if (!lf_modifier_apply_subpixel_geometry_distortion (
                        uf->modifier, x, y + yy, w, 1, buff))
                        break; // huh?? should never happen

                    guint8 *out_buf = out->buffer + (y + yy) * out->rowstride + x * out->depth;

                    float *cur, *end = buff + w * 2 * 3;
                    for (cur = buff; cur < end; cur += 2 * 3, out_buf += out->depth)
                    {
                        // roundf() and truncf() are C99 and gcc warns on them
                        // if used without -std=c99... oh well...
                        int xi = cur [0] + 0.5;
                        int yi = cur [1] + 0.5;
                        if (xi<0 || xi >= in->width || yi<0 || yi >= in->height)
                            out_buf [0] = 0;
                        else
                            /* Nearest interpolation: don't spend time
                               for high-quality interpolation for preview */
                            out_buf [0] = (in->buffer + yi * in->rowstride +
                                           xi * in->depth) [0];

                        xi = cur [2] + 0.5;
                        yi = cur [3] + 0.5;
                        if (xi<0 || xi >= in->width || yi<0 || yi >= in->height)
                            out_buf [1] = 0;
                        else
                            /* Nearest interpolation: don't spend time
                               for high-quality interpolation for preview */
                            out_buf [1] = (in->buffer + yi * in->rowstride +
                                           xi * in->depth) [1];

                        xi = cur [4] + 0.5;
                        yi = cur [5] + 0.5;
                        if (xi<0 || xi >= in->width || yi<0 || yi >= in->height)
                            out_buf [2] = 0;
                        else
                            /* Nearest interpolation: don't spend time
                               for high-quality interpolation for preview */
                            out_buf [2] = (in->buffer + yi * in->rowstride +
                                           xi * in->depth) [2];
                    }
                }

                g_free (buff);
            }
            break;
#else /* HAVE_LENSFUN */
            return in;
#endif /* HAVE_LENSFUN */

        default:
	    g_warning("%s: invalid phase %d\n", G_STRFUNC, phase);
            return in;
    }

#ifdef _OPENMP
#pragma omp critical
#endif
    // Mark the subarea as valid
    out->valid |= (1 << saidx);

    return out;
}

static int ufraw_flip_image_buffer(ufraw_image_data *img, int flip)
{
    /* Following code was copied from dcraw's flip_image()
     * and modified to work with any pixel depth. */
    int base, dest, next, row, col;
    guint8 *image = img->buffer;
    int height = img->height;
    int width = img->width;
    int depth = img->depth;
    int size = height * width;
    guint8 hold[8];
    unsigned *flag = g_new0(unsigned, (size+31) >> 5);
    for (base = 0; base < size; base++) {
	if (flag[base >> 5] & (1 << (base & 31)))
	    continue;
	dest = base;
	memcpy(hold, image+base*depth, depth);
	while (1) {
	    if (flip & 4) {
		row = dest % height;
		col = dest / height;
	    } else {
		row = dest / width;
		col = dest % width;
	    }
	    if (flip & 2)
		row = height - 1 - row;
	    if (flip & 1)
		col = width - 1 - col;
	    next = row * width + col;
	    if (next == base) break;
	    flag[next >> 5] |= 1 << (next & 31);
	    memcpy(image+dest*depth, image+next*depth, depth);
	    dest = next;
	}
	memcpy(image+dest*depth, hold, depth);
    }
    g_free (flag);
    if (flip & 4) {
	img->height = width;
	img->width = height;
	img->rowstride = height * depth;
    }
    return UFRAW_SUCCESS;
}

static void ufraw_flip_orientation(ufraw_data *uf, int flip)
{
    const char flipMatrix[8][8] = {
	{ 0, 1, 2, 3, 4, 5, 6, 7 }, /* No flip */
	{ 1, 0, 3, 2, 5, 4, 7, 6 }, /* Flip horizontal */
	{ 2, 3, 0, 1, 6, 7, 4, 5 }, /* Flip vertical */
	{ 3, 2, 1, 0, 7, 6, 5, 4 }, /* Rotate 180 */
	{ 4, 6, 5, 7, 0, 2, 1, 3 }, /* Flip over diagonal "\" */
	{ 5, 7, 4, 6, 1, 3, 0, 2 }, /* Rotate 270 */
	{ 6, 4, 7, 5, 2, 0, 3, 1 }, /* Rotate 90 */
	{ 7, 5, 6, 4, 3, 1, 2, 0 }  /* Flip over diagonal "/" */
    };
    uf->conf->orientation = flipMatrix[uf->conf->orientation][flip];
}

/*
 * Normalize arbitrary rotations into a 0..90 degree range.
 */
void ufraw_normalize_rotation(ufraw_data *uf)
{
    int angle, flip = 0;

    uf->conf->rotationAngle = fmod(uf->conf->rotationAngle, 360.0);
    if (uf->conf->rotationAngle < 0.0)
	uf->conf->rotationAngle += 360.0;
    angle = floor(uf->conf->rotationAngle / 90) * 90;
    switch (angle) {
	case  90: flip = 6; break;
	case 180: flip = 3; break;
	case 270: flip = 5; break;
    }
    ufraw_flip_orientation(uf, flip);
    uf->conf->rotationAngle -= angle;
}

/*
 * Unnormalize a normalized rotaion into a -180..180 degree range,
 * while orientation can be either 0 (normal) or 1 (flipped).
 * All image processing code assumes normalized rotation, therefore
 * each call to ufraw_unnormalize_rotation() must be followed by a call
 * to ufraw_normalize_rotation().
 */
void ufraw_unnormalize_rotation(ufraw_data *uf)
{
    switch (uf->conf->orientation) {
    case 5: /* Rotate 270 */
	uf->conf->rotationAngle += 90;
    case 3: /* Rotate 180 */
	uf->conf->rotationAngle += 90;
    case 6: /* Rotate 90 */
	uf->conf->rotationAngle += 90;
	uf->conf->orientation = 0;
    case 0: /* No flip */
	break;
    case 4: /* Flip over diagonal "\" */
	uf->conf->rotationAngle += 90;
    case 2: /* Flip vertical */
	uf->conf->rotationAngle += 90;
    case 7: /* Flip over diagonal "/" */
	uf->conf->rotationAngle += 90;
	uf->conf->orientation = 1;
    case 1: /* Flip horizontal */
	break;
    default:
	g_error("ufraw_unnormalized_roation(): orientation=%d out of range",
		uf->conf->orientation);
    }
    uf->conf->rotationAngle = remainder(uf->conf->rotationAngle, 360.0);
}

int ufraw_flip_image(ufraw_data *uf, int flip)
{
    ufraw_flip_orientation(uf, flip);
    /* Usually orientation is applied before rotationAngle.
     * Here we are flipping after rotationAngle was applied.
     * We need to correct rotationAngle for this since these
     * operations do no commute.
     */
    if (flip==1 || flip==2 || flip==4 || flip==7) {
	uf->conf->rotationAngle = -uf->conf->rotationAngle;
	ufraw_normalize_rotation(uf);
    }
    ufraw_flip_image_buffer(&uf->Images[ufraw_first_phase], flip);
    ufraw_flip_image_buffer(&uf->Images[ufraw_develop_phase], flip);
#ifdef HAVE_LENSFUN
    if (uf->modifier &&
	(uf->postproc_ops & (UF_LF_ALL & ~LF_MODIFY_VIGNETTING)))
	ufraw_flip_image_buffer(&uf->Images[ufraw_lensfun_phase], flip);
#endif /* HAVE_LENSFUN */

    return UFRAW_SUCCESS;
}

void ufraw_invalidate_layer(ufraw_data *uf, UFRawPhase phase)
{
    for (; phase < ufraw_phases_num; phase++) {
	if (uf->Images[phase].valid != 0) {
	    uf->Images[phase].valid = 0;
	    uf->Images[phase].invalidate_event = TRUE;
	}
    }
}

void ufraw_invalidate_hotpixel_layer(ufraw_data *uf)
{
    ufraw_invalidate_layer(uf, ufraw_raw_phase);
}

void ufraw_invalidate_denoise_layer(ufraw_data *uf)
{
    ufraw_invalidate_layer(uf, ufraw_raw_phase);
}

void ufraw_invalidate_darkframe_layer(ufraw_data *uf)
{
    ufraw_invalidate_layer(uf, ufraw_raw_phase);
}

/*
 * This one is special. The raw layer applies WB in preparation for optimal
 * interpolation but the first layer undoes it for develop() et.al. So, the
 * first layer stays valid but all the others must be invalidated upon WB
 * adjustments.
 */
void ufraw_invalidate_whitebalance_layer(ufraw_data *uf)
{
    ufraw_invalidate_layer(uf, ufraw_develop_phase);
    if (uf->Images[ufraw_raw_phase].valid) {
	uf->Images[ufraw_raw_phase].valid = 0;
	uf->Images[ufraw_raw_phase].invalidate_event = TRUE;
    }
}

/*
 * This should be a no-op in case we don't interpolate but we don't care: the
 * delay will at least give the illusion that it matters. Color smoothing
 * implementation is a bit too simplistic.
 */
void ufraw_invalidate_smoothing_layer(ufraw_data *uf)
{
    ufraw_invalidate_layer(uf, ufraw_first_phase);
}

gboolean ufraw_invalidate_layer_event(ufraw_data *uf, UFRawPhase phase)
{
    gboolean ret;

    ret = uf->Images[phase].invalidate_event;
    uf->Images[phase].invalidate_event = FALSE;
    return ret;
}

int ufraw_set_wb(ufraw_data *uf)
{
    dcraw_data *raw = uf->raw;
    double rgbWB[3];
    int status, c, cc, i;

    ufraw_invalidate_whitebalance_layer(uf);

    /* For manual_wb we calculate chanMul from the temperature/green. */
    /* For all other it is the other way around. */
    if (!strcmp(uf->conf->wb, manual_wb)) {
	Temperature_to_RGB(uf->conf->temperature, rgbWB);
	rgbWB[1] = rgbWB[1] / uf->conf->green;
	/* Suppose we shot a white card at some temperature:
	 * rgbWB[3] = rgb_cam[3][4] * preMul[4] * camWhite[4]
	 * Now we want to make it white (1,1,1), so we replace preMul
	 * with chanMul, which is defined as:
	 * chanMul[4][4] = cam_rgb[4][3] * (1/rgbWB[3][3]) * rgb_cam[3][4]
	 *		* preMul[4][4]
	 * We "upgraded" preMul, chanMul and rgbWB to diagonal matrices.
	 * This allows for the manipulation:
	 * (1/chanMul)[4][4] = (1/preMul)[4][4] * cam_rgb[4][3] * rgbWB[3][3]
	 *		* rgb_cam[3][4]
	 * We use the fact that rgb_cam[3][4] * (1,1,1,1) = (1,1,1) and get:
	 * (1/chanMul)[4] = (1/preMul)[4][4] * cam_rgb[4][3] * rgbWB[3]
	 */
	if (uf->raw_color) {
	    /* If there is no color matrix it is simple */
	    for (c=0; c<3; c++) {
		uf->conf->chanMul[c] = raw->pre_mul[c] / rgbWB[c];
	    }
	} else {
	    for (c=0; c<raw->colors; c++) {
		double chanMulInv = 0;
		for (cc=0; cc<3; cc++)
		    chanMulInv += 1/raw->pre_mul[c] * raw->cam_rgb[c][cc]
			    * rgbWB[cc];
		uf->conf->chanMul[c] = 1/chanMulInv;
	    }
	}
	/* Normalize chanMul[] so that MIN(chanMul[]) will be 1.0 */
	double min = uf->conf->chanMul[0];
	for (c=1; c<raw->colors; c++)
	   if (uf->conf->chanMul[c] < min) min = uf->conf->chanMul[c];
	if (min==0.0) min = 1.0; /* should never happen, just to be safe */
	for (c=0; c<raw->colors; c++) uf->conf->chanMul[c] /= min;
	if (raw->colors<4) uf->conf->chanMul[3] = 0.0;
	uf->conf->WBTuning = 0;
	return UFRAW_SUCCESS;
    }
    if (!strcmp(uf->conf->wb, spot_wb)) {
	/* do nothing */
	uf->conf->WBTuning = 0;
    } else if ( !strcmp(uf->conf->wb, auto_wb) ) {
	int p;
	/* Build a raw channel histogram */
	image_type *histogram;
	histogram = g_new0(image_type, uf->rgbMax+1);
	for (i=0; i<raw->raw.height*raw->raw.width; i++) {
	    gboolean countPixel = TRUE;
	    /* The -25 bound was copied from dcraw */
	    for (c=0; c<raw->raw.colors; c++)
		if (raw->raw.image[i][c] > uf->rgbMax+raw->black-25)
		    countPixel = FALSE;
	    if (countPixel) {
		for (c=0; c<raw->raw.colors; c++) {
		    p = MIN(MAX(raw->raw.image[i][c]-raw->black,0),uf->rgbMax);
		    histogram[p][c]++;
		}
	    }
	}
	for (c=0; c<uf->colors; c++) {
	    gint64 sum = 0;
	    for (i=0; i<uf->rgbMax+1; i++)
		sum += (gint64)i*histogram[i][c];
	    if (sum==0) uf->conf->chanMul[c] = 1.0;
	    else uf->conf->chanMul[c] = 1.0/sum;
	}
	g_free(histogram);
	uf->conf->WBTuning = 0;
    } else if ( !strcmp(uf->conf->wb, camera_wb) ) {
	if ( (status=dcraw_set_color_scale(raw,
		!strcmp(uf->conf->wb, auto_wb),
		!strcmp(uf->conf->wb, camera_wb)))!=DCRAW_SUCCESS ) {
	    if (status==DCRAW_NO_CAMERA_WB) {
		ufraw_message(UFRAW_BATCH_MESSAGE,
		    _("Cannot use camera white balance, "
		    "reverting to auto white balance."));
		ufraw_message(UFRAW_INTERACTIVE_MESSAGE,
		    _("Cannot use camera white balance, "
		    "reverting to auto white balance."));
		g_strlcpy(uf->conf->wb, auto_wb, max_name);
		return ufraw_set_wb(uf);
	    }
	    if (status!=DCRAW_SUCCESS)
		return status;
	}
	for (c=0; c<raw->colors; c++)
	    uf->conf->chanMul[c] = raw->post_mul[c];
	uf->conf->WBTuning = 0;
    } else {
	int lastTuning = -1;
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
	    if (!strcmp(uf->conf->wb, wb_preset[i].name) &&
		!strcmp(uf->conf->make, wb_preset[i].make) &&
		!strcmp(model, wb_preset[i].model) ) {
		if (uf->conf->WBTuning == wb_preset[i].tuning) {
		    for (c=0; c<raw->colors; c++)
			uf->conf->chanMul[c] = wb_preset[i].channel[c];
		    break;
		} else if (uf->conf->WBTuning < wb_preset[i].tuning) {
		    if (lastTuning == -1) {
			/* WBTuning was set to a value smaller than possible */
			uf->conf->WBTuning = wb_preset[i].tuning;
			for (c=0; c<raw->colors; c++)
			    uf->conf->chanMul[c] = wb_preset[i].channel[c];
			break;
		    } else {
			/* Extrapolate WB tuning values:
			 * f(x) = f(a) + (x-a)*(f(b)-f(a))/(b-a) */
			for (c=0; c<raw->colors; c++)
			    uf->conf->chanMul[c] = wb_preset[i].channel[c] +
				(uf->conf->WBTuning - wb_preset[i].tuning) *
				(wb_preset[lastTuning].channel[c] -
				 wb_preset[i].channel[c]) /
				(wb_preset[lastTuning].tuning -
				 wb_preset[i].tuning);
			break;
		    }
		} else if (uf->conf->WBTuning > wb_preset[i].tuning) {
			lastTuning = i;
		}
	    } else if (lastTuning!=-1) {
		/* WBTuning was set to a value larger than possible */
		uf->conf->WBTuning = wb_preset[lastTuning].tuning;
		for (c=0; c<raw->colors; c++)
		    uf->conf->chanMul[c] = wb_preset[lastTuning].channel[c];
		break;
	    }
	}
	if (i==wb_preset_count) {
	    if (lastTuning!=-1) {
		/* WBTuning was set to a value larger than possible */
		uf->conf->WBTuning = wb_preset[lastTuning].tuning;
		for (c=0; c<raw->colors; c++)
		    uf->conf->chanMul[c] = wb_preset[lastTuning].channel[c];
	    } else {
		g_strlcpy(uf->conf->wb, manual_wb, max_name);
		ufraw_set_wb(uf);
		return UFRAW_WARNING;
	    }
	}
    }
    /* Normalize chanMul[] so that MIN(chanMul[]) will be 1.0 */
    double min = uf->conf->chanMul[0];
    for (c=1; c<raw->colors; c++)
	if (uf->conf->chanMul[c] < min) min = uf->conf->chanMul[c];
    if (min==0.0) min = 1.0; /* should never happen, just to be safe */
    for (c=0; c<raw->colors; c++) uf->conf->chanMul[c] /= min;
    if (raw->colors<4) uf->conf->chanMul[3] = 0.0;

    /* (1/chanMul)[4] = (1/preMul)[4][4] * cam_rgb[4][3] * rgbWB[3]
     * Therefore:
     * rgbWB[3] = rgb_cam[3][4] * preMul[4][4] * (1/chanMul)[4]
     */
    if (uf->raw_color) {
	/* If there is no color matrix it is simple */
	for (c=0; c<3; c++) {
	    rgbWB[c] = raw->pre_mul[c] / uf->conf->chanMul[c];
	}
    } else {
	for (c=0; c<3; c++) {
	    rgbWB[c] = 0;
	    for (cc=0; cc<raw->colors; cc++)
		rgbWB[c] += raw->rgb_cam[c][cc] * raw->pre_mul[cc]
		    / uf->conf->chanMul[cc];
	}
    }
    /* From these values we calculate temperature, green values */
    RGB_to_Temperature(rgbWB, &uf->conf->temperature, &uf->conf->green);

    return UFRAW_SUCCESS;
}

static void ufraw_build_raw_histogram(ufraw_data *uf)
{
    int i, c;
    dcraw_data *raw = uf->raw;
    gboolean updateHistogram = FALSE;

    if (uf->RawHistogram==NULL) {
	uf->RawHistogram = g_new(int, uf->rgbMax+1);
	updateHistogram = TRUE;
    }
    double maxChan = 0;
    for (c=0; c<uf->colors; c++) maxChan = MAX(uf->conf->chanMul[c], maxChan);
    for (c=0; c<uf->colors; c++) {
	int tmp = floor(uf->conf->chanMul[c]/maxChan*0x10000);
	if (uf->RawChanMul[c]!=tmp) {
	    updateHistogram = TRUE;
	    uf->RawChanMul[c] = tmp;
	}
    }
    if (!updateHistogram) return;

    if (uf->colors==3) uf->RawChanMul[3] = uf->RawChanMul[1];
    memset(uf->RawHistogram, 0, (uf->rgbMax+1)*sizeof(int));
    int count = raw->raw.height*raw->raw.width;
    for (i=0; i<count; i++)
	for (c=0; c<raw->raw.colors; c++)
	    uf->RawHistogram[MIN(
		    (gint64)MAX(raw->raw.image[i][c]-raw->black, 0) *
		    uf->RawChanMul[c] / 0x10000, uf->rgbMax)]++;

    uf->RawCount = count * raw->raw.colors;
}

void ufraw_auto_expose(ufraw_data *uf)
{
    int sum, stop, wp, c, pMax, pMin, p;
    image_type pix;
    guint16 p16[3];

    if (uf->conf->autoExposure!=apply_state) return;

    /* Reset the exposure and luminosityCurve */
    uf->conf->exposure = 0;
    /* If we normalize the exposure then 0 EV also gets normalized */
    if ( uf->conf->ExposureNorm>0 )
	uf->conf->exposure = -log(1.0*uf->rgbMax/uf->conf->ExposureNorm)/log(2);
    ufraw_developer_prepare(uf, auto_developer);
    /* Find the grey value that gives 99% luminosity */
    double maxChan = 0;
    for (c=0; c<uf->colors; c++) maxChan = MAX(uf->conf->chanMul[c], maxChan);
    for (pMax=uf->rgbMax, pMin=0, p=(pMax+pMin)/2; pMin<pMax-1; p=(pMax+pMin)/2)
    {
	for (c=0; c<uf->colors; c++)
	    pix[c] = MIN (p * maxChan/uf->conf->chanMul[c], uf->rgbMax);
	develop(p16, pix, uf->AutoDeveloper, 16, 1);
	for (c=0, wp=0; c<3; c++) wp = MAX(wp, p16[c]);
	if (wp < 0x10000 * 99/100) pMin = p;
	else pMax = p;
    }
    /* set cutoff at 99% of the histogram */
    ufraw_build_raw_histogram(uf);
    stop = uf->RawCount * 1/100;
    /* Calculate the white point */
    for (wp=uf->rgbMax, sum=0; wp>1 && sum<stop; wp--)
	sum += uf->RawHistogram[wp];
    /* Set 99% of the luminosity values with luminosity below 99% */
    uf->conf->exposure = log((double)p/wp)/log(2);
    /* If we are going to normalize the exposure later,
     * we need to cancel its effect here. */
    if ( uf->conf->ExposureNorm>0 )
	uf->conf->exposure -=
		log(1.0*uf->rgbMax/uf->conf->ExposureNorm)/log(2);
    uf->conf->autoExposure = enabled_state;
//    ufraw_message(UFRAW_SET_LOG, "ufraw_auto_expose: "
//	    "Exposure %f (white point %d/%d)\n", uf->conf->exposure, wp, p);
}

void ufraw_auto_black(ufraw_data *uf)
{
    int sum, stop, bp, c;
    image_type pix;
    guint16 p16[3];

    if (uf->conf->autoBlack==disabled_state) return;

    /* Reset the luminosityCurve */
    ufraw_developer_prepare(uf, auto_developer);
    /* Calculate the black point */
    ufraw_build_raw_histogram(uf);
    stop = uf->RawCount/256/4;
    for (bp=0, sum=0; bp<uf->rgbMax && sum<stop; bp++)
	sum += uf->RawHistogram[bp];
    double maxChan = 0;
    for (c=0; c<uf->colors; c++) maxChan = MAX(uf->conf->chanMul[c], maxChan);
    for (c=0; c<uf->colors; c++)
	pix[c] = MIN (bp * maxChan/uf->conf->chanMul[c], uf->rgbMax);
    develop(p16, pix, uf->AutoDeveloper, 16, 1);
    for (c=0, bp=0; c<3; c++) bp = MAX(bp, p16[c]);

    CurveDataSetPoint(&uf->conf->curve[uf->conf->curveIndex],
	    0, (double)bp/0x10000, 0);

    uf->conf->autoBlack = enabled_state;
//    ufraw_message(UFRAW_SET_LOG, "ufraw_auto_black: "
//	    "Black %f (black point %d)\n",
//	    uf->conf->curve[uf->conf->curveIndex].m_anchors[0].x, bp);
}

/* ufraw_auto_curve sets the black-point and then distribute the (step-1)
 * parts of the histogram with the weights: w_i = pow(decay,i). */
void ufraw_auto_curve(ufraw_data *uf)
{
    int sum, stop, steps=8, bp, p, i, j, c;
    image_type pix;
    guint16 p16[3];
    CurveData *curve = &uf->conf->curve[uf->conf->curveIndex];
    double decay = 0.90;
    double norm = (1-pow(decay,steps))/(1-decay);

    CurveDataReset(curve);
    ufraw_developer_prepare(uf, auto_developer);
    /* Calculate curve points */
    ufraw_build_raw_histogram(uf);
    stop = uf->RawCount/256/4;
    double maxChan = 0;
    for (c=0; c<uf->colors; c++) maxChan = MAX(uf->conf->chanMul[c], maxChan);
    for (bp=0, sum=0, p=0, i=j=0; i<steps && bp<uf->rgbMax && p<0xFFFF; i++) {
	for (; bp<uf->rgbMax && sum<stop; bp++)
	    sum += uf->RawHistogram[bp];
	for (c=0; c<uf->colors; c++)
	    pix[c] = MIN (bp * maxChan/uf->conf->chanMul[c], uf->rgbMax);
	develop(p16, pix, uf->AutoDeveloper, 16, 1);
	for (c=0, p=0; c<3; c++) p = MAX(p, p16[c]);
	stop += uf->RawCount * pow(decay,i) / norm;
	/* Skip adding point if slope is too big (more than 4) */
	if (j>0 && p - curve->m_anchors[j-1].x*0x10000 < (i+1-j)*0x04000/steps)
	    continue;
	curve->m_anchors[j].x = (double)p/0x10000;
	curve->m_anchors[j].y = (double)i/steps;
	j++;
    }
    if (bp==0x10000) {
	curve->m_numAnchors = j;
    } else {
	curve->m_anchors[j].x = 1.0;
	/* The last point can be up to twice the height of a linear
	 * interpolation of the last two points */
	if (j>1) {
	    curve->m_anchors[j].y = curve->m_anchors[j-1].y +
		    2 * (1.0 - curve->m_anchors[j-1].x) *
		    (curve->m_anchors[j-1].y - curve->m_anchors[j-2].y) /
		    (curve->m_anchors[j-1].x - curve->m_anchors[j-2].x);
	    if (curve->m_anchors[j].y > 1.0) curve->m_anchors[j].y = 1.0;
	} else {
	    curve->m_anchors[j].y = 1.0;
	}
	curve->m_numAnchors = j+1;
    }
}

void ufraw_rotate_row(ufraw_image_data *image, void *pixbuf, double angle,
		      int bitDepth, int row, int offset, int width)
{
    int col, ur, uc, i, j, in_image;
    float r, c, fr, fc;
    guint16 (*input)[3] = (guint16 (*)[3]) image->buffer;
    guint16 (*iPix[2][2])[3];
    guint16 pixValue;
    guint8 (*oPix8)[3] = (guint8 (*)[3]) pixbuf;
    guint16 (*oPix16)[3] = (guint16 (*)[3]) pixbuf;
    double rotationRadians = (angle * 2 * M_PI) / 360;
    double sine = sin(rotationRadians);
    double cosine = cos(rotationRadians);
    guint16 bgcolor[3] = {0, 0, 0};

    for (col = 0; col < width; col++) {
	// Find co-ordinates of output pixel in input image:
	// c, r are the ideal subpixel co-ordinates.
	// uc, ur are the integer co-ordinates to the top and left of c, r.
	uc = (int)floor(c = row*sine + (offset+col)*cosine
			    - image->height*sine*cosine);
	ur = (int)floor(r = row*cosine -(offset+col)*sine
			    + image->height*sine*sine);
	// Differences between the ideal and integer co-ordinates, for weighting.
	fr = r - ur;
	fc = c - uc;
	// Whether this pixel is within the image at all. Set in next loop.
	in_image = 0;
	// Find pointers to the four pixels surrouding the ideal co-ordinate,
	// either from the input image or the background color.
	for (i = 0; i < 2; i++)
	    for (j = 0; j < 2; j++)
		if (((uc+i) >= 0) && ((uc+i) <= image->width - 1) &&
		    ((ur+j) >= 0) && ((ur+j) <= image->height - 1)) {
		    iPix[i][j] = &input[(ur+j)*image->width + uc+i];
		    in_image = 1;
		} else iPix[i][j] = &bgcolor;
	// Write output pixel.
	for (i = 0; i < 3; i++) {
	    if (in_image)
		pixValue = (guint16)(
		    ((*iPix[0][0])[i]*(1-fc) + (*iPix[1][0])[i]*fc) * (1-fr)
		  + ((*iPix[0][1])[i]*(1-fc) + (*iPix[1][1])[i]*fc) * fr);
	    else
		// Shortcut some floating point work if outside image.
		pixValue = bgcolor[i];

	    if (bitDepth > 8)
		oPix16[col][i] = pixValue;
	    else
		oPix8[col][i] = pixValue >> 8;
	}
    }
}

/*
 * Rotate an 8 or 16 bits per color image 0-90 degrees. The output pixel is
 * calculated as a weighted average of the 4 nearest input pixels.
 */
void ufraw_rotate_image_buffer(ufraw_image_data *img, double angle)
{
    double sine, cosine, movecol, moverow;
    int oldwidth, oldheight, oldrowstride;
    int width, height, depth, rowstride;
    int col, row, uc, ur, i;
    float c, r, fc, fr;
    guint8 *in, *in_00, *in_01, *in_10, *in_11, *out;

    if (!angle)
	return;
    ufraw_image_format(NULL, NULL, img, "36", G_STRFUNC);
    sine = sin(angle * 2 * M_PI / 360);
    cosine = cos(angle * 2 * M_PI / 360);
    depth = img->depth;
    oldwidth = img->width;
    oldheight = img->height;
    oldrowstride = img->rowstride;
    width = ceil((oldheight * sine) + (oldwidth * cosine));
    height = ceil((oldwidth * sine) + (oldheight * cosine));
    rowstride = width * depth;
    in = img->buffer;
    out = g_new0(guint8, height * rowstride);
    img->buffer = out;
    img->width = width;
    img->height = height;
    img->rowstride = rowstride;
    movecol = -oldheight * sine * cosine;
    moverow = oldheight * sine * sine;
    for (row = 0; row < height; ++row) {
	for (col = 0; col < width; col++) {
	    // (c,r) are the float input subpixel coordinates. fc,fr are the
	    // weighting factors for integer coordinates (uc,ur)..(uc+1,ur+1)
	    // around (c,r)
	    c = (col * cosine) + (row * sine) + movecol;
	    r = (row * cosine) - (col * sine) + moverow;
	    uc = (int)floor(c);
	    ur = (int)floor(r);
	    if (uc >= 0 && uc + 1 < oldwidth && ur >= 0 && ur + 1 < oldheight) {
		fc = c - uc;
		fr = r - ur;
		in_00 = in + ((ur * oldwidth) + uc) * depth;	// (uc,ur)
		in_01 = in_00 + depth;				// (uc+1,ur)
		in_10 = in_00 + oldrowstride;			// (uc,ur+1)
		in_11 = in_10 + depth;				// (uc+1,ur+1)
		if (depth > 4) {
		    for (i = 0; i < 3; ++i) {
			((guint16 *)out)[i] =
			    (((guint16 *)in_00)[i] * (1 - fc) + ((guint16 *)in_01)[i] * fc) * (1 - fr) +
			    (((guint16 *)in_10)[i] * (1 - fc) + ((guint16 *)in_11)[i] * fc) * fr;
		    }
		} else {
		    for (i = 0; i < 3; ++i) {
			out[i] =
			    (in_00[i] * (1 - fc) + in_01[i] * fc) * (1 - fr) +
			    (in_10[i] * (1 - fc) + in_11[i] * fc) * fr;
		    }
		}
	    }
	    out += depth;
	}
    }
    g_free(in);
}
