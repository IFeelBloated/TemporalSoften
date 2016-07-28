#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <malloc.h>
#include <stdlib.h> 
#include "VapourSynth.h"

enum class PixelType {
	Integer8 = 8,
	Integer9to16 = 16,
	Single = 32
};

struct TemporalSoftenData final {
	VSNodeRef *node = nullptr;
	const VSVideoInfo *vi = nullptr;
	int radius = 0;
	double luma_threshold = 0.;
	double chroma_threshold = 0.;
	double scenechange = 0.;
};

auto VS_CC temporalSoftenInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<TemporalSoftenData *>(*instanceData);
	vsapi->setVideoInfo(d->vi, 1, node);
	d->scenechange *= d->vi->width / 32 * 32 * d->vi->height;
	auto bits = d->vi->format->bitsPerSample;
	auto pixel = static_cast<PixelType>(bits > 8 && bits < 16 ? 16 : bits);
	switch (pixel) {
	case PixelType::Integer9to16:
		d->luma_threshold *= ((1 << bits) - 1) / 255.;
		d->chroma_threshold *= ((1 << bits) - 1) / 255.;
		d->scenechange *= ((1 << bits) - 1) / 255.;
		break;
	case PixelType::Single:
		d->luma_threshold /= 255;
		d->chroma_threshold /= 255;
		d->scenechange /= 255;
		break;
	default:
		break;
	}
}

