/*
 * UFRaw - Unidentified Flying Raw converter for digital camera images
 *
 * ufraw_lensfun.cc - define lensfun UFObject settings.
 * Copyright 2004-2010 by Udi Fuchs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ufraw.h"
#include <glib/gi18n.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#ifdef HAVE_LENSFUN

#define UF_LF_TRANSFORM ( \
	LF_MODIFY_DISTORTION | LF_MODIFY_GEOMETRY | LF_MODIFY_SCALE)

namespace UFRaw {

class Lensfun : public UFGroup {
private:
    static lfDatabase *LensDB;
public:
    lfLens Transformation;
    lfLens Interpolation;
    double FocalLengthValue;
    double ApertureValue;
    double DistanceValue;
    Lensfun();
#if 0 // Can be useful for valgrind --leak-check=full
    ~Lensfun() {
	if (LensDB != NULL)
	    lf_db_destroy(LensDB);
	LensDB = NULL;
    }
#endif
    static Lensfun &Parent(UFObject &object) {
	if (strcmp(object.Parent().Name(), ufLensfun) == 0)
	    return static_cast<Lensfun &>(object.Parent());
	return Lensfun::Parent(object.Parent());
    }
    void Interpolate();
    void Init();
};

#define _buffer_size 80

char *_StringNumber(char *buffer, double number) {
/*    int precision = 0;
    while (floor(number * pow(10, precision))*10 <
	    floor(number * pow(10, precision+1)))
	precision++;*/
    // Compute the floating-point precision which is enough for "normal use".
    // The criteria is to have 2 or 3 significant digits.
    int precision;
    if (number > 10.0 && (int)(10*number)%10 != 0)
	// Support focal length such as 10.5mm fisheye.
	precision = MAX(-floor(log(number) / log(10) - 1.99), 0);
    else
	precision = MAX(-floor(log(number) / log(10) - 0.99), 0);
    snprintf(buffer, _buffer_size, "%.*f", precision, number);
    return buffer;
}

extern "C" { UFName ufFocalLength = "FocalLength"; }
class FocalLength : public UFArray {
public:
    FocalLength() : UFArray(ufFocalLength) { }
    void OriginalValueChangedEvent() {
	if (!HasParent())
	    return;
	double value;
	if (sscanf(StringValue(), "%lf", &value) != 1)
	    return;
	Lensfun::Parent(*this).FocalLengthValue = value;
	Lensfun::Parent(*this).Interpolate();
    }
    void CreatePresets() {
	if (!HasParent())
	    return;
	Clear();
	lfLens &lens = Lensfun::Parent(*this).Interpolation;

	static double focalValues[] = { 4, 5, 6, 7, 8, 10, 12, 14, 17, 21, 24,
		28, 35, 43, 50, 70, 85, 105, 135, 200, 300, 400, 600, 800, 0 };
	char buffer[_buffer_size];
	double min = lens.MinFocal, max = lens.MaxFocal;
	if (min > 0)
	    *this << new UFString(ufPreset, _StringNumber(buffer, min));
	int i = 0;
	while (focalValues[i] < min && focalValues[i] != 0)
	    i++;
	if (Has(_StringNumber(buffer, focalValues[i])))
	    i++; // Comparing strings works better than comparing doubles.
	while (focalValues[i] < max && focalValues[i] != 0) {
	    *this << new UFString(ufPreset, _StringNumber(buffer,
		    focalValues[i]));
	    i++;
	}
	if (max > min)
	    *this << new UFString(ufPreset, _StringNumber(buffer, max));
    }
};

