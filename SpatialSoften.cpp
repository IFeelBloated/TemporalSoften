#include "shared.h"

auto VS_CC spatialsoftenInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<SpatialSoftenData *>(*instanceData);
	vsapi->setVideoInfo(d->vi, 1, node);
	auto pixel = static_cast<PixelType>(d->vi->format->bytesPerSample);
	switch (pixel) {
	case PixelType::Integer9to16:
		d->luma_threshold *= ((1 << d->vi->format->bitsPerSample) - 1) / 255.;
		d->chroma_threshold *= ((1 << d->vi->format->bitsPerSample) - 1) / 255.;
		break;
	case PixelType::Single:
		d->luma_threshold /= 255;
		d->chroma_threshold /= 255;
		break;
	default:
		break;
	}
}

auto VS_CC spatialsoftenGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)->const VSFrameRef *{
	auto d = reinterpret_cast<SpatialSoftenData *>(*instanceData);
	if (activationReason == arInitial)
		vsapi->requestFrameFilter(n, d->node, frameCtx);
	else if (activationReason == arAllFramesReady) {
		auto src = vsapi->getFrameFilter(n, d->node, frameCtx);
		auto dst = vsapi->copyFrame(src, core);
		auto fi = d->vi->format;
		auto pixel = static_cast<PixelType>(fi->bytesPerSample);
		auto pmax = (1 << fi->bitsPerSample) - 1;
		auto diameter = (d->radius << 1) + 1;
		for (auto plane = 0; plane < fi->numPlanes; ++plane) {
			if (fi->colorFamily != cmRGB) {
				if (plane == 0 && d->luma_threshold == 0.)
					continue;
				if (plane == 1 && d->chroma_threshold == 0.)
					break;
			}
			auto src_stride = vsapi->getStride(src, plane) / fi->bytesPerSample;
			auto dst_stride = vsapi->getStride(dst, plane) / fi->bytesPerSample;
			auto dstp = vsapi->getWritePtr(dst, plane);
			auto srcp = vsapi->getReadPtr(src, plane);
			auto h = vsapi->getFrameHeight(src, plane);
			auto w = vsapi->getFrameWidth(src, plane);
			auto current_threshold = (plane == 0 || fi->colorFamily == cmRGB) ? d->luma_threshold : d->chroma_threshold;
			for (auto y = 0; y < h; ++y) {
				auto kernel = [&](auto srcp, auto dstp, auto sum_type) {
					decltype(srcp) line[65];
					for (auto i = 0; i < diameter; ++i)
						line[i] = srcp + src_stride *
						[](auto x, auto min, auto max) {
						return x > max ? max : x < min ? min : x;
					}(y + i - (diameter >> 1), 0, h - 1);
					auto x = 0;
					for (; x < d->radius; ++x)
						dstp[y * dst_stride + x] = srcp[y * src_stride + x];
					for (; x < w - d->radius; ++x) {
						auto div = 0;
						decltype(sum_type) sum = 0;
						auto center = static_cast<decltype(sum_type)>(srcp[y * src_stride + x]);
						for (auto i = 0; i < diameter; ++i)
							for (auto j = -d->radius; j < 1 + d->radius; ++j)
								if (std::abs(line[i][x + j] - center) <= current_threshold) {
									sum += line[i][x + j];
									++div;
								}
						if (pixel != PixelType::Single) {
							sum += div / 2;
							sum /= div;
							sum = sum < 0 ? 0 : sum > pmax ? pmax : sum;
						}
						else
							sum /= div;
						dstp[y * dst_stride + x] = static_cast<decltype(dstp[0] + 0)>(sum);
					}
					for (; x < w; ++x)
						dstp[y * dst_stride + x] = srcp[y * src_stride + x];
				};
				switch (pixel) {
				case PixelType::Single:
					kernel(reinterpret_cast<const float *>(srcp), reinterpret_cast<float *>(dstp), 0.);
					break;
				case PixelType::Integer9to16:
					kernel(reinterpret_cast<const uint16_t *>(srcp), reinterpret_cast<uint16_t *>(dstp), 0ll);
					break;
				default:
					kernel(srcp, dstp, 0ll);
					break;
				}
			}
		}
		vsapi->freeFrame(src);
		return dst;
	}
	return nullptr;
}

auto VS_CC spatialsoftenFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	auto d = reinterpret_cast<SpatialSoftenData *>(instanceData);
	vsapi->freeNode(d->node);
	delete d;
}

auto VS_CC spatialsoftenCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	auto data = new SpatialSoftenData;
	auto err = 0;
	data->node = vsapi->propGetNode(in, "clip", 0, 0);
	data->vi = vsapi->getVideoInfo(data->node);
	if (!data->vi->format) {
		vsapi->setError(out, "SpatialSoften: only constant format YUV, RGB, or Gray input supported");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->vi->format->bitsPerSample == 16 && data->vi->format->sampleType == stFloat) {
		vsapi->setError(out, "SpatialSoften: half precision not supported!");
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
	if (data->radius < 1 || data->radius > 32) {
		vsapi->setError(out, "SpatialSoften: radius must be between 1 and 32 (inclusive)");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->luma_threshold < 0. || data->luma_threshold > 255.) {
		vsapi->setError(out, "SpatialSoften: luma_threshold must be between 0.0 and 255.0 (inclusive)");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->chroma_threshold < 0. || data->chroma_threshold > 255.) {
		vsapi->setError(out, "SpatialSoften: chroma_threshold must be between 0.0 and 255.0 (inclusive)");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->luma_threshold == 0. && data->chroma_threshold == 0.) {
		vsapi->setError(out, "SpatialSoften: luma_threshold and chroma_threshold can't both be 0.0");
		vsapi->freeNode(data->node);
		return;
	}
	if (data->luma_threshold == 0 && (data->vi->format->colorFamily == cmRGB || data->vi->format->colorFamily == cmGray)) {
		vsapi->setError(out, "SpatialSoften: luma_threshold must not be 0.0 when the input is RGB or Gray");
		vsapi->freeNode(data->node);
		return;
	}
	vsapi->createFilter(in, out, "SpatialSoften", spatialsoftenInit, spatialsoftenGetFrame, spatialsoftenFree, fmParallel, 0, data, core);
	return;
}

auto spatialsoftenRegister(VSRegisterFunction registerFunc, VSPlugin *plugin) {
	registerFunc("SpatialSoften", "clip:clip;radius:int:opt;luma_threshold:float:opt;chroma_threshold:float:opt", spatialsoftenCreate, 0, plugin);
}
