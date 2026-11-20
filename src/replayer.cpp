// SoundMonReplay: the replayer (docs/SoundMon-format.md, sections 3-6).
#include "replayer.h"

#include <algorithm>

namespace smon {

namespace {

const int kPeriods[84] = {
	6848, 6464, 6080, 5760, 5440, 5120, 4832, 4576, 4320, 4064, 3840, 3616,
	3424, 3232, 3040, 2880, 2720, 2560, 2416, 2288, 2160, 2032, 1920, 1808,
	1712, 1616, 1520, 1440, 1360, 1280, 1208, 1144, 1080, 1016, 960,  904,
	856,  808,  760,  720,  680,  640,  604,  572,  540,  508,  480,  452,
	428,  404,  380,  360,  340,  320,  302,  286,  270,  254,  240,  226,
	214,  202,  190,  180,  170,  160,  151,  143,  135,  127,  120,  113,
	107,  101,  95,   90,   85,   80,   76,   72,   68,   64,   60,   57,
};

// V.3 vibrato: the period offset is this over the option's data, from the tick after the note.
const int kVibrato[8] = {128, 64, 0, -64, -128, -64, 0, 64};

const int kOptionArpeggio = 0, kOptionVolume = 1, kOptionSpeed = 2, kOptionFilter = 3, kOptionUp = 4,
          kOptionDown = 5, kOptionRepeatCount = 6, kOptionRepeat = 7, kOptionSlide = 8, kOptionArpeggioOn = 9,
          kOptionTranspose = 10, kOptionFx = 11, kOptionFxSwap = 13, kOptionRestartAdsr = 14, kOptionNewNote = 15;

}  // namespace

Replayer::Replayer(const Module &module) : m_(module) { start(0); }

void Replayer::start(int step) {
	// The song's own memory, then the empty sample (one word of silence). It sits a little past
	// the end, because an empty instrument (length 0) points at the end of the file itself. A
	// length of 0 plays 65,536 words from there on the Amiga, whatever memory holds; here it's
	// silence.
	mem_ = m_.file;
	empty_ = uint32_t(mem_.size()) + 16;
	mem_.resize(mem_.size() + 32, 0);
	for (Voice &v : voice_) {
		v = Voice();
		v.rep_lc = empty_;
		v.rep_len = 1;
	}
	step_ = (step >= 0 && step < m_.step_count) ? step : 0;
	row_ = 0;
	count_ = 1;  // the first tick reads row 0
	speed_ = 6;
	arp_counter_ = 1;
	repeat_ = 1;
	pending_jump_ = -1;
	ticks_ = 0;
	event_ = RowEvent();
}

int Replayer::period_of(int note) const { return kPeriods[std::clamp(note + 35, 0, 83)]; }

int Replayer::table_entry(int table, int index) const {
	const size_t at = size_t(m_.wave_base) + size_t(table) * 64 + size_t(index);
	return at < empty_ ? mem_[at] : 0;
}

int Replayer::table_speed(int speed) const {
	// Speed 0 is 256 ticks per entry in V.3; in V.2 (and SoundMon 1) the table never moves on.
	return speed ? speed : m_.version == Version::V3 ? 256 : 0;
}

int Replayer::entry_index(int t, int delay, int speed) {
	// Entry 0 from the note, entry k >= 1 from tick 1 + delay + k * speed (speed 0 = 256).
	if (!speed)
		return 0;  // a table that never moves on
	const int s = speed;
	if (t < 1 + delay + s)
		return 0;
	return (t - 1 - delay) / s;
}

void Replayer::restore_copy(Voice &x) {
	if (!x.has_copy)
		return;
	for (int i = 0; i < 32; i++)
		if (x.wave_at + uint32_t(i) < empty_)
			mem_[x.wave_at + uint32_t(i)] = x.copy[i];
	x.has_copy = false;
}

// A synth note's copy of its waveform (docs/SoundMon-format.md, section 6.1). V.2 takes it at the
// note, whatever the instrument. V.3 takes it once every voice has read the row, so that a voice
// on the same waveform has put its own copy back first, and only for an instrument with a
// waveform effect (EG, FX or Mod): without one there's no copy, nothing is written back at the
// next note, and options B and D can't start an FX.
void Replayer::note_copy(Voice &x) {
	x.copy_pending = false;
	const bool v3 = m_.version == Version::V3;
	const Synth &s = m_.instruments[x.instrument].syn;
	if (v3 && !(s.eg_ctl || s.fx || s.mod_ctl))
		return;
	const uint32_t w = x.wave_at;
	take_copy(x, w);
	// V.2 byte 19, with the EG on: the note starts with the waveform's first (byte 19 / 8) bytes
	// negated, and the EG carries on from there. (Values 1-7 do something else in the original,
	// not modelled: no known song has them with the EG on.)
	if (!v3 && s.eg_ctl && s.eg_b19 >= 8) {
		x.eg_n = std::min(s.eg_b19 >> 3, 31);
		for (int i = 0; i < x.eg_n; i++)
			if (w + uint32_t(i) < empty_)
				mem_[w + uint32_t(i)] = uint8_t(-int8_t(x.copy[i]));
	}
}

void Replayer::take_copy(Voice &x, uint32_t at) {
	x.wave_at = at;
	x.eg_n = 0;
	for (int i = 0; i < 32; i++)
		x.copy[i] = at + uint32_t(i) < empty_ ? mem_[at + uint32_t(i)] : 0;
	x.has_copy = true;
}

void Replayer::tick(std::vector<Write> &out) {
	out_ = &out;
	event_ = RowEvent();
	const bool v3 = m_.version == Version::V3;
	bool noted[4] = {false, false, false, false};

	if (!v3) {
		arp_counter_ = arp_counter_ > 1 ? arp_counter_ - 1 : 3;
		// Slides and the synth part, before the registers are rewritten.
		for (int v = 3; v >= 0; v--) {  // Paula channel order, like everything per voice
			Voice &x = voice_[v];
			if (!x.playing)
				continue;
			// The LFO's period is taken before this tick's slide; without an LFO the slide is
			// heard at once.
			x.lfo_out = -1;
			if (x.synth)
				synth_effects(v, out);
			x.period += x.slide;
		}
		for (int ch = 0; ch < 4; ch++) {
			const Voice &x = voice_[3 - ch];
			// A synth with an LFO has its period written by the LFO, and only once it runs (after
			// its delay); until then Paula keeps the note's period.
			const bool lfo = x.playing && x.synth && m_.instruments[x.instrument].syn.lfo_ctl;
			if (!lfo)
				put(out, ch, Reg::PER, uint32_t(x.period));
			else if (x.lfo_out >= 0)
				put(out, ch, Reg::PER, uint32_t(x.lfo_out));
			put(out, ch, Reg::LC, x.rep_lc);
			put(out, ch, Reg::LEN, x.rep_len);
		}
		// The arpeggio: the note, its low nibble, its high nibble.
		for (int v = 3; v >= 0; v--) {
			Voice &x = voice_[v];
			if (!x.arp_on())  // even on a voice that has never played (from note 0)
				continue;
			const int off = arp_counter_ == 2 ? 0 : arp_counter_ == 1 ? x.arp_low() : x.arp_high();
			put(out, 3 - v, Reg::PER, uint32_t(period_of(x.note + off)));
		}
	} else {
		arp_counter_ = (arp_counter_ - 1) & 3;
		// The slide comes first, so the per-tick write already has it. One set on this row starts
		// on the next tick.
		for (Voice &x : voice_)
			if (x.playing)
				x.period += x.slide;
		for (int ch = 0; ch < 4; ch++) {
			const Voice &x = voice_[3 - ch];
			put(out, ch, Reg::PER, uint32_t(x.period));
			put(out, ch, Reg::LC, x.rep_lc);
			put(out, ch, Reg::LEN, x.rep_len);
		}
		// The vibrato, before the row: its phase runs from the start of the song, whatever the
		// notes. Synth voices with an LFO take their period from the synth part below instead.
		for (int v = 3; v >= 0; v--) {
			Voice &x = voice_[v];
			if (!x.playing || !x.vibrato)
				continue;
			const int vib = kVibrato[(ticks_ - 1) & 7] / x.vibrato;
			const bool lfo = x.synth && m_.instruments[x.instrument].syn.lfo_ctl;
			if (!lfo)
				put(out, 3 - v, Reg::PER, uint32_t(x.period + vib));
		}
		// The arpeggio (after the slide, which it overwrites): low nibble, high nibble, then the
		// note for two ticks.
		for (int v = 3; v >= 0; v--) {
			Voice &x = voice_[v];
			if (!x.arp_on())  // even on a voice that has never played (from note 0)
				continue;
			const int off = arp_counter_ == 1 ? x.arp_low() : arp_counter_ == 0 ? x.arp_high() : 0;
			x.period = period_of(x.note + off);
			put(out, 3 - v, Reg::PER, uint32_t(x.period));
		}
		// The synth part, before the row (so on a note's tick it plays on with the old note's
		// state, and what a row changes is heard from the next tick).
		for (int v = 3; v >= 0; v--) {
			Voice &x = voice_[v];
			x.synth_period = x.period;
			x.synth_volume = x.volume;
			if (!x.playing || !x.synth)
				continue;
			synth_effects(v, out);
			if (m_.instruments[x.instrument].syn.lfo_ctl && x.lfo_write)
				put(out, 3 - v, Reg::PER, uint32_t(x.synth_period + x.lfo));
		}
	}

	if (--count_ <= 0) {
		for (int v = 0; v < 4; v++)
			noted[v] = false;
		event_.read = true;
		event_.step = step_;
		event_.row = row_;
		uint32_t dma = 0;
		// Voices are read in Paula channel order (song voice 4 first). A V.2 repeat (option 7)
		// changes the step at once, so the voices after it read this row of the new step.
		for (int v = 3; v >= 0; v--) {
			const Entry &e = m_.steps[size_t(step_) * 4 + size_t(v)];
			const Row r = m_.row(e.track, row_);
			noted[v] = voice_row(v, r, e, dma, out);
		}
		// V.3 takes a synth note's copy once every voice has read the row (note_copy).
		for (int v = 3; v >= 0; v--)
			if (voice_[v].copy_pending)
				note_copy(voice_[v]);
		for (int ch = 0; ch < 4; ch++)
			if (dma & (1u << ch))
				put(out, ch, Reg::DMA_ON, 0);
		// The next row.
		if (v3 && pending_jump_ >= 0) {
			event_.jumped = true;
			event_.target = pending_jump_;
			step_ = pending_jump_;
			row_ = 0;
			pending_jump_ = -1;
		} else {
			row_++;
			if (row_ >= 16) {
				row_ = 0;
				step_++;
			}
		}
		if (step_ >= m_.step_count)
			step_ = 0;
		count_ = speed_ ? speed_ : 256;
	}

	for (Voice &x : voice_) {
		x.since_note++;
		if (x.fx_next >= 0) {
			x.fx = x.fx_next;
			x.fx_next = -1;
		}
	}
	ticks_++;
	out_ = nullptr;
}

bool Replayer::voice_row(int v, const Row &r, const Entry &e, uint32_t &dma, std::vector<Write> &out) {
	Voice &x = voice_[v];
	const int ch = 3 - v;
	const bool v3 = m_.version == Version::V3;
	const bool bpsm = m_.version == Version::BPSM;
	const int opt = r.option, d = r.data;

	const bool keep_sound = v3 && (opt == kOptionFxSwap || opt == kOptionRestartAdsr || opt == kOptionNewNote);

	if (r.note != 0) {
		const bool no_transpose = opt == kOptionTranspose && (d & 0xF0);
		const bool no_sound_transpose = opt == kOptionTranspose && (d & 0x0F);
		x.note = r.note + (no_transpose ? 0 : e.transpose);
		int period = period_of(x.note);
		if (opt == kOptionUp)
			period -= d;
		else if (opt == kOptionDown)
			period += d;
		x.slide = 0;
		x.arp_cont_on = false;
		x.lfo = 0;
		if (v3)
			x.vibrato = 0;

		if (keep_sound) {
			// D, E, F: the note changes without restarting the sound; the arpeggio writes it.
			x.period = period;
		} else {
			int ins = r.instrument;
			const bool new_ins = ins != 0;
			if (new_ins && !no_sound_transpose)
				ins += e.sound_transpose;
			if (!new_ins)
				ins = x.instrument;
			// Beyond 1-15 (a sound transpose too far) the original reads an instrument from
			// whatever memory follows its table; here it's instrument 0, a silent sample.
			if (ins < 0 || ins > 15)
				ins = 0;
			const Instrument &I = m_.instruments[ins];
			// A note on the instrument already playing (or with no instrument number) doesn't
			// restart it if the sound loops: a synth, or (V.2 and SoundMon 1) a looped sample.
			const bool same = x.playing && ins == x.instrument;
			const bool restart = !(same && (I.synth || (!v3 && I.sample.loop_words > 1)));
			x.instrument = ins;
			x.period = period;
			if (restart)
				put(out, ch, Reg::DMA_OFF, 0);
			put(out, ch, Reg::PER, uint32_t(period));
			x.volume = opt == kOptionVolume ? d : (I.synth ? I.syn.volume : I.sample.volume);
			if (!I.synth) {
				restore_copy(x);
				x.synth = false;
				const Sample &s = I.sample;
				put(out, ch, Reg::LC, s.offset);
				put(out, ch, Reg::LEN, s.words);
				put(out, ch, Reg::VOL, uint32_t(x.volume));
				if (s.loop_words > 1) {
					x.rep_lc = s.offset + s.loop_bytes;
					x.rep_len = s.loop_words;
					if (v3) {
						put(out, ch, Reg::LEN, uint32_t(s.loop_bytes / 2 + s.loop_words));
					} else {
						put(out, ch, Reg::LEN, x.rep_len);
						put(out, ch, Reg::LC, x.rep_lc);
					}
				} else {
					x.rep_lc = empty_;
					x.rep_len = 1;
				}
			} else {
				const Synth &s = I.syn;
				restore_copy(x);
				const uint32_t w = m_.wave_base + uint32_t(s.wave) * 64;
				x.wave_at = w;
				x.copy_pending = true;
				if (!v3)
					note_copy(x);
				x.synth = true;
				x.fx = s.fx;
				x.fx_count = 0;
				x.adsr_pos = 0;
				x.adsr_count = 1;
				x.adsr_restart = false;
				x.hold_next = false;
				x.volume_hold = -1;
				put(out, ch, Reg::LC, w);
				put(out, ch, Reg::LEN, s.words);
				int vol = x.volume;
				if (s.adsr_ctl)
					vol = (((table_entry(s.adsr_table, 0) ^ 0x80) >> 2) * x.volume) >> 6;
				put(out, ch, Reg::VOL, uint32_t(vol));
				x.rep_lc = w;
				x.rep_len = s.words;
			}
			x.since_note = 0;
			x.playing = true;
			dma |= 1u << ch;
		}
	}

	if (opt == kOptionUp || opt == kOptionDown)
		x.arp_option = 0;
	switch (opt) {
	case kOptionArpeggio:
		x.arp_option = d;
		break;
	case kOptionVolume:
		x.volume = d;
		// V.3: a synth takes the new volume from the next tick (with ADSR, at its next step).
		if (r.note == 0 && !(v3 && x.synth))
			put(out, ch, Reg::VOL, uint32_t(d));
		break;
	case kOptionSpeed:
		speed_ = d;
		break;
	case kOptionFilter:
		filter_ = d == 0 ? 0 : 1;
		break;
	case kOptionUp:
	case kOptionDown:
		if (r.note == 0) {
			x.period += opt == kOptionUp ? -d : d;
			if (bpsm)  // V.2 and V.3 are heard a tick later
				put(out, ch, Reg::PER, uint32_t(x.period));
		}
		break;
	case kOptionRepeatCount:
		if (v3)
			x.vibrato = d;
		else
			repeat_ = d;
		break;
	case kOptionRepeat:
		if (v3) {
			pending_jump_ = d;
		} else {
			repeat_ = (repeat_ - 1) & 255;
			if (repeat_ != 0) {
				step_ = d < m_.step_count ? d : 0;
				event_.jumped = true;
				event_.target = step_;
			}
		}
		break;
	case kOptionSlide:
		x.slide = int8_t(d);
		break;
	case kOptionArpeggioOn:
		if (v3 && r.note == 0)
			x.adsr_restart = true;  // V.3: the ADSR starts again
		x.arp_cont = d;
		x.arp_cont_on = d != 0;
		break;
	case kOptionFx:
		// From the next tick. A note that restarts the sound sets its instrument's own type,
		// which wins over B on the same row.
		if (v3 && !(r.note != 0 && !keep_sound))
			x.fx_next = d;
		break;
	case kOptionFxSwap:
	case kOptionRestartAdsr:
	case kOptionNewNote:
		if (v3) {
			// D flips the type's lowest bit (inversion and transform swap direction; averaging
			// goes off), from the next tick.
			if (opt == kOptionFxSwap)
				x.fx_next = (x.fx_next >= 0 ? x.fx_next : x.fx) ^ 1;
			if (opt != kOptionNewNote)
				x.adsr_restart = true;  // D and E: the ADSR starts again
			// The arpeggio, as option 9 sets it. (A new note's period reaches Paula on the next
			// tick through the per-tick period write.)
			x.arp_cont = d;
			x.arp_cont_on = d != 0;
		}
		break;
	default:
		break;
	}
	// D, E and F change the note without restarting it: the synth part runs on as usual this tick.
	return r.note != 0 && !keep_sound;
}

void Replayer::synth_effects(int v, std::vector<Write> &out) {
	Voice &x = voice_[v];
	const Synth &s = m_.instruments[x.instrument].syn;
	const bool v3 = m_.version == Version::V3;
	const int t = x.since_note;
	const uint32_t w = x.wave_at;

	// ADSR: the volume. A counter, reloaded with the speed (0 = 256), and a position in the
	// table: each time the counter runs out, the entry at the position is written and the
	// position moves on. A note sets the position to 0 and the counter to run out on the next
	// tick. Played once, it writes nothing after its last entry, so the volume stays.
	// A restart (V.3 options 9, D, E, on the row before) puts the position back to 0, keeping the
	// counter's rhythm; a synth without ADSR gets a single volume instead.
	if (x.adsr_restart) {
		x.adsr_restart = false;
		if (s.adsr_ctl && s.adsr_len)
			x.adsr_pos = 0;
		else
			x.hold_next = true;
	}
	const int volume = v3 ? x.synth_volume : x.volume;
	if (x.hold_next) {
		// V.3: options 9, D and E on a synth without ADSR still restart it, once: entry 0 of
		// its table at a volume of 255, and nothing after.
		x.hold_next = false;
		x.volume_hold = (((table_entry(s.adsr_table, 0) ^ 0x80) >> 2) * 255) >> 6;
	}
	if (s.adsr_ctl && s.adsr_len) {
		if (--x.adsr_count <= 0) {
			x.adsr_count = table_speed(s.adsr_speed) ? table_speed(s.adsr_speed) : 0x7FFFFFFF;
			if (x.adsr_pos < s.adsr_len || s.adsr_ctl != 1) {
				const int k = x.adsr_pos % s.adsr_len;
				put(out, 3 - v, Reg::VOL, uint32_t((((table_entry(s.adsr_table, k) ^ 0x80) >> 2) * volume) >> 6));
			}
			if (x.adsr_pos < s.adsr_len || s.adsr_ctl != 1)
				x.adsr_pos++;
		}
	} else {
		put(out, 3 - v, Reg::VOL, uint32_t(x.volume_hold >= 0 ? x.volume_hold : volume));
	}
	// LFO: the period.
	x.lfo = 0;
	x.lfo_write = false;
	if (s.lfo_ctl && s.lfo_len) {
		const int lfo_speed = table_speed(s.lfo_speed);
		int k = entry_index(t, s.lfo_delay, lfo_speed);
		const int end = v3 ? s.lfo_len : s.lfo_len - 1;
		bool on = true;
		if (k >= end && s.lfo_ctl == 1)
			on = false;
		else
			k %= s.lfo_len;
		if (on)
			x.lfo = int8_t(table_entry(s.lfo_table, k)) / std::max<int>(s.lfo_depth, 1);
		// The period is written only on the ticks the table moves on, from 1 + delay. Between
		// them V.2's Paula holds the last LFO period; V.3's per-tick write puts back the plain one.
		// At speed 0 a V.2 LFO never writes at all (gapulus); a V.3 one writes every 256 ticks.
		x.lfo_write = lfo_speed && t >= 1 + s.lfo_delay && (t - 1 - s.lfo_delay) % lfo_speed == 0;
		// Played once, it writes the plain period when it ends, then nothing more.
		if (s.lfo_ctl == 1 && t >= 1 && entry_index(t - 1, s.lfo_delay, lfo_speed) >= end)
			x.lfo_write = false;
		if (x.lfo_write)
			x.lfo_out = x.period + x.lfo;
	}

	if (!x.has_copy || t < 1)
		return;

	// EG: negates the start of the waveform.
	if (s.eg_ctl && s.eg_len && t >= 1 + s.eg_delay) {
		int k = entry_index(t, s.eg_delay, table_speed(s.eg_speed));
		if (k >= s.eg_len)
			k = s.eg_ctl == 1 ? s.eg_len - 1 : k % s.eg_len;
		// Only the bytes entering or leaving the first n change: an entering byte becomes the
		// saved copy's byte negated, a leaving one the copy's byte. The rest keep what the FX has
		// made of them.
		const int n = (table_entry(s.eg_table, k) ^ 0x80) >> 3;
		for (int i = std::min(n, x.eg_n); i < std::max(n, x.eg_n); i++)
			if (w + uint32_t(i) < empty_)
				mem_[w + uint32_t(i)] = i < n ? uint8_t(-int8_t(x.copy[i])) : x.copy[i];
		x.eg_n = n;
	}

	// Then the FX, on what the EG has left.
	if (v3) {
		// FX. Averaging runs on a countdown that only moves while the FX is averaging: from tick
		// 1 + delay (byte 24) it runs whenever the count is out, and the count restarts at *speed*
		// (speed 0: it runs once). So averaging switched on by option B or D after that tick runs
		// at once, then every *speed* ticks. The others run every tick from tick 1, ignoring the
		// delay, each byte stepping *speed* toward its target; a byte that doesn't land on its
		// target swings either side of it (speed 0: no change).
		const int fx_from = 1 + s.fx_delay;
		bool fx_tick = true;
		if (x.fx == 1) {
			fx_tick = false;
			if (t >= fx_from) {
				if (x.fx_count == 0) {
					fx_tick = true;
					x.fx_count = s.fx_speed ? s.fx_speed : 0x7FFFFFFF;
				}
				x.fx_count--;
			}
		}
		if (x.fx && t >= 1 && fx_tick && w + 33 < empty_ && w > 0) {
			auto sb = [&](uint32_t a) { return int(int8_t(mem_[a])); };
			const int step = s.fx_speed;
			if (x.fx == 1) {
				for (int i = 0; i < 32; i++) {
					const uint32_t a = w + uint32_t(i);
					mem_[a] = uint8_t((sb(a - 1) + sb(a + 1)) >> 1);
				}
			} else if (x.fx >= 2 && x.fx <= 5) {
				int target[32];
				for (int i = 0; i < 32; i++) {
					if (x.fx == 2)
						target[i] = int8_t(x.copy[31 - i]);
					else if (x.fx == 4)
						target[i] = w + 64 + uint32_t(i) < empty_ ? sb(w + 64 + uint32_t(i)) : 0;
					else
						target[i] = int8_t(x.copy[i]);
				}
				for (int i = 0; i < 32; i++) {
					const uint32_t a = w + uint32_t(i);
					const int b = sb(a);
					mem_[a] = uint8_t(b < target[i] ? b + step : b > target[i] ? b - step : b);
				}
			}
		}
	}

	if (!v3)
		return;

	// Mod: one byte of the waveform from the table.
	if (s.mod_ctl && s.mod_len && t >= 1 + s.mod_delay && w + 32 < empty_) {
		int k = entry_index(t, s.mod_delay, table_speed(s.mod_speed));
		if (k >= s.mod_len)
			k = s.mod_ctl == 1 ? s.mod_len - 1 : k % s.mod_len;
		mem_[w + 32] = uint8_t(table_entry(s.mod_table, k));
	}
}

}  // namespace smon