extern "C" { UFName ufAperture = "Aperture"; }
class Aperture : public UFArray {
public:
    Aperture() : UFArray(ufAperture) { }
    void OriginalValueChangedEvent() {
	if (!HasParent())
	    return;
	double value;
	if (sscanf(StringValue(), "%lf", &value) != 1)
	    return;
	Lensfun::Parent(*this).ApertureValue = value;
	Lensfun::Parent(*this).Interpolate();
    }
    void CreatePresets() {
	if (!HasParent())
	    return;
	Clear();
	lfLens &lens = Lensfun::Parent(*this).Interpolation;

	static double apertureValues[] = { 1, 1.2, 1.4, 1.7, 2, 2.4, 2.8, 3.4,
	    4, 4.8, 5.6, 6.7, 8, 9.5, 11, 13, 16, 19, 22, 27, 32, 38, 45, 0 };
	char buffer[_buffer_size];
	double min = lens.MinAperture;
	if (min > 0)
	    *this << new UFString(ufPreset, _StringNumber(buffer, min));
	int i = 0;
	while (apertureValues[i] < min && apertureValues[i] != 0)
	    i++;
	if (Has(_StringNumber(buffer, apertureValues[i])))
	    i++; // Comparing strings works better than comparing doubles.
	while (apertureValues[i] != 0) {
	    *this << new UFString(ufPreset, _StringNumber(buffer,
		    apertureValues[i]));
	    i++;
	}
    }
};

extern "C" { UFName ufDistance = "Distance"; }
class Distance : public UFArray {
public:
    Distance() : UFArray(ufDistance) { }
    void OriginalValueChangedEvent() {
	if (!HasParent())
	    return;
	double value;
	if (sscanf(StringValue(), "%lf", &value) != 1)
	    return;
	Lensfun::Parent(*this).DistanceValue = value;
	Lensfun::Parent(*this).Interpolate();
    }
    void CreatePresets() {
	Clear();
	char buffer[_buffer_size];
	double value = 0.25;
	while (value < 1001) {
	    *this << new UFString(ufPreset, _StringNumber(buffer, value));
	    value *= sqrt(2);
	    if (value > 127 && value < 129) value = 125;
	}
    }
};

extern "C" { UFName ufModel = "Model"; }

extern "C" { UFName ufTCA = "TCA"; }
class TCA : public UFArray {
public:
    TCA() : UFArray(ufTCA,
	    lfLens::GetTCAModelDesc(LF_TCA_MODEL_NONE, NULL, NULL)) {
	for (lfTCAModel model = LF_TCA_MODEL_NONE; ;
		model = lfTCAModel(model+1)) {
	    const lfParameter **params;
	    const char *model_name =
		    lfLens::GetTCAModelDesc(model, NULL, &params);
	    if (model_name == NULL)
		break; // End of loop.
	    UFGroup &Model = *(new UFGroup(ufModel, model_name));
	    (*this) << &Model;
	    assert(params != NULL);
	    for (int i = 0; params[i] != NULL; i++)
		Model << new UFNumber(params[i]->Name, params[i]->Min,
			params[i]->Max, params[i]->Default);
	}
    }
    void Event(UFEventType type) {
	if (type != uf_value_changed)
	    return UFObject::Event(type);
	ufraw_data *uf = ufraw_image_get_data(this);
	if (uf == NULL)
	    return UFObject::Event(type);
	lfLens &lens = Lensfun::Parent(*this).Transformation;
	if (lens.CalibTCA != NULL)
	    while (lens.CalibTCA[0] != NULL)
		lens.RemoveCalibTCA(0);
	lfLensCalibTCA calib;
	calib.Model = static_cast<lfTCAModel>(Index());
	calib.Focal = Lensfun::Parent(*this).FocalLengthValue;
	const lfParameter **params;
	lfLens::GetTCAModelDesc(calib.Model, NULL, &params);
	if (params != NULL) {
	    UFGroup &Model = (*this)[StringValue()];
	    for (int i = 0; params[i] != NULL; i++) {
		UFNumber &Param = Model[params[i]->Name];
		calib.Terms[i] = Param.DoubleValue();
	    }
	}
	lens.AddCalibTCA(&calib);
	ufraw_invalidate_tca_layer(uf);
	return UFObject::Event(type);
    }
    void Interpolate() {
	if (!HasParent())
	    return;
	Lensfun &Lensfun = Lensfun::Parent(*this);
	lfLensCalibTCA calib;
	if (!Lensfun.Interpolation.InterpolateTCA(
		Lensfun.FocalLengthValue, calib)) {
	    SetIndex(0);
	    return;
	}
	SetIndex(calib.Model - LF_TCA_MODEL_NONE);
	const lfParameter **params;
	lfLens::GetTCAModelDesc(calib.Model, NULL, &params);
	if (params != NULL) {
	    UFGroup &Model = (*this)[StringValue()];
	    for (int i = 0; params[i] != NULL; i++) {
		UFNumber &Param = Model[params[i]->Name];
		Param.Set(calib.Terms[i]);
	    }
	}
    }
};

