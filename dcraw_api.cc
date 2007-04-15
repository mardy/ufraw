/*
 * UFRaw - Unidentified Flying Raw converter for digital camera images
 *
 * dcraw_api.cc - API for DCRaw
 * Copyright 2004-2007 by Udi Fuchs
 *
 * based on dcraw by Dave Coffin
 * http://www.cybercom.net/~dcoffin/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation. You should have received
 * a copy of the license along with this program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h> /* for sqrt() */
#include <setjmp.h>
#include <errno.h>
#include <float.h>
#include <glib.h>
#include <glib/gi18n.h> /*For _(String) definition - NKBJ*/
#include "dcraw_api.h"
#include "dcraw.h"

#define FORCC for (c=0; c < colors; c++)
#define FC(filters,row,col) \
    (filters >> ((((row) << 1 & 14) + ((col) & 1)) << 1) & 3)
extern "C" {
int fc_INDI (const unsigned filters, const int row, const int col);
void wavelet_denoise_INDI(gushort (*image)[4], const int black,
    const int iheight, const int iwidth, const int height, const int width,
    const int colors, const int shrink, float pre_mul[4],
    const float threshold, const unsigned filters, void *dcraw);
void scale_colors_INDI(gushort (*image)[4], const int maximum, const int black,
    const int use_auto_wb, const int use_camera_wb, const float cam_mul[4],
    const int iheight, const int iwidth, const int colors, float pre_mul[4],
    const unsigned filters, /*const*/ gushort white[8][8], const char *ifname,
    void *dcraw);
void lin_interpolate_INDI(gushort (*image)[4], const unsigned filters,
    const int width, const int height, const int colors, void *dcraw);
void vng_interpolate_INDI(gushort (*image)[4], const unsigned filters,
    const int width, const int height, const int colors, const int rgb_max,
    void *dcraw);
void ahd_interpolate_INDI(gushort (*image)[4], const unsigned filters,
    const int width, const int height, const int colors, float rgb_cam[3][4],
    void *dcraw);
void flip_image_INDI(gushort (*image)[4], int *height_p, int *width_p,
    const int flip);
void fuji_rotate_INDI(gushort (**image_p)[4], int *height_p, int *width_p,
    int *fuji_width_p, const int colors, const double step, void *dcraw);

int dcraw_open(dcraw_data *h,char *filename)
{
    DCRaw *d = new DCRaw;

    g_free(d->messageBuffer);
    d->messageBuffer = NULL;
    d->lastStatus = DCRAW_SUCCESS;
    d->verbose = 1;
    d->ifname = g_strdup(filename);
    if (setjmp(d->failure)) {
        d->dcraw_message(DCRAW_ERROR,_("Fatal internal error\n"));
        h->message = d->messageBuffer;
	delete d;
        return DCRAW_ERROR;
    }
    if (!(d->ifp = fopen (d->ifname, "rb"))) {
        d->dcraw_message(DCRAW_OPEN_ERROR,_("Cannot open file %s: %s\n"),
                filename, strerror(errno));
        g_free(d->ifname);
        h->message = d->messageBuffer;
	delete d;
        return DCRAW_OPEN_ERROR;
    }
    d->identify();
    /* We first check if dcraw recognizes the file, this is equivalent
     * to 'dcraw -i' succeeding */
    if (!d->make[0]) {
	d->dcraw_message(DCRAW_OPEN_ERROR,_("%s: unsupported file format.\n"),
		d->ifname);
        fclose(d->ifp);
        g_free(d->ifname);
        h->message = d->messageBuffer;
	int lastStatus = d->lastStatus;
	delete d;
        return lastStatus;
    }
    /* Next we check if dcraw can decode the file */
    if (!d->is_raw) {
	d->dcraw_message(DCRAW_OPEN_ERROR,_("Cannot decode file %s\n"), d->ifname);
        fclose(d->ifp);
        g_free(d->ifname);
        h->message = d->messageBuffer;
	int lastStatus = d->lastStatus;
	delete d;
        return lastStatus;
    }
    if (d->load_raw == &DCRaw::kodak_ycbcr_load_raw) {
	d->height += d->height & 1;
	d->width += d->width & 1;
    }
    /* Pass class variables to the handler on two conditions:
     * 1. They are needed at this stage.
     * 2. They where set in identify() and won't change in load_raw() */
    h->dcraw = d;
    h->ifp = d->ifp;
    h->height = d->height;
    h->width = d->width;
    h->fuji_width = d->fuji_width;
    h->fuji_step = sqrt(0.5);
    h->colors = d->colors;
    h->filters = d->filters;
    h->raw_color = d->raw_color;
    h->shrink = d->shrink = (h->filters!=0);
    h->pixel_aspect = d->pixel_aspect;
    /* copied from dcraw's main() */
    switch ((d->flip+3600) % 360) {
        case 270: d->flip = 5; break;
        case 180: d->flip = 3; break;
        case  90: d->flip = 6;
    }
    h->flip = d->flip;
    h->toneCurveSize = d->tone_curve_size;
    h->toneCurveOffset = d->tone_curve_offset;
    h->toneModeOffset = d->tone_mode_offset;
    h->toneModeSize = d->tone_mode_size;
    g_strlcpy(h->make, d->make, 80);
    g_strlcpy(h->model, d->model, 80);
    h->iso_speed = d->iso_speed;
    h->shutter = d->shutter;
    h->aperture = d->aperture;
    h->focal_len = d->focal_len;
    h->timestamp = d->timestamp;
    h->raw.image = NULL;
    h->thumbType = unknown_thumb_type;
    h->message = d->messageBuffer;
    return d->lastStatus;
}

int dcraw_load_raw(dcraw_data *h)
{
    DCRaw *d = (DCRaw *)h->dcraw;
    int i, j;
    double dmin;

    g_free(d->messageBuffer);
    d->messageBuffer = NULL;
    d->lastStatus = DCRAW_SUCCESS;
    h->raw.height = d->iheight = (h->height+h->shrink) >> h->shrink;
    h->raw.width = d->iwidth = (h->width+h->shrink) >> h->shrink;
    h->raw.image = d->image = g_new0(dcraw_image_type, d->iheight * d->iwidth
	    + d->meta_length);
    d->meta_data = (char *) (d->image + d->iheight*d->iwidth);
    /* copied from the end of dcraw's identify() */
    if (d->filters && d->colors == 3) {
        for (i=0; i < 32; i+=4) {
            if ((d->filters >> i & 15) == 9) d->filters |= 2 << i;
            if ((d->filters >> i & 15) == 6) d->filters |= 8 << i;
        }
        d->colors++;
    }
    h->raw.colors = d->colors;
    h->fourColorFilters = d->filters;
    d->dcraw_message(DCRAW_VERBOSE,_("Loading %s %s image from %s ...\n"),
                d->make, d->model, d->ifname);
    fseek (d->ifp, d->data_offset, SEEK_SET);
    (d->*d->load_raw)();
    d->bad_pixels();
    if (d->is_foveon) {
        d->foveon_interpolate();
	h->raw.width = d->width;
	h->raw.height = d->height;
    }
    fclose(d->ifp);
    h->ifp = NULL;
    h->rgbMax = d->maximum;
    h->black = d->black;
    d->dcraw_message(DCRAW_VERBOSE,_("Black: %d, Maximum: %d\n"),
	    d->black, d->maximum);
    dmin = DBL_MAX;
    for (i=0; i<h->colors; i++) if (dmin > d->pre_mul[i]) dmin = d->pre_mul[i];
    for (i=0; i<h->colors; i++) h->pre_mul[i] = d->pre_mul[i]/dmin;
    if (h->colors==3) h->pre_mul[3] = 0;
    memcpy(h->cam_mul, d->cam_mul, sizeof d->cam_mul);
    memcpy(h->rgb_cam, d->rgb_cam, sizeof d->rgb_cam);

    double rgb_cam_transpose[4][3];
    for (i=0; i<4; i++) for (j=0; j<3; j++)
	rgb_cam_transpose[i][j] = d->rgb_cam[j][i];
    d->pseudoinverse (rgb_cam_transpose, h->cam_rgb, d->colors);

    h->message = d->messageBuffer;
    return d->lastStatus;
}

int dcraw_load_thumb(dcraw_data *h, dcraw_image_data *thumb)
{
    DCRaw *d = (DCRaw *)h->dcraw;

    g_free(d->messageBuffer);
    d->messageBuffer = NULL;
    d->lastStatus = DCRAW_SUCCESS;

    thumb->height = d->thumb_height;
    thumb->width = d->thumb_width;
    h->thumbOffset = d->thumb_offset;
    h->thumbBufferLength = d->thumb_length;
    if (d->thumb_offset==0) {
	dcraw_message(d, DCRAW_ERROR,_("%s has no thumbnail."), d->ifname);
    } else if (d->thumb_load_raw!=NULL) {
	dcraw_message(d, DCRAW_ERROR,
		_("Unsupported thumb format (load_raw) for %s"), d->ifname);
    } else if (d->write_thumb==&DCRaw::jpeg_thumb) {
	h->thumbType = jpeg_thumb_type;
    } else if (d->write_thumb==&DCRaw::ppm_thumb) {
	h->thumbType = ppm_thumb_type;
    } else {
	dcraw_message(d, DCRAW_ERROR,
		_("Unsupported thumb format for %s"), d->ifname);
    }
    h->message = d->messageBuffer;
    return d->lastStatus;
}

int dcraw_finalize_shrink(dcraw_image_data *f, dcraw_data *hh, int scale)
{
    DCRaw *d = (DCRaw *)hh->dcraw;
    int h, w, fujiWidth, r, c, ri, ci, cl, norm, s, recombine;
    int f4, sum[4], count[4];

    g_free(d->messageBuffer);
    d->messageBuffer = NULL;
    d->lastStatus = DCRAW_SUCCESS;

    recombine = ( hh->colors==3 && hh->raw.colors==4 );
    f->colors = hh->colors;

    /* hh->raw.image is shrunk in half if there are filters.
     * If scale is odd we need to "unshrink" it using the info in
     * hh->fourColorFilters before scaling it. */
    if (hh->filters!=0 && scale%2==1) {
        /* I'm skiping the last row/column if it is not a full row/column */
	f->height = h = hh->height / scale;
	f->width = w = hh->width / scale;
	fujiWidth = hh->fuji_width / scale;
	f->image = g_new0(dcraw_image_type, h * w);
	f4 = hh->fourColorFilters;
	for(r=0; r<h; r++) {
	    for(c=0; c<w; c++) {
		for (cl=0; cl<hh->raw.colors; cl++) sum[cl] = count[cl] = 0;
		for (ri=0; ri<scale; ri++)
		    for (ci=0; ci<scale; ci++) {
			sum[fc_INDI(f4, r*scale+ri, c*scale+ci)] +=
			    hh->raw.image
				[(r*scale+ri)/2*hh->raw.width+(c*scale+ci)/2]
				[fc_INDI(f4, r*scale+ri, c*scale+ci)];
			count[fc_INDI(f4, r*scale+ri, c*scale+ci)]++;
		    }
		for (cl=0; cl<hh->raw.colors; cl++)
		    f->image[r*w+c][cl] =
				MAX(sum[cl]/count[cl] - hh->black,0);
		if (recombine) f->image[r*w+c][1] =
			(f->image[r*w+c][1] + f->image[r*w+c][3])>>1;
	    }
	}
    } else {
	if (hh->filters!=0) scale /= 2;
        /* I'm skiping the last row/column if it is not a full row/column */
	f->height = h = hh->raw.height / scale;
	f->width = w = hh->raw.width / scale;
	fujiWidth = ( (hh->fuji_width+hh->shrink) >> hh->shrink ) / scale;
	f->image = g_new0(dcraw_image_type, h * w);
	norm = scale * scale;
	for(r=0; r<h; r++) {
	    for(c=0; c<w; c++) {
		for (cl=0; cl<hh->raw.colors; cl++) {
		    for (ri=0, s=0; ri<scale; ri++)
			for (ci=0; ci<scale; ci++)
			    s += hh->raw.image
				[(r*scale+ri)*hh->raw.width+c*scale+ci][cl];
                    f->image[r*w+c][cl] = MAX(s/norm - hh->black,0);
		}
		if (recombine) f->image[r*w+c][1] =
		    (f->image[r*w+c][1] + f->image[r*w+c][3])>>1;
	    }
	}
    }
    fuji_rotate_INDI(&f->image, &f->height, &f->width, &fujiWidth,
	    f->colors, hh->fuji_step, d);

    hh->message = d->messageBuffer;
    return d->lastStatus;
}

int dcraw_image_resize(dcraw_image_data *image, int size)
{
    int h, w, wid, r, ri, rii, c, ci, cii, cl, norm;
    guint64 riw, riiw, ciw, ciiw;
    guint64 (*iBuf)[4];
    int mul=size, div=MAX(image->height, image->width);

    if (mul > div) return DCRAW_ERROR;
    /* I'm skiping the last row/column if it is not a full row/column */
    h = image->height * mul / div;
    w = image->width * mul / div;
    wid = image->width;
    iBuf = (guint64(*)[4])g_new0(guint64, h * w * 4);
    norm = div * div;

    for(r=0; r<image->height; r++) {
        /* r should be divided between ri and rii */
        ri = r * mul / div;
        rii = (r+1) * mul / div;
        /* with weights riw and riiw (riw+riiw==mul) */
        riw = rii * div - r * mul;
        riiw = (r+1) * mul - rii * div;
        if (rii>=h) {rii=h-1; riiw=0;}
        if (ri>=h) {ri=h-1; riw=0;}
        for(c=0; c<image->width; c++) {
            ci = c * mul / div;
            cii = (c+1) * mul / div;
            ciw = cii * div - c * mul;
            ciiw = (c+1) * mul - cii * div;
            if (cii>=w) {cii=w-1; ciiw=0;}
            if (ci>=w) {ci=w-1; ciw=0;}
            for (cl=0; cl<image->colors; cl++) {
                iBuf[ri *w+ci ][cl] += image->image[r*wid+c][cl]*riw *ciw ;
                iBuf[ri *w+cii][cl] += image->image[r*wid+c][cl]*riw *ciiw;
                iBuf[rii*w+ci ][cl] += image->image[r*wid+c][cl]*riiw*ciw ;
                iBuf[rii*w+cii][cl] += image->image[r*wid+c][cl]*riiw*ciiw;
            }
        }
    }
    for (c=0; c<h*w; c++) for (cl=0; cl<image->colors; cl++)
        image->image[c][cl] = iBuf[c][cl]/norm;
    g_free(iBuf);
    image->height = h;
    image->width = w;
    return DCRAW_SUCCESS;
}

/* Adapted from dcraw.c stretch() - NKBJ */
int dcraw_image_stretch(dcraw_image_data *image, double pixel_aspect)
{
    int newdim, row, col, c, colors = image->colors;
    double rc, frac;
    ushort *pix0, *pix1;
    dcraw_image_type *iBuf;

    if (pixel_aspect==1) return DCRAW_SUCCESS;
    if (pixel_aspect < 1) {
	newdim = (int)(image->height / pixel_aspect + 0.5);
	iBuf = g_new(dcraw_image_type, image->width * newdim);
	for (rc=row=0; row < newdim; row++, rc+=pixel_aspect) {
	    frac = rc - (c = (int)rc);
	    pix0 = pix1 = image->image[c*image->width];
	    if (c+1 < image->height) pix1 += image->width*4;
	    for (col=0; col < image->width; col++, pix0+=4, pix1+=4)
		FORCC iBuf[row*image->width+col][c] =
		    (guint16)(pix0[c]*(1-frac) + pix1[c]*frac + 0.5);
	}
	image->height = newdim;
    } else {
	newdim = (int)(image->width * pixel_aspect + 0.5);
	iBuf = g_new(dcraw_image_type, image->height * newdim);
	for (rc=col=0; col < newdim; col++, rc+=1/pixel_aspect) {
	    frac = rc - (c = (int)rc);
	    pix0 = pix1 = image->image[c];
	    if (c+1 < image->width) pix1 += 4;
	    for (row=0; row < image->height;
		row++, pix0+=image->width*4, pix1+=image->width*4)
		FORCC iBuf[row*newdim+col][c] =
		    (guint16)(pix0[c]*(1-frac) + pix1[c]*frac + 0.5);
	}
	image->width = newdim;
    }
    g_free(image->image);
    image->image = iBuf;
    return DCRAW_SUCCESS;
}

int dcraw_flip_image(dcraw_image_data *image, int flip)
{
    if (flip)
        flip_image_INDI(image->image, &image->height, &image->width, flip);
    return DCRAW_SUCCESS;
}

int dcraw_set_color_scale(dcraw_data *h, int useAutoWB, int useCameraWB)
{
    DCRaw *d = (DCRaw *)h->dcraw;
    g_free(d->messageBuffer);
    d->messageBuffer = NULL;
    d->lastStatus = DCRAW_SUCCESS;
    memcpy(h->post_mul, h->pre_mul, sizeof h->post_mul);
    if (!d->is_foveon) /* foveon_interpolate() do this. */
        /* BUG white should not be global */
        scale_colors_INDI(h->raw.image,
                h->rgbMax-h->black, h->black, useAutoWB, useCameraWB,
                h->cam_mul, h->raw.height, h->raw.width, h->raw.colors,
                h->post_mul, h->filters, d->white, d->ifname, d);
    h->message = d->messageBuffer;
    return d->lastStatus;
}

int dcraw_wavelet_denoise(dcraw_data *h, float threshold)
{
    DCRaw *d = (DCRaw *)h->dcraw;
    g_free(d->messageBuffer);
    d->messageBuffer = NULL;
    d->lastStatus = DCRAW_SUCCESS;
    memcpy(h->post_mul, h->pre_mul, sizeof h->post_mul);
    if (threshold)
	wavelet_denoise_INDI(h->raw.image, h->black, h->raw.height,
		h->raw.width, h->height, h->width, h->raw.colors, h->shrink,
		h->post_mul, threshold, h->filters, d);
    h->message = d->messageBuffer;
    return d->lastStatus;
}

int dcraw_finalize_interpolate(dcraw_image_data *f, dcraw_data *h,
	int interpolation, int rgbWB[4])
{
    DCRaw *d = (DCRaw *)h->dcraw;
    int fujiWidth, i, r, c, cl;
    unsigned ff, f4;

    g_free(d->messageBuffer);
    d->messageBuffer = NULL;
    d->lastStatus = DCRAW_SUCCESS;

    f->width = h->width;
    f->height = h->height;
    fujiWidth = h->fuji_width;
    f->colors = h->colors;
    f->image = g_new0(dcraw_image_type, f->height * f->width);

    if (h->filters==0)
	return DCRAW_ERROR;

    cl = h->colors;
    if (interpolation==dcraw_four_color_interpolation || h->colors == 4) {
	ff = h->fourColorFilters;
        cl = 4;
	interpolation = dcraw_vng_interpolation;
    } else {
	ff = h->filters &= ~((h->filters & 0x55555555) << 1);
    }
    /* It might be better to report an error here: */
    /* (dcraw also forbids AHD for Fuji rotated images) */
    if (interpolation==dcraw_ahd_interpolation && h->colors > 3)
	interpolation = dcraw_vng_interpolation;
    f4 = h->fourColorFilters;
    if (h->colors==3) rgbWB[3] = rgbWB[1];
    for(r=0; r<h->height; r++)
        for(c=0; c<h->width; c++)
            f->image[r*f->width+c][fc_INDI(ff,r,c)] = MIN( MAX( (gint64)
                (h->raw.image[r/2*h->raw.width+c/2][fc_INDI(f4,r,c)] - h->black) *
                rgbWB[fc_INDI(f4,r,c)]/0x10000, 0), 0xFFFF);

    if (interpolation==dcraw_bilinear_interpolation)
	lin_interpolate_INDI(f->image, ff, f->width, f->height, cl, d);
    else if (interpolation==dcraw_vng_interpolation)
	vng_interpolate_INDI(f->image, ff, f->width, f->height, cl, 0xFFFF, d);
    else if (interpolation==dcraw_ahd_interpolation)
	ahd_interpolate_INDI(f->image, ff, f->width, f->height, cl,
		h->rgb_cam, d);

    if (cl==4 && h->colors == 3) {
        for (i=0; i<f->height*f->width; i++)
            f->image[i][1] = (f->image[i][1]+f->image[i][3])/2;
    }
    fuji_rotate_INDI(&f->image, &f->height, &f->width, &fujiWidth,
	    f->colors, h->fuji_step, d);

    h->message = d->messageBuffer;
    return d->lastStatus;
}

void dcraw_close(dcraw_data *h)
{
    DCRaw *d = (DCRaw *)h->dcraw;
    g_free(d->ifname);
    g_free(h->raw.image);
    delete d;
}

char *ufraw_message(int code, char *message, ...);

void DCRaw::dcraw_message(int code, const char *format, ...)
{
    char *buf, *message;
    va_list ap;
    va_start(ap, format);
    message = g_strdup_vprintf(format, ap);
    va_end(ap);
#ifdef DEBUG
    fprintf(stderr, message);
#endif
    if (code==DCRAW_VERBOSE)
	ufraw_message(code, message);
    else {
	if (messageBuffer==NULL) messageBuffer = g_strdup(message);
	else {
	    buf = g_strconcat(messageBuffer, message, NULL);
	    g_free(messageBuffer);
	    messageBuffer = buf;
	}
	lastStatus = code;
    }
    g_free(message);
}

void dcraw_message(void *dcraw, int code, char *format, ...)
{
    char *message;
    DCRaw *d = (DCRaw *)dcraw;
    va_list ap;
    va_start(ap, format);
    message = g_strdup_vprintf(format, ap);
    d->dcraw_message(code, message);
    va_end(ap);
    g_free(message);
}

} /*extern "C"*/