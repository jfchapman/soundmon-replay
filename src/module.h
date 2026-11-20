// SoundMonReplay: a SoundMon song file (docs/SoundMon-format.md, section 2).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace smon {

enum class Version { BPSM, V2, V3 };

struct Sample {
	std::string name;
	uint32_t offset = 0;  // byte offset of the sample data in the file
	uint16_t words = 0;
	uint16_t loop_bytes = 0;  // loop start, in bytes
	uint16_t loop_words = 1;  // 1 = no loop
	uint16_t volume = 0;
};

struct Synth {
	uint8_t wave = 0;
	uint16_t words = 0;
	uint8_t adsr_ctl = 0, adsr_table = 0, adsr_speed = 0;
	int adsr_len = 0;  // table lengths in bytes; 0 in the file means 65536 (the table runs on)
	uint8_t lfo_ctl = 0, lfo_table = 0, lfo_depth = 0, lfo_speed = 0;
	int lfo_len = 0;
	uint16_t lfo_delay = 0;
	uint8_t eg_ctl = 0, eg_table = 0, eg_speed = 0, eg_b19 = 0;
	int eg_len = 0;
	uint16_t eg_delay = 0;
	uint8_t fx = 0, fx_speed = 0, fx_delay = 0;  // V.3
	uint8_t mod_ctl = 0, mod_table = 0, mod_speed = 0, mod_delay = 0;
	int mod_len = 0;
	uint8_t volume = 0;
};

struct Instrument {
	bool synth = false;
	Sample sample;
	Synth syn;
};

struct Entry {  // one voice of a step
	uint16_t track = 0;
	int8_t sound_transpose = 0;  // added to the instrument number
	int8_t transpose = 0;        // added to the note
};

struct Row {
	int8_t note = 0;
	uint8_t instrument = 0;
	uint8_t option = 0;
	uint8_t data = 0;
};

class Module {
public:
	// Returns false if the data isn't a SoundMon song.
	bool load(const uint8_t *data, size_t size);
	static bool probe(const uint8_t *data, size_t size);

	Version version = Version::V2;
	std::string title;
	std::vector<uint8_t> file;  // the song as loaded (the replayer works on a copy)
	Instrument instruments[16];  // 1-15
	std::vector<Entry> steps;    // 4 per step
	int step_count = 0;
	int track_count = 0;
	uint32_t track_base = 0;
	uint32_t wave_base = 0;
	int wave_count = 0;

	Row row(int track, int row) const;
};

}  // namespace smon