extern "C" { UFName ufVignetting = "Vignetting"; }
class Vignetting : public UFArray {
public:
    Vignetting() : UFArray(ufVignetting,
	    lfLens::GetVignettingModelDesc(LF_VIGNETTING_MODEL_NONE,
		    NULL, NULL)) {
	for (lfVignettingModel model = LF_VIGNETTING_MODEL_NONE; ;
		model = lfVignettingModel(model+1)) {
	    const lfParameter **params;
	    const char *model_name =
		    lfLens::GetVignettingModelDesc(model, NULL, &params);
	    if (model_name == NULL)
		break; // End of loop.
	    UFGroup &Model = *(new UFGroup(ufModel, model_name));
	    (*this) << &Model;
	    assert(params != NULL);
	    for (int i = 0; params[i] != NULL; i++)
		Model << new UFNumber(params[i]->Name, params[i]->Min,
			params[i]->Max, params[i]->Default);
	}
    }
    void Event(UFEventType type) {
	if (type != uf_value_changed)
	    return UFObject::Event(type);
	ufraw_data *uf = ufraw_image_get_data(this);
	if (uf == NULL)
	    return UFObject::Event(type);
	lfLens &lens = Lensfun::Parent(*this).Transformation;
	if (lens.CalibVignetting != NULL)
	    while (lens.CalibVignetting[0] != NULL)
		lens.RemoveCalibVignetting(0);
	lfLensCalibVignetting calib;
	calib.Model = static_cast<lfVignettingModel>(Index());
	calib.Focal = Lensfun::Parent(*this).FocalLengthValue;
	calib.Aperture = Lensfun::Parent(*this).ApertureValue;
	calib.Distance = Lensfun::Parent(*this).DistanceValue;
	const lfParameter **params;
	lfLens::GetVignettingModelDesc(calib.Model, NULL, &params);
	if (params != NULL) {
	    UFGroup &Model = (*this)[StringValue()];
	    for (int i = 0; params[i] != NULL; i++) {
		UFNumber &Param = Model[params[i]->Name];
		calib.Terms[i] = Param.DoubleValue();
	    }
	}
	lens.AddCalibVignetting(&calib);
	ufraw_invalidate_layer(uf, ufraw_first_phase);
	return UFObject::Event(type);
    }
    void Interpolate() {
	ufraw_data *uf = ufraw_image_get_data(this);
	if (uf == NULL)
	    return;
	Lensfun &Lensfun = Lensfun::Parent(*this);
	lfLensCalibVignetting calib;
	if (!Lensfun.Interpolation.InterpolateVignetting(
		Lensfun.FocalLengthValue, Lensfun.FocalLengthValue,
		Lensfun.DistanceValue, calib)) {
	    SetIndex(0);
	    return;
	}
	SetIndex(calib.Model - LF_VIGNETTING_MODEL_NONE);
	const lfParameter **params;
	lfLens::GetVignettingModelDesc(calib.Model, NULL, &params);
	if (params != NULL) {
	    UFGroup &Model = (*this)[StringValue()];
	    for (int i = 0; params[i] != NULL; i++) {
		UFNumber &Param = Model[params[i]->Name];
		Param.Set(calib.Terms[i]);
	    }
	}
    }
};

