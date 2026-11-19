// SoundMonReplay: loading a song (docs/SoundMon-format.md, section 2).
#include "module.h"

#include <algorithm>
#include <cstring>

namespace smon {

namespace {

uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }

}  // namespace

bool Module::probe(const uint8_t *data, size_t size) {
	if (!data || size < 512)
		return false;
	return std::memcmp(data + 26, "BPSM", 4) == 0 || std::memcmp(data + 26, "V.2", 3) == 0 ||
	       std::memcmp(data + 26, "V.3", 3) == 0;
}

bool Module::load(const uint8_t *data, size_t size) {
	if (!probe(data, size))
		return false;
	file.assign(data, data + size);
	if (std::memcmp(data + 26, "BPSM", 4) == 0) {
		version = Version::BPSM;
		wave_count = 0;
	} else {
		version = data[28] == '3' ? Version::V3 : Version::V2;
		wave_count = data[29];
	}
	title.assign(reinterpret_cast<const char *>(data), 26);
	title = title.substr(0, title.find('\0'));
	while (!title.empty() && title.back() == ' ')
		title.pop_back();

	step_count = be16(data + 30);
	if (step_count == 0 || 512 + size_t(step_count) * 16 > size)
		return false;
	steps.resize(size_t(step_count) * 4);
	track_count = 0;
	for (int s = 0; s < step_count; s++) {
		for (int v = 0; v < 4; v++) {
			const uint8_t *p = data + 512 + 16 * s + 4 * v;
			Entry &e = steps[size_t(s) * 4 + size_t(v)];
			e.track = be16(p);
			e.sound_transpose = int8_t(p[2]);
			e.transpose = int8_t(p[3]);
			track_count = std::max<int>(track_count, e.track);
		}
	}
	track_base = 512 + uint32_t(step_count) * 16;
	wave_base = track_base + uint32_t(track_count) * 48;
	if (wave_base > size)
		return false;

	uint32_t sample_at = wave_base + uint32_t(wave_count) * 64;
	for (int i = 1; i <= 15; i++) {
		const uint8_t *p = data + 32 + 32 * (i - 1);
		Instrument &ins = instruments[i];
		ins = Instrument();
		if (p[0] == 0xFF && version != Version::BPSM) {
			ins.synth = true;
			Synth &s = ins.syn;
			s.wave = p[1];
			s.words = be16(p + 2);
			s.adsr_ctl = p[4];
			s.adsr_table = p[5];
			s.adsr_len = be16(p + 6);
			s.adsr_speed = p[8];
			s.lfo_ctl = p[9];
			s.lfo_table = p[10];
			s.lfo_depth = p[11];
			s.lfo_len = be16(p + 12);
			if (version == Version::V3) {
				s.lfo_delay = p[14];
				s.lfo_speed = p[15];
				s.eg_ctl = p[16];
				s.eg_table = p[17];
				s.eg_len = be16(p + 18);
				s.eg_delay = p[20];
				s.eg_speed = p[21];
				s.fx = p[22];
				s.fx_speed = p[23];
				s.fx_delay = p[24];
				s.mod_ctl = p[25];
				s.mod_table = p[26];
				s.mod_speed = p[27];
				s.mod_delay = p[28];
				s.volume = p[29];
				s.mod_len = be16(p + 30);
			} else {
				s.lfo_delay = be16(p + 14);
				s.lfo_speed = p[16];
				s.eg_ctl = p[17];
				s.eg_table = p[18];
				s.eg_b19 = p[19];
				s.eg_len = be16(p + 20);
				s.eg_delay = be16(p + 22);
				s.eg_speed = p[24];
				s.volume = p[25];
			}
		} else {
			Sample &s = ins.sample;
			s.words = be16(p + 24);
			s.loop_bytes = be16(p + 26);
			s.loop_words = be16(p + 28);
			s.volume = be16(p + 30);
			s.offset = sample_at;
			sample_at += uint32_t(s.words) * 2;
		}
	}
	return true;
}

Row Module::row(int track, int r) const {
	Row out;
	if (track < 1 || track > track_count)
		return out;
	const uint32_t at = track_base + uint32_t(track - 1) * 48 + uint32_t(r) * 3;
	if (at + 3 > file.size())
		return out;
	out.note = int8_t(file[at]);
	out.instrument = file[at + 1] >> 4;
	out.option = file[at + 1] & 15;
	out.data = file[at + 2];
	return out;
}

}  // namespace smon
