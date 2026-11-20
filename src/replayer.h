// SoundMonReplay: the replayer (docs/SoundMon-format.md, sections 3-6).
//
// It works the way the original routines are seen to from outside: once a tick it writes Paula's
// registers, in the order the originals write them, and the synth effects rewrite the song's own
// waveforms in memory. Paula (paula.h) plays from that memory.
#pragma once

#include <cstdint>
#include <vector>

#include "module.h"

namespace smon {

enum class Reg : uint8_t { LC, LEN, PER, VOL, DMA_ON, DMA_OFF };

struct Write {
	uint8_t channel;  // Paula channel 0-3
	Reg reg;
	uint32_t value;   // LC: byte offset into memory()
};

class Replayer {
public:
	explicit Replayer(const Module &module);

	// Starts the song at a step (a sub-song) from the fresh state.
	void start(int step = 0);

	// One tick (a 50 Hz frame): the register writes it makes, appended to out.
	void tick(std::vector<Write> &out);

	// The song's memory as the replayer has it (waveforms rewritten by the synth effects), with
	// the empty sample at empty_offset().
	const std::vector<uint8_t> &memory() const { return mem_; }
	uint32_t empty_offset() const { return empty_; }

	// Position: the step and row about to be read next, and the repeat counter (V.2).
	int step() const { return step_; }
	int row() const { return row_; }
	int repeat_counter() const { return repeat_; }
	bool at_row_start() const { return count_ == 1; }
	int speed() const { return speed_; }

	// Option 3's last setting: 1 = filter on, 0 = off.
	int filter() const { return filter_; }

	// Set when the last tick read a row: the step and row it read, and any jump it made.
	struct RowEvent {
		bool read = false;
		int step = 0, row = 0;
		bool jumped = false;
		int target = 0;
	};
	const RowEvent &last_row() const { return event_; }

private:
	struct Voice {
		int instrument = 0;
		int note = 0;            // after the transpose
		int period = 0;          // the period variable (slides included)
		uint32_t rep_lc = 0;     // what each tick writes to LC/LEN (the loop)
		uint16_t rep_len = 1;
		int volume = 0;          // the note's volume (instrument or option 1)
		// Two arpeggios, added together. Option 0 sets one (0 = off), which only options 0, 4
		// and 5 change; options 9, D, E and F set the other, which a note ends.
		int arp_option = 0;
		int arp_cont = 0;
		bool arp_cont_on = false;
		bool arp_on() const { return arp_option != 0 || arp_cont_on; }
		int arp_low() const { return (arp_option & 15) + (arp_cont_on ? arp_cont & 15 : 0); }
		int arp_high() const { return (arp_option >> 4) + (arp_cont_on ? arp_cont >> 4 : 0); }
		int lfo = 0;             // the LFO's period offset this tick
		int lfo_out = -1;        // V.2: the period the LFO made this tick, -1 = no LFO
		bool lfo_write = false;  // the LFO writes the period this tick
		int synth_period = 0;    // V.3: the period before this tick's row, for the synth part
		int synth_volume = 0;    // V.3: the volume before this tick's row, likewise
		int slide = 0;           // option 8
		int vibrato = 0;         // V.3 option 6
		bool playing = false;
		// Synth state.
		bool synth = false;
		int since_note = 0;      // ticks since the note
		int adsr_pos = 0;        // the ADSR's position in its table
		int adsr_count = 1;      // ticks until it writes the next entry
		bool adsr_restart = false;  // V.3: options 9, D, E asked for a restart this tick
		bool hold_next = false;  // V.3: a restart on a synth without ADSR writes on the next tick
		int volume_hold = -1;    // V.3: the volume such a restart left
		int fx = 0;
		int fx_count = 0;        // V.3: ticks until the averaging FX runs again (it counts only while averaging)
		int eg_n = 0;            // how many bytes of the waveform the EG has negated
		int fx_next = -1;        // V.3: an FX type set by option B or D, taken up after this tick
		uint32_t wave_at = 0;    // the waveform being rewritten (0 = none)
		uint8_t copy[32] = {};
		bool has_copy = false;
		bool copy_pending = false;  // V.3: a synth note's copy, taken after the row
	};

	void process_row();
	// Returns true if the row had a note for this voice.
	bool voice_row(int v, const Row &r, const Entry &e, uint32_t &dma_mask, std::vector<Write> &out);
	void synth_effects(int v, std::vector<Write> &out);
	void restore_copy(Voice &voice);
	void take_copy(Voice &voice, uint32_t at);
	void note_copy(Voice &voice);
	int period_of(int note) const;
	int table_entry(int table, int index) const;
	int table_speed(int speed) const;
	// The table entry at tick t (since the note), for an effective speed (0 = never moves on).
	static int entry_index(int t, int delay, int speed);

	void put(std::vector<Write> &out, int channel, Reg reg, uint32_t value) {
		out.push_back(Write{uint8_t(channel), reg, value});
	}

	const Module &m_;
	std::vector<uint8_t> mem_;
	uint32_t empty_ = 0;
	Voice voice_[4];
	int step_ = 0, row_ = 0;
	int count_ = 1, speed_ = 6;
	int arp_counter_ = 1;
	int repeat_ = 1;
	int filter_ = 0;
	int ticks_ = 0;          // ticks since the start (the V.3 vibrato's phase)
	int pending_jump_ = -1;  // V.3: the step the next row comes from
	RowEvent event_;
	std::vector<Write> *out_ = nullptr;
};

}  // namespace smon
