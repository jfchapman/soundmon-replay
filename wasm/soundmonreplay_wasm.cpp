// Entry points for the WebAssembly build, for callers that work in plain numbers: JavaScript can't fill in an
// smon_settings without knowing how the compiler laid it out. The rest of the API is exported as it is.
#include "soundmonreplay.h"

static smon_settings make_settings(int sample_rate, int stereo_separation, int interpolation, int volume_ramping, int loop_count) {
	smon_settings settings;
	smon_default_settings(&settings);
	settings.sample_rate = sample_rate;
	settings.stereo_separation = stereo_separation;
	settings.interpolation = static_cast<smon_interpolation>(interpolation);
	settings.volume_ramping = volume_ramping;
	settings.loop_count = loop_count;
	return settings;
}

extern "C" {

// Opens a song with the default settings at this rate. Returns null if it can't.
smon_module *smon_wasm_open(const void *data, size_t size, int sample_rate) {
	const smon_settings settings = make_settings(sample_rate, 100, SMON_INTERPOLATION_NONE, 2000, 0);
	return smon_open(data, size, &settings, nullptr);
}

// Changes the settings of an open song, which take effect at once (the sample rate at the next subsong
// selection). Returns 0 on success.
int smon_wasm_configure(smon_module *module, int sample_rate, int stereo_separation, int interpolation, int volume_ramping, int loop_count) {
	const smon_settings settings = make_settings(sample_rate, stereo_separation, interpolation, volume_ramping, loop_count);
	return smon_set_settings(module, &settings);
}

}
