// SoundMonReplay: the Amiga's sound chip, Paula, as far as SoundMon uses it.
#pragma once

#include <cstdint>
#include <vector>

#include "soundmonreplay.h"

namespace smon {

const double kPaulaClock = 3546895.0;  // PAL colour clock, Hz

struct Channel {
	// Registers, as the replayer writes them. lc is a byte offset into the memory.
	uint32_t lc = 0;
	uint16_t len = 1;
	uint16_t per = 428;
	int vol = 0;
	bool dma = false;

	// Playback: the block Paula latched, and the position in it (in samples, i.e. bytes).
	uint32_t block_lc = 0;
	uint32_t block_bytes = 2;
	double pos = 0;

	// The last sample of the previous block (for interpolation across a loop point).
	float prev = 0;

	// Anti-click ramp: the volume actually applied, moving toward vol.
	float ramp_vol = 0;
};

class Paula {
public:
	void reset();
	void set_memory(const std::vector<uint8_t> *memory) { mem_ = memory; }
	void set_output(int sample_rate, smon_interpolation interpolation, int ramp_us, int separation, float gain,
	                smon_amiga_model model);
	void set_led(bool on) { led_ = on; }

	Channel &channel(int n) { return ch_[n]; }

	void dma_on(int n);
	void dma_off(int n);

	// Mixes 'frames' stereo frames, added into out (left, right interleaved).
	void render(float *out, int frames);

	// Which voices are heard: bit n for channel n. Muted voices still play.
	void set_voice_mask(unsigned mask) { mask_ = mask; }

private:
	float sample_at(const Channel &c, uint32_t index) const;
	void next_block(Channel &c);
	void step(Channel &c, double samples);
	float voice_sample(const Channel &c) const;
	float filter(int side, float x);

	Channel ch_[4];
	struct Tail {
		Channel c;
		bool active = false;
	};
	Tail tails_[4];  // cut voices fading out (anti-click)
	const std::vector<uint8_t> *mem_ = nullptr;
	int rate_ = 44100;
	smon_interpolation interp_ = SMON_INTERPOLATION_NONE;
	float ramp_step_ = 0;  // volume units per output frame, 0 = no ramping
	float sep_ = 1.0f;
	float gain_ = 1.0f;
	unsigned mask_ = 0xF;
	bool heard(int n) const { return (mask_ >> n) & 1; }

	// The output filters, per side: the fixed one-pole low-pass, then the two-pole LED filter.
	double fixed_coef_ = 0;  // 0 = off
	bool led_ = false;
	double led_b0_ = 0, led_a1_ = 0, led_a2_ = 0;
	struct FilterState {
		double fixed = 0, led1 = 0, led2 = 0;
	};
	FilterState filter_[2];
};

}  // namespace smon
