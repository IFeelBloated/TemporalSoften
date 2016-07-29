#pragma once
#include <algorithm>
#include <cstring>
#include <cmath>
#include "VapourSynth.h"

enum class PixelType {
	Integer8 = 1,
	Integer9to16 = 2,
	Single = 4
};

struct SpatialSoftenData final {
	VSNodeRef *node = nullptr;
	const VSVideoInfo *vi = nullptr;
	int radius = 0;
	double luma_threshold = 0.;
	double chroma_threshold = 0.;
};

struct TemporalSoftenData final {
	VSNodeRef *node = nullptr;
	const VSVideoInfo *vi = nullptr;
	int radius = 0;
	double luma_threshold = 0.;
	double chroma_threshold = 0.;
	double scenechange = 0.;
};