extern "C" { UFName ufDistortion = "Distortion"; }
class Distortion : public UFArray {
public:
    // The elements of Distortion are distortion models:
    // UFGroup &Model = (*this)[StringValue()];
    // The elements of Model are the model's parameters:
    // UFNumber Param = Model[params[i].Name];
    // In XML it would look something like this:
    // <Distortion Index='PanoTools lens model'>
    //   <Model Label='5th order polynomial'>
    //     <k1>0.0100</k1>
    //     <k2>-0.0100</k2>
    //   </Model>
    //   <Model Label='PanoTools lens model'>
    //     <a>0.0010</a>
    //     <b>0.0023</b>
    //   </Model>
    // </Distortion>
    Distortion() : UFArray(ufDistortion,
	    lfLens::GetDistortionModelDesc(LF_DIST_MODEL_NONE, NULL, NULL)) {
	for (lfDistortionModel model = LF_DIST_MODEL_NONE; ;
		model = lfDistortionModel(model+1)) {
	    const lfParameter **params;
	    const char *model_name =
		    lfLens::GetDistortionModelDesc(model, NULL, &params);
	    if (model_name == NULL)
		break; // End of loop.
	    UFGroup &Model = *(new UFGroup(ufModel, model_name));
	    (*this) << &Model;
	    assert(params != NULL);
	    for (int i = 0; params[i] != NULL; i++)
		Model << new UFNumber(params[i]->Name, params[i]->Min,
			params[i]->Max, params[i]->Default);
	}
    }
    void Event(UFEventType type) {
	if (type != uf_value_changed)
	    return UFObject::Event(type);
	ufraw_data *uf = ufraw_image_get_data(this);
	if (uf == NULL)
	    return UFObject::Event(type);
	lfLens &lens = Lensfun::Parent(*this).Transformation;
	if (lens.CalibDistortion != NULL)
	    while (lens.CalibDistortion[0] != NULL)
		lens.RemoveCalibDistortion(0);
	lfLensCalibDistortion calib;
	calib.Model = static_cast<lfDistortionModel>(Index());
	calib.Focal = Lensfun::Parent(*this).FocalLengthValue;
	const lfParameter **params;
	lfLens::GetDistortionModelDesc(calib.Model, NULL, &params);
	if (params != NULL) {
	    UFGroup &Model = (*this)[StringValue()];
	    for (int i = 0; params[i] != NULL; i++) {
		UFNumber &Param = Model[params[i]->Name];
		calib.Terms[i] = Param.DoubleValue();
	    }
	}
	lens.AddCalibDistortion(&calib);
	ufraw_invalidate_layer(uf, ufraw_transform_phase);
	return UFObject::Event(type);
    }
    void Interpolate() {
	if (!HasParent())
	    return;
	Lensfun &Lensfun = Lensfun::Parent(*this);
	lfLensCalibDistortion calib;
	if (!Lensfun.Interpolation.InterpolateDistortion(
		Lensfun.FocalLengthValue, calib)) {
	    SetIndex(0);
	    return;
	}
	SetIndex(calib.Model - LF_DIST_MODEL_NONE);
	const lfParameter **params;
	lfLens::GetDistortionModelDesc(calib.Model, NULL, &params);
	if (params != NULL) {
	    UFGroup &Model = (*this)[StringValue()];
	    for (int i = 0; params[i] != NULL; i++) {
		UFNumber &Param = Model[params[i]->Name];
		Param.Set(calib.Terms[i]);
	    }
	}
    }
};

