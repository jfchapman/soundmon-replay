/*
 * SoundMonReplay: a renderer for Brian Postma's SoundMon music format (Amiga): BP SoundMon 1
 * (BPSM), 2 (V.2) and 3 (V.3) songs.
 *
 * Copyright (c) 2026 James Chapman. MIT licence: see LICENSE.
 *
 * A plain C API. Typical use:
 *
 *     smon_settings settings;
 *     smon_default_settings(&settings);
 *     settings.sample_rate = 48000;
 *
 *     smon_error error;
 *     smon_module *module = smon_open(data, size, &settings, &error);
 *     if (!module) { ... smon_error_string(error) ... }
 *
 *     int16_t buffer[2 * 1024];
 *     size_t frames;
 *     while ((frames = smon_render_s16(module, buffer, 1024)) > 0) {
 *         ... write 'frames' stereo frames ...
 *     }
 *     smon_close(module);
 *
 * Thread safety: a module may be used from one thread at a time. Different modules are independent.
 */
#ifndef SOUNDMONREPLAY_H
#define SOUNDMONREPLAY_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(SMON_SHARED)
#  if defined(SMON_BUILDING)
#    define SMON_API __declspec(dllexport)
#  else
#    define SMON_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(SMON_SHARED)
#  define SMON_API __attribute__((visibility("default")))
#else
#  define SMON_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SMON_VERSION_MAJOR 0
#define SMON_VERSION_MINOR 1
#define SMON_VERSION_PATCH 0

/* Returns the library version as "major.minor.patch". */
SMON_API const char *smon_version(void);

typedef struct smon_module smon_module;

typedef enum smon_error {
	SMON_OK = 0,
	SMON_ERROR_ARGUMENT, /* a null pointer or an invalid setting */
	SMON_ERROR_FORMAT,   /* not a SoundMon song, or damaged */
	SMON_ERROR_MEMORY    /* out of memory */
} smon_error;

SMON_API const char *smon_error_string(smon_error error);

typedef enum smon_interpolation {
	SMON_INTERPOLATION_NONE = 0, /* nearest sample, like the Amiga (bright, with aliasing): default */
	SMON_INTERPOLATION_LINEAR,
	SMON_INTERPOLATION_CUBIC
} smon_interpolation;

/*
 * The fixed low-pass filter on the Amiga's audio output, always in the path whatever the power
 * LED shows. The separate LED filter is smon_settings.led_filter.
 */
typedef enum smon_amiga_model {
	SMON_AMIGA_A500 = 0, /* 6 dB/octave at about 4.9 kHz: default */
	SMON_AMIGA_A1200,    /* 6 dB/octave at about 29 kHz: close to flat */
	SMON_AMIGA_NONE      /* no fixed filter */
} smon_amiga_model;

/* The "LED" filter (12 dB/octave at about 3.3 kHz). SoundMon songs can switch it (option 3). */
typedef enum smon_led_filter {
	SMON_LED_SONG = 0, /* as the song sets it, off at the start: default */
	SMON_LED_OFF,
	SMON_LED_ON
} smon_led_filter;

typedef struct smon_settings {
	int sample_rate;                  /* output rate in Hz, 8000-192000 (default 44100) */
	int stereo_separation;            /* 0 = mono, 100 = hard Amiga panning (default 100) */
	smon_interpolation interpolation; /* default SMON_INTERPOLATION_NONE */
	int volume_ramping;               /* anti-click ramp in microseconds, 0 = off (default 2000) */
	int loop_count;                   /* extra times to play a looped song: 0 = play once (default), -1 = forever */
	float gain;                       /* output gain, 1.0 = default */
	smon_amiga_model amiga_model;     /* the fixed output filter, default SMON_AMIGA_A500 */
	smon_led_filter led_filter;       /* default SMON_LED_SONG */
	float fade_seconds;               /* after its loops, a looped song plays on, fading out over this
	                                     many seconds. 0 = stop at the loop point (default) */
} smon_settings;

/* Fills settings with the defaults. */
SMON_API void smon_default_settings(smon_settings *settings);

/*
 * Opens a song. The data is copied, so the caller may free its buffer afterwards. settings may
 * be NULL for the defaults, and error may be NULL. Subsong 0 is selected.
 */
SMON_API smon_module *smon_open(const void *data, size_t size, const smon_settings *settings, smon_error *error);

/* Returns 1 if the data looks like a SoundMon song. Only the first 512 bytes are needed. */
SMON_API int smon_probe(const void *data, size_t size);

SMON_API void smon_close(smon_module *module);

/* --- song information --- */

/* The song's title (up to 26 characters) and its format version: "BPSM", "V.2" or "V.3". */
SMON_API const char *smon_title(const smon_module *module);
SMON_API const char *smon_format(const smon_module *module);

/*
 * The name of instrument 1-15, with trailing spaces removed: a sample instrument's name as the
 * editor stored it, or "" for a synth instrument (which has no name) or a number out of range.
 */
SMON_API const char *smon_instrument_name(const smon_module *module, int instrument);

/*
 * Number of subsongs: a SoundMon song can hold several, each a run of steps that loops on its
 * own. Subsong 0 starts at step 0; the others start at steps that subsong 0 never reaches.
 */
SMON_API int smon_subsong_count(const smon_module *module);

typedef struct smon_subsong_info {
	int start_step;     /* the step the subsong starts at */
	double duration;    /* one play-through in seconds, -1 if no end was found */
	double loop_start;  /* where the song continues from at the end of a play-through, in seconds; -1 if it doesn't */
	double loop_length; /* seconds from loop_start to the end of the play-through, 0 if it doesn't loop */
	int looped;         /* 1 if the song is heard to loop: its loop has notes in it. Other songs stop at
	                       the end of a play-through, whatever loop_count and fade_seconds say */
} smon_subsong_info;

/* Fills info for a subsong. Returns 0 on success. */
SMON_API int smon_get_subsong_info(const smon_module *module, int index, smon_subsong_info *info);

/*
 * Playing length of a subsong in seconds with the current settings: one play-through, plus
 * loop_count extra loops and fade_seconds if it's a looped song. -1 if no end was found.
 */
SMON_API double smon_subsong_duration(const smon_module *module, int index);

/* --- playback --- */

/* Selects and restarts a subsong. Returns 0 on success. */
SMON_API int smon_select_subsong(smon_module *module, int index);

/*
 * Renders up to 'frames' stereo frames (interleaved left, right). Returns the number rendered,
 * which is less than 'frames' only at the end of the song (0 once it has ended).
 */
SMON_API size_t smon_render_s16(smon_module *module, int16_t *buffer, size_t frames);
SMON_API size_t smon_render_float(smon_module *module, float *buffer, size_t frames);

/*
 * Seeks by time played since the start of the subsong, in seconds. Returns the new position. It
 * plays from the start without rendering audio, so the sound is exact.
 */
SMON_API double smon_seek(smon_module *module, double seconds);

/* Time played since the start of the subsong, in seconds: it keeps rising through loops. */
SMON_API double smon_position(const smon_module *module);

/*
 * Changes settings on an open module. sample_rate takes effect from the next
 * smon_select_subsong; the others take effect at once.
 */
SMON_API int smon_set_settings(smon_module *module, const smon_settings *settings);

/* Chooses which voices are heard, bit n for Paula channel n (0xF = all, the default). */
SMON_API int smon_set_voice_mask(smon_module *module, unsigned mask);

#ifdef __cplusplus
}
#endif

#endif
