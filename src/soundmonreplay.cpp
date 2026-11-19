// SoundMonReplay: the C API.
#define SMON_BUILDING
#include "soundmonreplay.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <new>
#include <set>
#include <tuple>
#include <vector>

#include "module.h"
#include "paula.h"
#include "replayer.h"

namespace {

// One tick is one PAL frame: 313 lines of 227 colour clocks.
const double kTickSeconds = 313.0 * 227.0 / smon::kPaulaClock;
const int kMaxTicks = int(30 * 60 / kTickSeconds);  // 30 minutes

struct Subsong {
	int start_step = 0;
	int ticks = -1;       // one play-through
	int loop_tick = -1;   // where it continues from
	bool looped = false;
	bool audible = false;
};

// Plays a song from a step without sound and finds where it starts repeating: the first row read
// a second time in the same state (step, row and, for V.2, the repeat counter).
Subsong analyse(const smon::Module &m, int start, std::set<int> &visited) {
	Subsong s;
	s.start_step = start;
	smon::Replayer r(m);
	r.start(start);
	std::map<std::tuple<int, int, int>, int> seen;
	std::vector<int> note_ticks;
	std::vector<smon::Write> out;
	for (int t = 0; t < kMaxTicks; t++) {
		out.clear();
		r.tick(out);
		for (const smon::Write &w : out)
			if (w.reg == smon::Reg::DMA_ON) {
				note_ticks.push_back(t);
				break;
			}
		const smon::Replayer::RowEvent &e = r.last_row();
		if (!e.read)
			continue;
		visited.insert(e.step);
		// A repeat counter that has wrapped (a loop without option 6 runs 255 times) counts as one
		// state, so such a loop is found on its second pass.
		const int rep = std::min(r.repeat_counter(), 9);
		auto key = std::make_tuple(e.step, e.row, m.version == smon::Version::V3 ? 0 : rep);
		auto it = seen.find(key);
		if (it != seen.end()) {
			s.ticks = t;
			s.loop_tick = it->second;
			break;
		}
		seen[key] = t;
	}
	s.audible = !note_ticks.empty();
	if (s.ticks > 0) {
		for (int t : note_ticks)
			if (t >= s.loop_tick && t < s.ticks)
				s.looped = true;
	}
	return s;
}

}  // namespace

struct smon_module {
	smon::Module song;
	smon::Replayer *replayer = nullptr;
	smon::Paula paula;
	smon_settings settings;
	std::vector<Subsong> subsongs;
	int current = 0;

	std::vector<smon::Write> writes;
	long long ticks_done = 0;     // ticks played since the start of the subsong
	double tick_frames_left = 0;  // output frames left in the current tick
	double frames_per_tick = 0;
	long long frames_done = 0;
	long long end_frame = -1;     // where playback stops, -1 = never
	long long fade_from = -1;     // where the fade-out starts
	std::vector<float> mix;

	~smon_module() { delete replayer; }
};