extern "C" { UFName ufLensGeometry = "LensGeometry"; }
class LensGeometry : public UFArray {
public:
    explicit LensGeometry(UFName name = ufLensGeometry) : UFArray(name,
	    lfLens::GetLensTypeDesc(LF_UNKNOWN, NULL)) {
	for (lfLensType type = LF_UNKNOWN; ; type = lfLensType(type+1)) {
	    const char *typeName = lfLens::GetLensTypeDesc(type, NULL);
	    if (typeName == NULL)
		break; // End of loop.
	    *this << new UFString("Type", typeName);
	}
    }
    void OriginalValueChangedEvent() {
	ufraw_data *uf = ufraw_image_get_data(this);
	if (uf == NULL)
	    return;
	Lensfun::Parent(*this).Transformation.Type = lfLensType(Index());
	ufraw_invalidate_layer(uf, ufraw_transform_phase);
    }
};

extern "C" { UFName ufTargetLensGeometry = "TargetLensGeometry"; }
class TargetLensGeometry : public LensGeometry {
public:
    TargetLensGeometry() : LensGeometry(ufTargetLensGeometry) { }
    void OriginalValueChangedEvent() {
	ufraw_data *uf = ufraw_image_get_data(this);
	if (uf == NULL)
	    return;
	ufraw_invalidate_layer(uf, ufraw_transform_phase);
    }
};

extern "C" { UFName ufLensfun = "Lensfun"; }
Lensfun::Lensfun() : UFGroup(ufLensfun), FocalLengthValue(0.0),
	ApertureValue(0.0), DistanceValue(0.0) {
    *this
	<< new FocalLength
	<< new Aperture
	<< new Distance
	<< new TCA
	<< new Vignetting
	<< new Distortion
	<< new LensGeometry
	<< new TargetLensGeometry
    ;
}

void Lensfun::Interpolate() {
    static_cast<TCA &>((*this)[ufTCA]).Interpolate();
    static_cast<Vignetting &>((*this)[ufVignetting]).Interpolate();
    static_cast<Distortion &>((*this)[ufDistortion]).Interpolate();
}

lfDatabase *Lensfun::LensDB = NULL;

void Lensfun::Init() {
    /* Load lens database only once */
    if (LensDB == NULL) {
	LensDB = lfDatabase::Create();
	LensDB->Load();
    }
    ufraw_data *uf = ufraw_image_get_data(this);
    uf->conf->lensdb = LensDB;

    /* Create a default lens & camera */
    uf->conf->lens = &Transformation;
    uf->conf->camera = lf_camera_new();
    uf->conf->cur_lens_type = LF_UNKNOWN;

    /* Set lens and camera from EXIF info, if possible */
    if (uf->conf->real_make[0] || uf->conf->real_model[0]) {
	const lfCamera **cams = LensDB->FindCameras(
		uf->conf->real_make, uf->conf->real_model);
	if (cams != NULL) {
	    lf_camera_copy(uf->conf->camera, cams[0]);
	    lf_free(cams);
	}
    }
    if (strlen(uf->conf->lensText) > 0) {
	const lfLens **lenses = LensDB->FindLenses(
		uf->conf->camera, NULL, uf->conf->lensText);
	if (lenses != NULL) {
	    Interpolation = *lenses[0];
	    Transformation = *lenses[0];
	    lf_free(lenses);
	}
    }
    if (uf->conf->lensfunMode == lensfun_default) {
	if (uf->LoadingID) {
	    (*this)[ufTCA].Event(uf_value_changed);
	    (*this)[ufVignetting].Event(uf_value_changed);
	    (*this)[ufDistortion].Event(uf_value_changed);
	    return;
	}
	uf->conf->lensfunMode = lensfun_auto;
    }
    if (uf->conf->lensfunMode == lensfun_none) {
	(*this)[ufTCA].Reset();
	(*this)[ufVignetting].Reset();
	(*this)[ufDistortion].Reset();
    } else {
	static_cast<FocalLength &>((*this)[ufFocalLength]).CreatePresets();
	static_cast<Aperture &>((*this)[ufAperture]).CreatePresets();
	static_cast<Distance &>((*this)[ufDistance]).CreatePresets();
	Interpolate();
    }
    char buffer[_buffer_size];
    (*this)[ufFocalLength].Set(_StringNumber(buffer, uf->conf->focal_len));
    (*this)[ufFocalLength].SetDefault();
    (*this)[ufAperture].Set(_StringNumber(buffer, uf->conf->aperture));
    (*this)[ufAperture].SetDefault();
    (*this)[ufDistance].Set(_StringNumber(buffer, uf->conf->subject_distance));
    (*this)[ufDistance].SetDefault();
}

