#include "shared.h"

auto temporalsoftenRegister(VSRegisterFunction registerFunc, VSPlugin *plugin)->void;
auto spatialsoftenRegister(VSRegisterFunction registerFunc, VSPlugin *plugin)->void;

VS_EXTERNAL_API(auto) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	configFunc("com.vapoursynth.focus", "focus", "VapourSynth Pixel Restoration Filters", VAPOURSYNTH_API_VERSION, 1, plugin);
	temporalsoftenRegister(registerFunc, plugin);
	spatialsoftenRegister(registerFunc, plugin);
}
