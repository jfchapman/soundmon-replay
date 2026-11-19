// smontrace: prints the replayer's Paula writes per tick, for comparison with the original
// replayers (soundmon-trace's trace.py prints the same format).
//
// Usage: smontrace <song> [ticks] [--start step]
//
// Output: "tick N" lines, each followed by " channel REG value" lines (hex), REG one of LC, LEN,
// PER, VOL, DMA_ON, DMA_OFF. LC is a byte offset into the song file, or "$EMPTY" for the empty
// sample.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "module.h"
#include "replayer.h"

int main(int argc, char **argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: smontrace <song> [ticks] [--start step]\n");
		return 2;
	}
	int ticks = 500, start = 0;
	for (int i = 2; i < argc; i++) {
		if (!std::strcmp(argv[i], "--start") && i + 1 < argc)
			start = std::atoi(argv[++i]);
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
	static const char *names[] = {"LC", "LEN", "PER", "VOL", "DMA_ON", "DMA_OFF"};
	std::vector<smon::Write> out;
	for (int t = 0; t < ticks; t++) {
		out.clear();
		r.tick(out);
		std::printf("tick %d\n", t);
		for (const smon::Write &w : out) {
			if (w.reg == smon::Reg::LC && w.value == r.empty_offset())
				std::printf(" %d LC $EMPTY\n", w.channel);
			else if (w.reg == smon::Reg::DMA_ON || w.reg == smon::Reg::DMA_OFF)
				std::printf(" %d %s 0\n", w.channel, names[int(w.reg)]);
			else
				std::printf(" %d %s %X\n", w.channel, names[int(w.reg)],
				            unsigned(w.reg == smon::Reg::VOL ? (w.value & 0x7F) : w.value));
		}
	}
	return 0;
}