auto VS_CC temporalSoftenGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)->const VSFrameRef *{
	auto d = reinterpret_cast<TemporalSoftenData *>(*instanceData);
	if (activationReason == arInitial) {
		auto first = n - d->radius;
		auto last = n + d->radius;
		first = first < 0 ? 0 : first;
		last = last > d->vi->numFrames - 1 ? d->vi->numFrames - 1 : last;
		for (auto i = first; i <= last; ++i)
			vsapi->requestFrameFilter(i, d->node, frameCtx);
	}
	else if (activationReason == arAllFramesReady) {
		decltype(vsapi->getFrameFilter(0, nullptr, nullptr)) src[16];
		bool planeDisabled[16];
		for (auto &x : src)
			x = nullptr;
		for (auto &x : planeDisabled)
			x = false;
		for (auto i = n - d->radius; i <= n + d->radius; ++i)
			src[i - n + d->radius] = vsapi->getFrameFilter(std::min(d->vi->numFrames - 1, std::max(i, 0)), d->node, frameCtx);
		auto dst = vsapi->copyFrame(src[d->radius], core);
		auto fi = d->vi->format;
		auto bits = fi->bitsPerSample;
		auto pixel = static_cast<PixelType>(bits > 8 && bits < 16 ? 16 : bits);
		auto pmax = (1 << bits) - 1;
		for (auto plane = 0; plane < fi->numPlanes; ++plane) {
			if (fi->colorFamily != cmRGB) {
				if (plane == 0 && d->luma_threshold == 0.)
					continue;
				if (plane == 1 && d->chroma_threshold == 0.)
					break;
			}
			auto current_threshold = (plane == 0 || fi->colorFamily == cmRGB) ? d->luma_threshold : d->chroma_threshold;
			auto dd = 0;
			decltype(vsapi->getStride(nullptr, 0)) src_stride[16];
			decltype(vsapi->getStride(nullptr, 0)) src_stride_trimmed[16];
			decltype(vsapi->getReadPtr(nullptr, 0)) srcp[16];
			decltype(vsapi->getReadPtr(nullptr, 0)) srcp_trimmed[16];
			for (auto i = 0; i < d->radius; ++i) {
				src_stride[dd] = vsapi->getStride(src[i], plane);
				srcp[dd] = vsapi->getReadPtr(src[i], plane);
				++dd;
			}
			for (auto i = 1; i <= d->radius; ++i) {
				src_stride[dd] = vsapi->getStride(src[d->radius + i], plane);
				srcp[dd] = vsapi->getReadPtr(src[d->radius + i], plane);
				++dd;
			}
			auto dst_stride = vsapi->getStride(dst, plane);
			auto dstp = vsapi->getWritePtr(dst, plane);
			auto h = vsapi->getFrameHeight(src[d->radius], plane);
			auto w = vsapi->getFrameWidth(src[d->radius], plane);
			auto sc_f = [=](auto srcp, auto dstp, auto stride, auto &sum) {
				auto wp = w / 32 * 32;
				sum = 0;
				for (auto y = 0; y < h; ++y)
					for (auto x = 0; x < wp; ++x)
						sum += std::abs(static_cast<decltype(sum + 0)>(srcp[x + y * stride / sizeof(srcp[0])]) - dstp[x + y * dst_stride / sizeof(dstp[0])]);
			};
			if (d->scenechange > 0.) {
				auto dd2 = 0;
				auto skiprest = false;
				for (auto i = d->radius - 1; i >= 0; --i)
					if (!skiprest && !planeDisabled[i]) {
						auto sum_ptr = alloca(std::max(sizeof(long long), sizeof(double)));
						auto f = [&](auto srcp, auto dstp, auto sum_ptr) {
							auto &scenevalues = *sum_ptr;
							sc_f(srcp, dstp, src_stride[i], scenevalues);
							if (scenevalues < d->scenechange) {
								src_stride_trimmed[dd2] = src_stride[i];
								srcp_trimmed[dd2] = reinterpret_cast<decltype(srcp_trimmed[0])>(srcp);
								++dd2;
							}
							else
								skiprest = true;
							planeDisabled[i] = skiprest;
						};
						switch (pixel) {
						case PixelType::Single:
							f(reinterpret_cast<const float *>(srcp[i]),
								reinterpret_cast<float *>(dstp),
								reinterpret_cast<double *>(sum_ptr));
							break;
						case PixelType::Integer9to16:
							f(reinterpret_cast<const uint16_t *>(srcp[i]),
								reinterpret_cast<uint16_t *>(dstp),
								reinterpret_cast<long long *>(sum_ptr));
							break;
						default:
							f(srcp[i], dstp, reinterpret_cast<long long *>(sum_ptr));
							break;
						}
					}
					else
						planeDisabled[i] = true;
				skiprest = false;
				for (auto i = 0; i < d->radius; ++i)
					if (!skiprest && !planeDisabled[i + d->radius]) {
						auto sum_ptr = alloca(std::max(sizeof(long long), sizeof(double)));
						auto f = [&](auto srcp, auto dstp, auto sum_ptr) {
							auto &scenevalues = *sum_ptr;
							sc_f(srcp, dstp, src_stride[i + d->radius], scenevalues);
							if (scenevalues < d->scenechange) {
								src_stride_trimmed[dd2] = src_stride[i + d->radius];
								srcp_trimmed[dd2] = reinterpret_cast<decltype(srcp_trimmed[0])>(srcp);
								++dd2;
							}
							else
								skiprest = true;
							planeDisabled[i + d->radius] = skiprest;
						};
						switch (pixel) {
						case PixelType::Single:
							f(reinterpret_cast<const float *>(srcp[i + d->radius]),
								reinterpret_cast<float *>(dstp),
								reinterpret_cast<double *>(sum_ptr));
							break;
						case PixelType::Integer9to16:
							f(reinterpret_cast<const uint16_t *>(srcp[i + d->radius]),
								reinterpret_cast<uint16_t *>(dstp),
								reinterpret_cast<long long *>(sum_ptr));
							break;
						default:
							f(srcp[i + d->radius], dstp, reinterpret_cast<long long *>(sum_ptr));
							break;
						}
					}
					else
						planeDisabled[i + d->radius] = true;
				std::memcpy(srcp, srcp_trimmed, dd2 * sizeof(srcp[0]));
				std::memcpy(src_stride, src_stride_trimmed, dd2 * sizeof(src_stride[0]));
				dd = dd2;
			}
			if (dd < 1) {
				for (auto x : src)
					vsapi->freeFrame(x);
				return dst;
			}
			auto div = dd + 1;
			for (auto y = 0; y < h; ++y) {
				auto sum_ptr = alloca(std::max(sizeof(long long), sizeof(double)));
				auto f = [&](auto srcp, auto dstp, auto sum_ptr) {
					for (auto x = 0; x < w; ++x) {
						auto &sum = *sum_ptr;
						sum = 0;
						sum += dstp[x];
						for (auto frame = dd - 1; frame >= 0; --frame) {
							auto absolute = std::abs(static_cast<decltype(sum + 0)>(dstp[x]) - srcp[frame][x]);
							if (absolute <= current_threshold)
								sum += srcp[frame][x];
							else
								sum += dstp[x];
						}
						if (pixel != PixelType::Single) {
							sum += div / 2;
							sum /= div;
							sum = sum < 0 ? 0 : sum > pmax ? pmax : sum;
						}
						else
							sum /= div;
						dstp[x] = static_cast<decltype(dstp[0] + 0)>(sum);
					}
				};
				switch (pixel) {
				case PixelType::Single:
					f(reinterpret_cast<const float **>(srcp),
						reinterpret_cast<float *>(dstp),
						reinterpret_cast<double *>(sum_ptr));
					break;
				case PixelType::Integer9to16:
					f(reinterpret_cast<const uint16_t **>(srcp),
						reinterpret_cast<uint16_t *>(dstp),
						reinterpret_cast<long long *>(sum_ptr));
					break;
				default:
					f(srcp, dstp, reinterpret_cast<long long *>(sum_ptr));
					break;
				}
				for (auto i = 0; i < dd; ++i)
					srcp[i] += src_stride[i];
				dstp += dst_stride;
			}
		}
		for (auto x : src)
			vsapi->freeFrame(x);
		return dst;
	}
	return nullptr;
}