extern "C" {

const char *smon_version(void) { return "0.1.0"; }

const char *smon_error_string(smon_error error) {
	switch (error) {
	case SMON_OK:
		return "no error";
	case SMON_ERROR_ARGUMENT:
		return "invalid argument";
	case SMON_ERROR_FORMAT:
		return "not a SoundMon song";
	case SMON_ERROR_MEMORY:
		return "out of memory";
	}
	return "unknown error";
}

void smon_default_settings(smon_settings *s) {
	if (!s)
		return;
	s->sample_rate = 44100;
	s->stereo_separation = 100;
	s->interpolation = SMON_INTERPOLATION_NONE;
	s->volume_ramping = 2000;
	s->loop_count = 0;
	s->gain = 1.0f;
	s->amiga_model = SMON_AMIGA_A500;
	s->led_filter = SMON_LED_SONG;
	s->fade_seconds = 0;
}

static bool valid(const smon_settings *s) {
	return s->sample_rate >= 8000 && s->sample_rate <= 192000 && s->stereo_separation >= 0 &&
	       s->stereo_separation <= 100 && s->volume_ramping >= 0 && s->loop_count >= -1 && s->gain >= 0 &&
	       s->fade_seconds >= 0;
}

static void apply_output(smon_module *m) {
	const smon_settings &s = m->settings;
	m->paula.set_output(s.sample_rate, s.interpolation, s.volume_ramping, s.stereo_separation, s.gain, s.amiga_model);
}

static double play_seconds(const smon_module *m, const Subsong &s) {
	if (s.ticks < 0)
		return -1;
	double t = s.ticks * kTickSeconds;
	if (s.looped) {
		if (m->settings.loop_count < 0)
			return -1;
		t += m->settings.loop_count * (s.ticks - s.loop_tick) * kTickSeconds + m->settings.fade_seconds;
	}
	return t;
}

int smon_select_subsong(smon_module *m, int index) {
	if (!m || index < 0 || index >= int(m->subsongs.size()))
		return -1;
	m->current = index;
	const Subsong &s = m->subsongs[size_t(index)];
	m->replayer->start(s.start_step);
	m->paula.reset();
	m->paula.set_memory(&m->replayer->memory());
	apply_output(m);
	m->paula.set_led(m->settings.led_filter == SMON_LED_ON);
	m->ticks_done = 0;
	m->tick_frames_left = 0;
	m->frames_done = 0;
	m->frames_per_tick = kTickSeconds * m->settings.sample_rate;
	const double secs = play_seconds(m, s);
	m->end_frame = secs < 0 ? -1 : (long long)(secs * m->settings.sample_rate);
	m->fade_from = (s.looped && m->settings.fade_seconds > 0 && m->end_frame >= 0)
	                   ? m->end_frame - (long long)(m->settings.fade_seconds * m->settings.sample_rate)
	                   : -1;
	return 0;
}

smon_module *smon_open(const void *data, size_t size, const smon_settings *settings, smon_error *error) {
	smon_error dummy;
	smon_error &err = error ? *error : dummy;
	if (!data) {
		err = SMON_ERROR_ARGUMENT;
		return nullptr;
	}
	smon_module *m = new (std::nothrow) smon_module();
	if (!m) {
		err = SMON_ERROR_MEMORY;
		return nullptr;
	}
	if (settings) {
		if (!valid(settings)) {
			delete m;
			err = SMON_ERROR_ARGUMENT;
			return nullptr;
		}
		m->settings = *settings;
	} else {
		smon_default_settings(&m->settings);
	}
	try {
		if (!m->song.load(static_cast<const uint8_t *>(data), size)) {
			delete m;
			err = SMON_ERROR_FORMAT;
			return nullptr;
		}
		std::set<int> visited;
		m->subsongs.push_back(analyse(m->song, 0, visited));
		for (int s = 0; s < m->song.step_count; s++) {
			if (visited.count(s))
				continue;
			Subsong sub = analyse(m->song, s, visited);
			if (sub.audible)
				m->subsongs.push_back(sub);
		}
		m->replayer = new smon::Replayer(m->song);
	} catch (const std::bad_alloc &) {
		delete m;
		err = SMON_ERROR_MEMORY;
		return nullptr;
	}
	smon_select_subsong(m, 0);
	err = SMON_OK;
	return m;
}

int smon_probe(const void *data, size_t size) {
	return smon::Module::probe(static_cast<const uint8_t *>(data), size) ? 1 : 0;
}

void smon_close(smon_module *m) { delete m; }

const char *smon_title(const smon_module *m) { return m ? m->song.title.c_str() : ""; }

const char *smon_format(const smon_module *m) {
	if (!m)
		return "";
	return m->song.version == smon::Version::BPSM ? "BPSM" : m->song.version == smon::Version::V3 ? "V.3" : "V.2";
}

const char *smon_instrument_name(const smon_module *m, int instrument) {
	if (!m || instrument < 1 || instrument > 15 || m->song.instruments[instrument].synth)
		return "";
	return m->song.instruments[instrument].sample.name.c_str();
}

int smon_subsong_count(const smon_module *m) { return m ? int(m->subsongs.size()) : 0; }

int smon_get_subsong_info(const smon_module *m, int index, smon_subsong_info *info) {
	if (!m || !info || index < 0 || index >= int(m->subsongs.size()))
		return -1;
	const Subsong &s = m->subsongs[size_t(index)];
	info->start_step = s.start_step;
	info->duration = s.ticks < 0 ? -1 : s.ticks * kTickSeconds;
	info->loop_start = s.ticks < 0 ? -1 : s.loop_tick * kTickSeconds;
	info->loop_length = s.ticks < 0 ? 0 : (s.ticks - s.loop_tick) * kTickSeconds;
	info->looped = s.looped ? 1 : 0;
	return 0;
}

double smon_subsong_duration(const smon_module *m, int index) {
	if (!m || index < 0 || index >= int(m->subsongs.size()))
		return -1;
	return play_seconds(m, m->subsongs[size_t(index)]);
}

static void run_tick(smon_module *m) {
	m->writes.clear();
	m->replayer->tick(m->writes);
	for (const smon::Write &w : m->writes) {
		smon::Channel &c = m->paula.channel(w.channel);
		switch (w.reg) {
		case smon::Reg::LC:
			c.lc = w.value;
			break;
		case smon::Reg::LEN:
			c.len = uint16_t(w.value);
			break;
		case smon::Reg::PER:
			c.per = uint16_t(w.value);
			break;
		case smon::Reg::VOL:
			c.vol = int(w.value & 0x7F);
			break;
		case smon::Reg::DMA_ON:
			m->paula.dma_on(w.channel);
			break;
		case smon::Reg::DMA_OFF:
			m->paula.dma_off(w.channel);
			break;
		}
	}
	if (m->settings.led_filter == SMON_LED_SONG)
		m->paula.set_led(m->replayer->filter() != 0);
	m->ticks_done++;
}

size_t smon_render_float(smon_module *m, float *buffer, size_t frames) {
	if (!m || !buffer)
		return 0;
	size_t done = 0;
	while (done < frames) {
		if (m->end_frame >= 0 && m->frames_done >= m->end_frame)
			break;
		if (m->tick_frames_left < 1) {
			run_tick(m);
			m->tick_frames_left += m->frames_per_tick;
		}
		size_t n = std::min<size_t>(frames - done, size_t(m->tick_frames_left));
		if (m->end_frame >= 0)
			n = std::min<size_t>(n, size_t(m->end_frame - m->frames_done));
		if (n == 0)
			n = 1;
		float *out = buffer + 2 * done;
		std::fill(out, out + 2 * n, 0.0f);
		m->paula.render(out, int(n));
		if (m->fade_from >= 0) {
			const double span = double(m->end_frame - m->fade_from);
			for (size_t i = 0; i < n; i++) {
				const long long f = m->frames_done + (long long)i;
				if (f > m->fade_from) {
					const float g = float(std::max(0.0, 1.0 - double(f - m->fade_from) / span));
					out[2 * i] *= g;
					out[2 * i + 1] *= g;
				}
			}
		}
		m->tick_frames_left -= double(n);
		m->frames_done += (long long)n;
		done += n;
	}
	return done;
}

size_t smon_render_s16(smon_module *m, int16_t *buffer, size_t frames) {
	if (!m || !buffer)
		return 0;
	m->mix.resize(frames * 2);
	const size_t n = smon_render_float(m, m->mix.data(), frames);
	for (size_t i = 0; i < 2 * n; i++)
		buffer[i] = int16_t(std::lround(std::clamp(m->mix[i], -1.0f, 1.0f) * 32767.0f));
	return n;
}

double smon_seek(smon_module *m, double seconds) {
	if (!m)
		return 0;
	smon_select_subsong(m, m->current);
	const long long target = (long long)(std::max(0.0, seconds) / kTickSeconds);
	while (m->ticks_done < target) {
		if (m->end_frame >= 0 && double(m->ticks_done + 1) * m->frames_per_tick > double(m->end_frame))
			break;
		run_tick(m);
	}
	m->frames_done = (long long)(double(m->ticks_done) * m->frames_per_tick);
	m->tick_frames_left = 0;
	return m->ticks_done * kTickSeconds;
}

double smon_position(const smon_module *m) {
	return m ? double(m->frames_done) / m->settings.sample_rate : 0;
}

int smon_set_settings(smon_module *m, const smon_settings *settings) {
	if (!m || !settings || !valid(settings))
		return -1;
	const int rate = m->settings.sample_rate;
	m->settings = *settings;
	m->settings.sample_rate = rate;
	apply_output(m);
	if (m->settings.led_filter != SMON_LED_SONG)
		m->paula.set_led(m->settings.led_filter == SMON_LED_ON);
	return 0;
}

int smon_set_voice_mask(smon_module *m, unsigned mask) {
	if (!m)
		return -1;
	m->paula.set_voice_mask(mask);
	return 0;
}

}  // extern "C"
