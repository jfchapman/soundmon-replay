// smontrace: prints the replayer's Paula writes per tick, for comparison with the original
// replayers (soundmon-trace's trace.py prints the same format).
//
// Usage: smontrace <song> [ticks] [--start step] [--mem | --memdump] [--rows]
//
// Output: "tick N" lines, each followed by " channel REG value" lines (hex), REG one of LC, LEN,
// PER, VOL, DMA_ON, DMA_OFF. LC is a byte offset into the song file, or "$EMPTY" for the empty
// sample.
//
// --mem adds a " mem crc" line per tick: the CRC-32 (as zlib computes it) of the song's waveform
// area after the tick, which the synth effects rewrite in place. --memdump prints the area itself
// in hex instead (" mem <hex>").
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "module.h"
#include "replayer.h"

static uint32_t crc32(const uint8_t *p, size_t n) {
	static uint32_t table[256];
	if (!table[1]) {
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++)
		c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: smontrace <song> [ticks] [--start step] [--mem | --memdump] [--rows]\n");
		return 2;
	}
	int ticks = 500, start = 0, mem = 0;
	bool rows = false;
	for (int i = 2; i < argc; i++) {
		if (!std::strcmp(argv[i], "--start") && i + 1 < argc)
			start = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--mem"))
			mem = 1;
		else if (!std::strcmp(argv[i], "--memdump"))
			mem = 2;
		else if (!std::strcmp(argv[i], "--rows"))
			rows = true;
		else
			ticks = std::atoi(argv[i]);
	}
	FILE *f = std::fopen(argv[1], "rb");
	if (!f) {
		std::fprintf(stderr, "can't open %s\n", argv[1]);
		return 1;
	}
	std::vector<uint8_t> data;
	uint8_t buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
		data.insert(data.end(), buf, buf + n);
	std::fclose(f);

	smon::Module m;
	if (!m.load(data.data(), data.size())) {
		std::fprintf(stderr, "not a SoundMon song\n");
		return 1;
	}
	smon::Replayer r(m);
	r.start(start);
	// The waveform area, as far as the file has it.
	const size_t wave_at = std::min<size_t>(m.wave_base, data.size());
	const size_t wave_size = std::min<size_t>(size_t(m.wave_count) * 64, data.size() - wave_at);
	static const char *names[] = {"LC", "LEN", "PER", "VOL", "DMA_ON", "DMA_OFF"};
	std::vector<smon::Write> out;
	for (int t = 0; t < ticks; t++) {
		out.clear();
		r.tick(out);
		std::printf("tick %d\n", t);
		if (rows && r.last_row().read)
			std::printf(" row %d %d\n", r.last_row().step, r.last_row().row);
		for (const smon::Write &w : out) {
			if (w.reg == smon::Reg::LC && w.value == r.empty_offset())
				std::printf(" %d LC $EMPTY\n", w.channel);
			else if (w.reg == smon::Reg::DMA_ON || w.reg == smon::Reg::DMA_OFF)
				std::printf(" %d %s 0\n", w.channel, names[int(w.reg)]);
			else
				std::printf(" %d %s %X\n", w.channel, names[int(w.reg)],
				            unsigned(w.reg == smon::Reg::VOL ? (w.value & 0x7F) : w.value));
		}
		const uint8_t *area = r.memory().data() + wave_at;
		if (mem == 1) {
			std::printf(" mem %08X\n", unsigned(crc32(area, wave_size)));
		} else if (mem == 2) {
			std::printf(" mem ");
			for (size_t i = 0; i < wave_size; i++)
				std::printf("%02X", area[i]);
			std::printf("\n");
		}
	}
	return 0;
}
