/*
 * smonrender: renders a SoundMon song to a WAV file (16-bit stereo), using the public API only.
 *
 * Usage: smonrender [options] song.bp [out.wav]
 *   -r rate     sample rate (default 44100)
 *   -s n        subsong (default 0)
 *   -l n        extra loops of a looped song (default 0, -1 = forever, needs -t)
 *   -F seconds  fade-out after the loops (default 0)
 *   -t seconds  stop after this many seconds
 *   -i mode     interpolation: none (default), linear, cubic
 *   -f model    fixed output filter: a500 (default), a1200, none
 *   -L mode     LED filter: song (default), off, on
 *   -p n        stereo separation 0-100 (default 100)
 *   -d          print the subsongs and their lengths, and render nothing
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "soundmonreplay.h"

static void put32(FILE *f, unsigned v) {
	unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};
	fwrite(b, 1, 4, f);
}

static void put16(FILE *f, unsigned v) {
	unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)};
	fwrite(b, 1, 2, f);
}

static void usage(void) {
	fprintf(stderr, "usage: smonrender [-r rate] [-s subsong] [-l loops] [-F fade] [-t seconds] [-i none|linear|cubic]\n"
	                "                  [-f a500|a1200|none] [-L song|off|on] [-p separation] [-d] song.bp [out.wav]\n");
}

int main(int argc, char **argv) {
	smon_settings settings;
	int subsong = 0, describe = 0;
	double limit = -1;
	const char *in = NULL, *outname = NULL;
	int i;
	smon_default_settings(&settings);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (a[0] == '-' && a[1] && !a[2] && strchr("rslFtifLp", a[1]) && i + 1 < argc) {
			const char *v = argv[++i];
			switch (a[1]) {
			case 'r': settings.sample_rate = atoi(v); break;
			case 's': subsong = atoi(v); break;
			case 'l': settings.loop_count = atoi(v); break;
			case 'F': settings.fade_seconds = (float)atof(v); break;
			case 't': limit = atof(v); break;
			case 'i':
				settings.interpolation = !strcmp(v, "linear") ? SMON_INTERPOLATION_LINEAR
				                         : !strcmp(v, "cubic") ? SMON_INTERPOLATION_CUBIC : SMON_INTERPOLATION_NONE;
				break;
			case 'f':
				settings.amiga_model = !strcmp(v, "a1200") ? SMON_AMIGA_A1200 : !strcmp(v, "none") ? SMON_AMIGA_NONE : SMON_AMIGA_A500;
				break;
			case 'L':
				settings.led_filter = !strcmp(v, "on") ? SMON_LED_ON : !strcmp(v, "off") ? SMON_LED_OFF : SMON_LED_SONG;
				break;
			case 'p': settings.stereo_separation = atoi(v); break;
			}
		} else if (!strcmp(a, "-d")) {
			describe = 1;
		} else if (!in) {
			in = a;
		} else if (!outname) {
			outname = a;
		} else {
			usage();
			return 2;
		}
	}
	if (!in || (!outname && !describe)) {
		usage();
		return 2;
	}

	FILE *f = fopen(in, "rb");
	if (!f) {
		fprintf(stderr, "can't open %s\n", in);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *data = malloc((size_t)size);
	if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
		fclose(f);
		fprintf(stderr, "can't read %s\n", in);
		return 1;
	}
	fclose(f);

	smon_error err;
	smon_module *m = smon_open(data, (size_t)size, &settings, &err);
	free(data);
	if (!m) {
		fprintf(stderr, "%s: %s\n", in, smon_error_string(err));
		return 1;
	}
	if (describe) {
		int n = smon_subsong_count(m);
		printf("%s (%s) \"%s\": %d subsong%s\n", in, smon_format(m), smon_title(m), n, n == 1 ? "" : "s");
		for (i = 0; i < n; i++) {
			smon_subsong_info info;
			smon_get_subsong_info(m, i, &info);
			printf("  %d: step %d, %.2f s, loop from %.2f s (%.2f s)%s\n", i, info.start_step, info.duration,
			       info.loop_start, info.loop_length, info.looped ? ", looped" : "");
		}
		smon_close(m);
		return 0;
	}
	if (smon_select_subsong(m, subsong)) {
		fprintf(stderr, "no subsong %d\n", subsong);
		smon_close(m);
		return 1;
	}
	if (settings.loop_count < 0 && limit < 0) {
		fprintf(stderr, "-l -1 needs -t\n");
		smon_close(m);
		return 2;
	}

	FILE *o = fopen(outname, "wb");
	if (!o) {
		fprintf(stderr, "can't write %s\n", outname);
		smon_close(m);
		return 1;
	}
	fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, o);
	put32(o, 16);
	put16(o, 1);
	put16(o, 2);
	put32(o, (unsigned)settings.sample_rate);
	put32(o, (unsigned)settings.sample_rate * 4);
	put16(o, 4);
	put16(o, 16);
	fwrite("data\0\0\0\0", 1, 8, o);

	unsigned long total = 0, max_frames = limit >= 0 ? (unsigned long)(limit * settings.sample_rate) : 0;
	int16_t buf[2 * 4096];
	for (;;) {
		size_t want = 4096;
		if (limit >= 0 && total + want > max_frames)
			want = max_frames - total;
		if (!want)
			break;
		size_t n = smon_render_s16(m, buf, want);
		if (!n)
			break;
		for (i = 0; i < (int)(2 * n); i++)
			put16(o, (unsigned)(uint16_t)buf[i]);
		total += (unsigned long)n;
	}
	fseek(o, 4, SEEK_SET);
	put32(o, (unsigned)(36 + total * 4));
	fseek(o, 40, SEEK_SET);
	put32(o, (unsigned)(total * 4));
	fclose(o);
	smon_close(m);
	printf("%s: %.2f s\n", outname, (double)total / settings.sample_rate);
	return 0;
}