extern "C" {
void ufraw_lensfun_interpolate(UFObject *lensfun, const lfLens *lens) {
    Lensfun &Lensfun = dynamic_cast<UFRaw::Lensfun &>(*lensfun);
    Lensfun.Interpolation = *lens;
    static_cast<FocalLength &>(Lensfun[ufFocalLength]).CreatePresets();
    static_cast<Aperture &>(Lensfun[ufAperture]).CreatePresets();
    static_cast<Distance &>(Lensfun[ufDistance]).CreatePresets();
    Lensfun.Interpolate();
}

void ufraw_lensfun_init(ufraw_data *uf) {
    UFGroup &Image = *uf->conf->ufobject;
    UFRaw::Lensfun &Lensfun =  static_cast<UFRaw::Lensfun &>(Image[ufLensfun]);
    Lensfun.Init();
}

void ufraw_convert_prepare_transform(ufraw_data *uf,
	int width, int height, gboolean reverse, float scale)
{
    UFGroup &Image = *uf->conf->ufobject;
    UFRaw::Lensfun &Lensfun =  static_cast<UFRaw::Lensfun &>(Image[ufLensfun]);
    conf_data *conf = uf->conf;
    if (uf->modifier != NULL)
	uf->modifier->Destroy();
    uf->modifier = lfModifier::Create(&Lensfun.Transformation,
	    conf->camera->CropFactor, width, height);
    if (uf->modifier == NULL)
	return;

    UFArray &targetLensGeometry = Lensfun[ufTargetLensGeometry];
    uf->modFlags = uf->modifier->Initialize(&Lensfun.Transformation,
	    LF_PF_U16, Lensfun.FocalLengthValue, Lensfun.ApertureValue,
	    Lensfun.DistanceValue, scale,
	    lfLensType(targetLensGeometry.Index()),
	    UF_LF_TRANSFORM | LF_MODIFY_VIGNETTING, reverse);
    if ((uf->modFlags & (UF_LF_TRANSFORM | LF_MODIFY_VIGNETTING)) == 0) {
	uf->modifier->Destroy();
	uf->modifier = NULL;
    }
}

void ufraw_prepare_tca(ufraw_data *uf)
{
    UFGroup &Image = *uf->conf->ufobject;
    UFRaw::Lensfun &Lensfun =  static_cast<UFRaw::Lensfun &>(Image[ufLensfun]);
    ufraw_image_data *img = &uf->Images[ufraw_raw_phase];

    if (uf->TCAmodifier != NULL)
	uf->TCAmodifier->Destroy();
    uf->TCAmodifier = lfModifier::Create(&Lensfun.Transformation,
	    uf->conf->camera->CropFactor, img->width, img->height);
    if (uf->TCAmodifier == NULL)
	return;

    UFArray &targetLensGeometry = Lensfun[ufTargetLensGeometry];
    int modFlags = uf->TCAmodifier->Initialize(&Lensfun.Transformation,
	    LF_PF_U16, Lensfun.FocalLengthValue, Lensfun.ApertureValue,
	    Lensfun.DistanceValue, 1.0,
	    lfLensType(targetLensGeometry.Index()),
	    LF_MODIFY_TCA, false);
    if ((modFlags & LF_MODIFY_TCA) == 0) {
	uf->TCAmodifier->Destroy();
	uf->TCAmodifier = NULL;
    }
}

}

} // namespace UFRaw

extern "C" UFObject *ufraw_lensfun_new() {
    return new UFRaw::Lensfun();
}

#endif // HAVE_LENSFUN