auto VS_CC temporalSoftenFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<TemporalSoftenData *>(instanceData);
	vsapi->freeNode(d->node);
	delete d;
}

auto VS_CC temporalSoftenCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	auto data = new TemporalSoftenData;
	auto err = 0;
	data->node = vsapi->propGetNode(in, "clip", 0, 0);
	data->vi = vsapi->getVideoInfo(data->node);
	if (!data->vi->format) {
		vsapi->setError(out, "TemporalSoften: only constant format YUV, RGB, or Gray input supported");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->vi->format->bitsPerSample == 16 && data->vi->format->sampleType == stFloat) {
		vsapi->setError(out, "TemporalSoften: half precision not supported!");
		vsapi->freeNode(data->node);
		return;
	}
	data->radius = static_cast<decltype(data->radius)>(vsapi->propGetInt(in, "radius", 0, &err));
	if (err)
		data->radius = 4;
	data->luma_threshold = vsapi->propGetFloat(in, "luma_threshold", 0, &err);
	if (err)
		data->luma_threshold = 4.;
	data->chroma_threshold = vsapi->propGetFloat(in, "chroma_threshold", 0, &err);
	if (err)
		data->chroma_threshold = 8.;
	data->scenechange = vsapi->propGetFloat(in, "scenechange", 0, &err);
	if (err)
		data->scenechange = 0.;
	if (data->radius < 1 || data->radius > 7) {
		vsapi->setError(out, "TemporalSoften: radius must be between 1 and 7 (inclusive)");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->luma_threshold < 0. || data->luma_threshold > 255.) {
		vsapi->setError(out, "TemporalSoften: luma_threshold must be between 0.0 and 255.0 (inclusive)");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->chroma_threshold < 0. || data->chroma_threshold > 255.) {
		vsapi->setError(out, "TemporalSoften: chroma_threshold must be between 0.0 and 255.0 (inclusive)");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->luma_threshold == 0. && data->chroma_threshold == 0.) {
		vsapi->setError(out, "TemporalSoften: luma_threshold and chroma_threshold can't both be 0.0");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->luma_threshold == 0 && (data->vi->format->colorFamily == cmRGB || data->vi->format->colorFamily == cmGray)) {
		vsapi->setError(out, "TemporalSoften: luma_threshold must not be 0.0 when the input is RGB or Gray");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->scenechange > 0. && data->vi->format->colorFamily == cmRGB) {
		vsapi->setError(out, "TemporalSoften: scenechange is not available with RGB input");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->scenechange > 254.999999999) {
		vsapi->setError(out, "TemporalSoften: scenechange must be between 0.0 and 254.999999999 (inclusive)");
		vsapi->freeNode(data->node);
		return;
	}
	vsapi->createFilter(in, out, "TemporalSoften", temporalSoftenInit, temporalSoftenGetFrame, temporalSoftenFree, fmParallel, 0, data, core);
	return;
}

VS_EXTERNAL_API(auto) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("com.focus.temporalsoften", "focus", "VapourSynth TemporalSoften Filter", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("TemporalSoften", "clip:clip;radius:int:opt;luma_threshold:float:opt;chroma_threshold:float:opt;scenechange:float:opt", temporalSoftenCreate, 0, plugin);
}
