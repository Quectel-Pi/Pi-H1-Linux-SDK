/* pi-voice-inputd -- push-to-talk dictation for the Quectel PI H1 (QCS6490).
 *
 * The daemon owns no window. Its two surfaces are:
 *   - the GNOME status-area switch, drawn by the companion extension
 *     (gnome-shell-extensions/pi-voice-input@quectel.com), and
 *   - the text it recognizes, which the extension commits into the focused
 *     application through the shell's input-method path.
 * Both talk to this process over the session bus (org.quectel.PiVoiceInput).
 *
 * Hold to dictate, release to insert. The push-to-talk key is the right Ctrl
 * key held on its own, read by pi-voice-hotkey straight from /dev/input: a
 * GNOME custom keybinding only fires on press, so it cannot express
 * "release ends the utterance". It is also paired with a custom keybinding
 * (Ctrl+Q -> "pi-voice-input client") as the fallback for a session that
 * cannot read the input devices; that client is a separate, short-lived
 * program because a Wayland client cannot grab a global hotkey by itself. The
 * client only forwards "press" / "release" over the bus, which keeps this
 * daemon the single owner of the microphone.
 *
 * Everything runs unprivileged: the microphone is read through PipeWire's
 * native client API as the logged-in user, and recognition runs in-process on
 * the CPU. There is no DSP session to leak; README.md explains
 * why speech recognition stays off the HTP that the LLM owns.
 *
 * Recognition uses the sherpa-onnx C API with the SenseVoice int8 model that
 * the npu-llm package already installs -- same model, same tokens, same decode
 * settings as npu-chat, so both paths transcribe identically instead of
 * drifting apart.
 */

#define _GNU_SOURCE

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <dbus/dbus.h>
#include <ctype.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "sherpa-onnx/c-api/c-api.h"

#define BUS_NAME "org.quectel.PiVoiceInput"
#define BUS_PATH "/org/quectel/PiVoiceInput"
#define BUS_IFACE "org.quectel.PiVoiceInput"

#define CAPTURE_RATE 16000
#define CAPTURE_CHANNELS 1
/* What the PAL capture nodes expose on this board (pal_source_speaker_mic and
 * pal_source_headset_mic both: S16LE, 48 kHz, 2 ch, position FL FR). Asking the
 * graph for 16 kHz mono makes the stream sit unconnected on this image instead
 * of getting an audioconvert in front of it, so we ask for what the node
 * actually produces and downmix ourselves -- 3:1 decimation plus an average of
 * the two channels, done with a box filter before the ASR stream sees it. */
#define NODE_RATE 48000
#define NODE_CHANNELS 2
/* Samples of the node format per CAPTURE_RATE sample: 3. */
#define DECIMATE (NODE_RATE / CAPTURE_RATE)
#define MAX_RECORD_SECONDS 60
/* Measured on this board: ambient is ~40, normal speech ~400 (the same figures
 * npu-chat uses for its hands-free threshold). Below SILENCE_RMS the utterance
 * is dropped instead of decoded -- decoding noise produces hallucinated text. */
#define SILENCE_RMS 60.0
/* A release before this is a mis-click: discard without decoding. */
#define MIN_UTTERANCE_MS 250

/* ------------------------------------------------------------------ config */

static struct {
	/* Node name (or description) of the PipeWire capture source. Empty means
	 * "whatever PipeWire elected as the default source", which is the right
	 * answer on a board whose PAL node has a different name. */
	char *pw_node;
	char *asr_model;
	char *asr_tokens;
	char *language;
	char *state_file;
	int num_threads;
} opt;

/* ------------------------------------------------------------------- state */

/* Guarded by state_lock. state_name/lang point at string literals. */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static bool enabled;
static bool recording;
static const char *state_name = "off";

struct snapshot {
	const char *state;
	bool enabled;
};

/* Whether a device that could actually carry the push-to-talk key is attached.
 * Answered without opening anything for reading: this runs inside a D-Bus method
 * handler, and blocking there would stall the whole daemon.
 *
 * "Could carry right Ctrl" is stricter than "has a kbd handler", and it has to
 * be: a Goodix touchscreen registers Handlers=kbd mouse0 eventN, so a presence
 * check based on the kbd handler alone reports "keyboard" on a panel-only board
 * and the on-screen button never appears (measured on the board: 3-0014 Goodix
 * Capacitive TouchScreen, ABS=265800000000003, no letter keys).
 *
 * The rule is the same one pi-voice-hotkey uses to decide whether a device is
 * worth reading (is_keyboard(): EV_KEY + KEY_Q + KEY_LEFTCTRL), expressed in
 * sysfs so that nothing has to be opened. What is needed from it:
 *  - KEY_Q is bit 16 of the FIRST 64-bit word of capabilities/key, because the
 *    words are printed most-significant first: Q lives in the last word. A
 *    real keyboard has f800000000000000 there (Q..U plus the Ctrl/Alt keys),
 *    the touchscreen has 2000000000000001 (only BTN_TOUCH and BTN_TOOL_FINGER).
 *  - test bit 88 (KEY_LEFTCTRL) as well, so a bar-code scanner or a
 *    volume-key device that happens to decode letters is not mistaken for a
 *    keyboard the user can dictate with.
 *
 * Any read failure falls back to "present": showing a floating button on a desk
 * that has a keyboard is worse than hiding it once on a board that does not. */
#define SYSFS_SCAN_KEYS 256

/* The capabilities file lists one word per line, most significant first. Read
 * them back into a little-endian bit array so KEY_x can be tested by number. */
static bool sysfs_key_capable(const char *event)
{
	char path[256];
	FILE *f;
	unsigned char bits[SYSFS_SCAN_KEYS / 8];
	unsigned long words[16];
	int nwords = 0, i;

	memset(bits, 0, sizeof(bits));
	snprintf(path, sizeof(path),
		 "/sys/class/input/%s/device/capabilities/key", event);
	f = fopen(path, "re");
	if (!f)
		return false;
	while (nwords < (int)(sizeof(words) / sizeof(words[0])) &&
	       fscanf(f, "%lx", &words[nwords]) == 1)
		nwords++;
	fclose(f);
	if (nwords == 0)
		return false;
	/* words[0] is the most significant, so word k of n covers bit numbers
	 * [ (n-1-k)*64, (n-1-k)*64 + 63 ]. */
	for (i = 0; i < nwords; i++) {
		unsigned long w = words[i];
		int bit;

		for (bit = 0; bit < 64; bit++) {
			unsigned long number;

			if (!(w & (1UL << bit)))
				continue;
			number = (unsigned long)(nwords - 1 - i) * 64UL +
				 (unsigned long)bit;
			if (number < SYSFS_SCAN_KEYS)
				bits[number / 8] |=
					(unsigned char)(1u << (number % 8));
		}
	}
	/* KEY_Q = 16, KEY_LEFTCTRL = 29, KEY_RIGHTCTRL = 97 (input-event-codes.h;
	 * the headers are not shipped with the daemon, hence the numbers). Only
	 * KEY_Q and KEY_LEFTCTRL matter -- the same pair is_keyboard() checks. */
	if (!(bits[16 / 8] & (1u << (16 % 8))))
		return false;
	if (!(bits[29 / 8] & (1u << (29 % 8))))
		return false;
	return true;
}

static bool input_keyboard_present(void)
{
	DIR *d;
	struct dirent *e;
	bool saw_event = false;

	d = opendir("/sys/class/input");
	if (!d)
		return true; /* cannot tell -> do not add a button */
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		saw_event = true;
		if (sysfs_key_capable(e->d_name)) {
			closedir(d);
			return true;
		}
	}
	closedir(d);
	if (!saw_event)
		return true; /* sysfs not mounted as expected -> do not add a button */
	return false;
}

static struct snapshot snap(void)
{
	struct snapshot s;

	pthread_mutex_lock(&state_lock);
	s.state = state_name;
	s.enabled = enabled;
	pthread_mutex_unlock(&state_lock);
	return s;
}

static void set_state(const char *name)
{
	pthread_mutex_lock(&state_lock);
	state_name = name;
	pthread_mutex_unlock(&state_lock);
}

static DBusConnection *bus_conn;
/* Set by any thread that queued a message; the main loop flushes it. */
static volatile bool queue_flush;
/* Set when the bus name belongs to another instance of this daemon. */
static bool another_owner;
static int boot_count;
/* The position on disk, which is what the next login will restore. It differs
 * from `enabled` only while a save is failing (read-only home, full disk), and
 * that difference is exactly what a maintainer needs to see, so it is exposed
 * as its own D-Bus property instead of being inferred from the state. */
static bool remembered;
static volatile bool quit;
static volatile bool stop_capture;
static pthread_t worker;
static bool worker_running;

/* Only the worker thread decodes, so these two are not otherwise shared. They
 * are still protected because disable() frees the recognizer from the dispatch
 * thread and must not race a decode. */
static const SherpaOnnxOfflineRecognizer *recognizer;

static void logmsg(const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(stderr, "pi-voice-inputd [%llu.%03lu]: %s\n",
		(unsigned long long)ts.tv_sec, ts.tv_nsec / 1000000, buf);
}

/* ----------------------------------------------------------- persistence  */

/* The switch position outlives the session: whatever the user last chose
 * decides whether the model is loaded at login, which is the whole point of
 * "turn it off and the memory is gone". The value is one word, written to a
 * temporary file and renamed into place, so an interrupted write (power cut
 * while the switch is flipped) cannot leave a torn value behind -- rename(2)
 * within one directory is atomic, a plain truncate+write is not.
 *
 * Absent file means off. A freshly flashed board must not spend 356 MB on a
 * feature nobody has switched on yet. */
static void mkdir_parents(const char *path)
{
	char buf[PATH_MAX];
	char *p;
	size_t len;

	len = strlen(path);
	if (len == 0 || len >= sizeof(buf))
		return;
	memcpy(buf, path, len + 1);
	for (p = buf + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		mkdir(buf, 0700);
		*p = '/';
	}
}

/* Returns true when the recorded position is "on". Anything unreadable or
 * unrecognised is treated as off: the safe direction is not holding 356 MB. */
static bool state_load(void)
{
	char buf[32];
	FILE *f;
	size_t n;

	if (!opt.state_file)
		return false;
	f = fopen(opt.state_file, "r");
	if (!f) {
		if (errno != ENOENT)
			logmsg("cannot read %s: %s", opt.state_file,
			       strerror(errno));
		return false;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ' ||
			 buf[n - 1] == '\r'))
		buf[--n] = '\0';
	return strcmp(buf, "on") == 0;
}

/* Best effort: a read-only home or a full disk must not stop the switch from
 * working in this session, it only means the choice will not survive a reboot.
 * That is logged so the cause is visible instead of looking like the feature
 * forgetting things. */
static void state_save(bool en)
{
	char tmp[PATH_MAX];
	FILE *f;

	if (!opt.state_file)
		return;
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", opt.state_file) >= (int)sizeof(tmp))
		return;
	f = fopen(tmp, "w");
	if (!f) {
		logmsg("cannot write %s: %s (choice will not be remembered)",
		       tmp, strerror(errno));
		return;
	}
	fprintf(f, "%s\n", en ? "on" : "off");
	if (fflush(f) != 0 || fsync(fileno(f)) != 0)
		logmsg("cannot sync %s: %s", tmp, strerror(errno));
	fclose(f);
	if (rename(tmp, opt.state_file) != 0) {
		logmsg("cannot rename %s -> %s: %s", tmp, opt.state_file,
		       strerror(errno));
		unlink(tmp);
	}
}

/* Recognition uses the sherpa-onnx C API that npu-llm installs privately in
 * /usr/lib/npu-llm/voice/sherpa_onnx/lib. It is loaded with dlopen() instead of
 * being linked: that directory is not on the linker path (the package keeps a
 * private copy of libonnxruntime.so next to it and sets $ORIGIN rpath), and
 * dlopen lets a board without the package fail with one clear message instead
 * of a loader error before main(). */
#define SHERPA_LIB "libsherpa-onnx-c-api.so"
#define SHERPA_LIBDIR "/usr/lib/npu-llm/voice/sherpa_onnx/lib"
/* libonnxruntime.so is a NEEDED of the sherpa library and is found through its
 * own $ORIGIN rpath, but it has to be loaded first when the sherpa objects live
 * in a directory RTLD does not search. */
#define SHERPA_ORT "libonnxruntime.so"

static const SherpaOnnxOfflineRecognizer *(*api_create)(
	const SherpaOnnxOfflineRecognizerConfig *);
static void (*api_destroy)(const SherpaOnnxOfflineRecognizer *);
static const SherpaOnnxOfflineStream *(*api_stream_new)(
	const SherpaOnnxOfflineRecognizer *);
static void (*api_stream_free)(const SherpaOnnxOfflineStream *);
static void (*api_accept)(const SherpaOnnxOfflineStream *, int32_t,
			  const float *, int32_t);
static void (*api_decode)(const SherpaOnnxOfflineRecognizer *,
			  const SherpaOnnxOfflineStream *);
static const SherpaOnnxOfflineRecognizerResult *(*api_result)(
	const SherpaOnnxOfflineStream *);
static void (*api_result_free)(const SherpaOnnxOfflineRecognizerResult *);

/* Handles of the two shared objects, kept so "switch off" can unmap them again.
 * Destroying the recognizer frees the weights, but the library's own text/data
 * stays mapped and its heap arenas stay with it -- measured on the board, RSS
 * went 357 MB -> 148 MB on destroy alone and -> 38.7 MB after malloc_trim, still
 * well above the 2.4 MB the daemon uses before the first load. Unmapping the
 * libraries is what actually returns that last block, which is why the switch
 * can claim to unload rather than to "mostly free". */
static void *sherpa_ort_handle;
static void *sherpa_lib_handle;

/* 0 = never loaded, 1 = loaded, -1 = load failed. Reset by sherpa_unload() so
 * that "switch off, switch on" re-runs the load. */
static int sherpa_state;

/* Returns 0 on success. Idempotent while loaded. */
static int sherpa_load(void)
{
	char path[PATH_MAX];
	void *ort, *lib;

	if (sherpa_state)
		return sherpa_state > 0 ? 0 : -1;
	snprintf(path, sizeof(path), "%s/%s", SHERPA_LIBDIR, SHERPA_ORT);
	ort = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	if (!ort) {
		logmsg("cannot load %s: %s", path, dlerror());
		sherpa_state = -1;
		return -1;
	}
	snprintf(path, sizeof(path), "%s/%s", SHERPA_LIBDIR, SHERPA_LIB);
	lib = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	if (!lib) {
		logmsg("cannot load %s: %s", path, dlerror());
		dlclose(ort);
		sherpa_state = -1;
		return -1;
	}

	api_create = dlsym(lib, "SherpaOnnxCreateOfflineRecognizer");
	api_destroy = dlsym(lib, "SherpaOnnxDestroyOfflineRecognizer");
	api_stream_new = dlsym(lib, "SherpaOnnxCreateOfflineStream");
	api_stream_free = dlsym(lib, "SherpaOnnxDestroyOfflineStream");
	api_accept = dlsym(lib, "SherpaOnnxAcceptWaveformOffline");
	api_decode = dlsym(lib, "SherpaOnnxDecodeOfflineStream");
	api_result = dlsym(lib, "SherpaOnnxGetOfflineStreamResult");
	api_result_free = dlsym(lib, "SherpaOnnxDestroyOfflineRecognizerResult");
	if (!api_create || !api_destroy || !api_stream_new || !api_stream_free ||
	    !api_accept || !api_decode || !api_result || !api_result_free) {
		logmsg("sherpa-onnx C API is incomplete in %s", path);
		dlclose(lib);
		dlclose(ort);
		sherpa_state = -1;
		return -1;
	}
	sherpa_ort_handle = ort;
	sherpa_lib_handle = lib;
	sherpa_state = 1;
	return 0;
}

/* Unmaps the libraries and clears the entry points. Only called after the
 * recognizer is destroyed and no decode is running (see recognizer_free()).
 * After this, sherpa_load() starts over from the top, so a later "on" loads
 * everything again -- the cost is the 1-2 s load, which is why the daemon warms
 * the model on press rather than on release. */
static void sherpa_unload(void)
{
	if (sherpa_lib_handle) {
		dlclose(sherpa_lib_handle);
		sherpa_lib_handle = NULL;
	}
	if (sherpa_ort_handle) {
		dlclose(sherpa_ort_handle);
		sherpa_ort_handle = NULL;
	}
	api_create = NULL;
	api_destroy = NULL;
	api_stream_new = NULL;
	api_stream_free = NULL;
	api_accept = NULL;
	api_decode = NULL;
	api_result = NULL;
	api_result_free = NULL;
	sherpa_state = 0;
}

/* -------------------------------------------------------------- dbus emit */

static void emit_state_changed(void)
{
	DBusMessage *m;
	struct snapshot s = snap();
	dbus_bool_t en = s.enabled ? TRUE : FALSE;
	dbus_uint32_t boot = (dbus_uint32_t)boot_count;
	const char *st = s.state;

	if (!bus_conn)
		return;
	m = dbus_message_new_signal(BUS_PATH, BUS_IFACE, "StateChanged");
	if (!m)
		return;
	dbus_message_append_args(m, DBUS_TYPE_STRING, &st, DBUS_TYPE_BOOLEAN, &en,
				 DBUS_TYPE_UINT32, &boot, DBUS_TYPE_INVALID);
	/* dbus_connection_send() is thread safe (libdbus serialises on the
	 * connection lock), but dbus_connection_flush() is NOT cheap from a
	 * worker thread: it blocks until the socket accepts the bytes, and with
	 * the main loop dispatching on the same connection that measured 4.1 s
	 * between "dictate: after set_state" and the next log line. The worker
	 * therefore only queues the message and the main loop flushes on its
	 * next turn (see the iterate loop in main()). Signals are replaced
	 * rather than accumulated: only the latest state matters. */
	dbus_connection_send(bus_conn, m, NULL);
	dbus_message_unref(m);
	queue_flush = true;
}

/* SenseVoice is trained on punctuated text and dutifully appends 。，？！ to every
 * sentence. For push-to-talk dictation that is noise: the text goes into a chat
 * box, a search field or a terminal, not into a document, and nobody says
 * "period" out loud. So punctuation is removed before the text is handed to the
 * shell.
 *
 * Removal is UTF-8 aware: CJK punctuation is 3 bytes, so the buffer is compacted
 * byte-wise but only over whole characters, and an unknown multibyte character
 * is always copied intact. The raw transcript is still logged, so a removal that
 * was wrong for a given phrase can be diagnosed afterwards.
 *
 * Only punctuation is removed: Latin letters, digits, spaces and every CJK
 * character stay, so "20:30" becomes "2030" but "测试" is untouched. */
static const char *const punct_utf8[] = {
	"\xef\xbc\x8c", /* U+FF0C ， */
	"\xe3\x80\x82", /* U+3002 。 */
	"\xe3\x80\x81", /* U+3001 、 */
	"\xef\xbc\x9b", /* U+FF1B ； */
	"\xef\xbc\x9a", /* U+FF1A ： */
	"\xef\xbc\x9f", /* U+FF1F ？ */
	"\xef\xbc\x81", /* U+FF01 ！ */
	"\xe2\x80\xa6", /* U+2026 … */
	"\xe2\x80\x94", /* U+2014 — */
	"\xe2\x80\x93", /* U+2013 – */
	"\xef\xbd\x9e", /* U+FF5E ～ */
	"\xe2\x80\x9c", /* U+201C “ */
	"\xe2\x80\x9d", /* U+201D ” */
	"\xe2\x80\x98", /* U+2018 ‘ */
	"\xe2\x80\x99", /* U+2019 ’ */
	"\xe3\x80\x8a", /* U+300A 《 */
	"\xe3\x80\x8b", /* U+300B 》 */
	"\xe3\x80\x8c", /* U+300C 「 */
	"\xe3\x80\x8d", /* U+300D 」 */
	"\xe3\x80\x8e", /* U+300E 『 */
	"\xe3\x80\x8f", /* U+300F 』 */
	"\xef\xbc\x88", /* U+FF08 （ */
	"\xef\xbc\x89", /* U+FF09 ） */
	"\xe3\x80\x90", /* U+3010 【 */
	"\xe3\x80\x91", /* U+3011 】 */
	"\xe3\x80\x88", /* U+3008 〈 */
	"\xe3\x80\x89", /* U+3009 〉 */
	"\xe3\x80\x94", /* U+3014 〔 */
	"\xe3\x80\x95", /* U+3015 〕 */
	"\xe3\x80\x80", /* U+3000 ideographic space (same role as a space here) */
	"\xc2\xb7",     /* U+00B7 · */
	"\xe2\x80\xbb", /* U+203B ※ */
	NULL,
};

/* Returns s, compacted in place. */
static char *strip_punctuation(char *s)
{
	size_t r = 0, w = 0;

	while (s[r] != '\0') {
		unsigned char c = (unsigned char)s[r];
		size_t len = 1, i;
		bool drop = false;

		if (c < 0x80) {
			drop = ispunct(c) != 0;
		} else {
			/* Length of this UTF-8 sequence; a stray continuation byte
			 * (already corrupt input) is treated as its own character so
			 * nothing is silently truncated. */
			if ((c & 0xe0) == 0xc0)
				len = 2;
			else if ((c & 0xf0) == 0xe0)
				len = 3;
			else if ((c & 0xf8) == 0xf0)
				len = 4;
			if (r + len > strlen(s))
				len = 1;
			for (i = 0; punct_utf8[i]; i++) {
				if (strncmp(s + r, punct_utf8[i], len) == 0 &&
				    strlen(punct_utf8[i]) == len) {
					drop = true;
					break;
				}
			}
		}
		if (drop) {
			r += len;
			continue;
		}
		if (r != w) {
			for (i = 0; i < len; i++)
				s[w + i] = s[r + i];
		}
		w += len;
		r += len;
	}
	s[w] = '\0';

	/* Removing punctuation leaves doubled and trailing ASCII spaces, and the
	 * shell inserts the string verbatim. */
	{
		size_t i, o = 0;
		bool prev_space = true; /* also trims the leading run */

		for (i = 0; s[i] != '\0'; i++) {
			if (s[i] == ' ' || s[i] == '\t') {
				if (prev_space)
					continue;
				prev_space = true;
				s[o++] = ' ';
				continue;
			}
			prev_space = false;
			s[o++] = s[i];
		}
		while (o > 0 && s[o - 1] == ' ')
			o--;
		s[o] = '\0';
	}
	return s;
}

static void emit_text(const char *text)
{
	DBusMessage *m;
	char *clean;
	const char *payload;

	if (!text || !*text || !bus_conn)
		return;
	logmsg("recognized: %s", text);
	clean = strdup(text);
	if (!clean)
		return;
	strip_punctuation(clean);
	if (!*clean) {
		/* The utterance was nothing but punctuation, which is what a
		 * cough or a door slam decodes to. Inserting an empty string would
		 * look like the feature did nothing at all. */
		logmsg("empty after removing punctuation; not inserting");
		free(clean);
		return;
	}
	if (strcmp(clean, text) != 0)
		logmsg("inserting without punctuation: %s", clean);
	payload = clean;
	m = dbus_message_new_signal(BUS_PATH, BUS_IFACE, "TextRecognized");
	if (!m) {
		free(clean);
		return;
	}
	dbus_message_append_args(m, DBUS_TYPE_STRING, &payload, DBUS_TYPE_INVALID);
	dbus_connection_send(bus_conn, m, NULL);
	dbus_connection_flush(bus_conn);
	dbus_message_unref(m);
	free(clean);
}

static void emit_error(const char *msg)
{
	DBusMessage *m;

	logmsg("error: %s", msg);
	if (!bus_conn)
		return;
	m = dbus_message_new_signal(BUS_PATH, BUS_IFACE, "Error");
	if (!m)
		return;
	dbus_message_append_args(m, DBUS_TYPE_STRING, &msg, DBUS_TYPE_INVALID);
	dbus_connection_send(bus_conn, m, NULL);
	dbus_connection_flush(bus_conn);
	dbus_message_unref(m);
}

/* ------------------------------------------------------------ recognizer  */

/* Builds the recognizer if it is not there yet and returns it. NULL means the
 * model could not be loaded (missing file, no memory, bad config). */
static const SherpaOnnxOfflineRecognizer *recognizer_get(void)
{
	const SherpaOnnxOfflineRecognizer *r;

	pthread_mutex_lock(&state_lock);
	if (!recognizer && sherpa_load() == 0) {
		SherpaOnnxOfflineRecognizerConfig cfg;

		memset(&cfg, 0, sizeof(cfg));
		cfg.feat_config.sample_rate = CAPTURE_RATE;
		cfg.feat_config.feature_dim = 80;
		cfg.model_config.sense_voice.model = opt.asr_model;
		cfg.model_config.sense_voice.language = opt.language;
		cfg.model_config.sense_voice.use_itn = 1;
		cfg.model_config.tokens = opt.asr_tokens;
		cfg.model_config.num_threads = opt.num_threads;
		cfg.model_config.provider = "cpu";
		cfg.model_config.debug = 0;
		cfg.decoding_method = "greedy_search";

		logmsg("loading ASR model %s (threads=%d, language=%s)",
		       opt.asr_model, opt.num_threads, opt.language);
		recognizer = api_create(&cfg);
		if (!recognizer)
			logmsg("SherpaOnnxCreateOfflineRecognizer failed");
	}
	r = recognizer;
	pthread_mutex_unlock(&state_lock);
	return r;
}

/* Frees the model. Caller must ensure no decode is running: disable() joins the
 * worker first. This is what makes "switch off" release the resources -- the
 * int8 weights plus the onnxruntime arenas live as long as the recognizer.
 *
 * free() alone is not enough: onnxruntime allocates in large chunks and glibc
 * keeps the freed arenas in the heap instead of returning them to the kernel,
 * so the RSS stayed at ~148 MB after a disable (measured on the board: 357 MB
 * enabled -> 148 MB "released"). malloc_trim(0) returns those free arenas, which
 * is what makes the switch mean what it says. */
static void recognizer_free(void)
{
	pthread_mutex_lock(&state_lock);
	if (recognizer) {
		api_destroy(recognizer);
		recognizer = NULL;
	}
	pthread_mutex_unlock(&state_lock);
	/* Destroy first, then unmap: the recognizer's destructor lives in the
	 * library we are about to dlclose. */
	sherpa_unload();
#ifdef M_TRIM_THRESHOLD
	/* The unmapped libraries take their arenas with them, but the small
	 * allocations the C API made in our own heap (config strings, the
	 * recognizer's stub struct) stay as free chunks. Trimming is cheap and
	 * keeps the "off" measurement repeatable instead of drifting by a few
	 * hundred KB per cycle. */
	malloc_trim(0);
#endif
}

/* --------------------------------------------------------------- capture  */

struct pcm {
	int16_t *samples;
	size_t count;
	size_t cap;
};

/* Grow the buffer so it can hold at least `want` samples. Only called from
 * the setup path, before the stream starts: the realtime callback writes
 * straight into the reserved space and never reaches this. */
static bool pcm_reserve(struct pcm *p, size_t want)
{
	int16_t *grown;
	size_t cap;

	if (want <= p->cap)
		return true;
	cap = p->cap ? p->cap : (size_t)CAPTURE_RATE * 2;
	while (cap < want)
		cap *= 2;
	grown = realloc(p->samples, cap * sizeof(int16_t));
	if (!grown)
		return false;
	p->samples = grown;
	p->cap = cap;
	return true;
}

static double pcm_rms(const int16_t *s, size_t n)
{
	double acc = 0.0;
	size_t i;

	if (n == 0)
		return 0.0;
	for (i = 0; i < n; i++) {
		double v = s[i] / 32768.0;
		acc += v * v;
	}
	return sqrt(acc / (double)n) * 32768.0;
}

/* --------------------------------------------------------- pw capture ---- */

/* Microphone input goes through PipeWire, not through the ALSA PCM plugin.
 *
 * The board runs two PipeWire instances: a system one (user "pipewire") that
 * grabs PAL/AGM first, and the per-session one. The ALSA "default" PCM is the
 * pipewire-alsa plugin, and that plugin binds to the *system* instance -- as
 * root it returned real audio, but a session service (any logged-in user,
 * protected by ProtectHome/ProtectSystem) got a stream of zeros, so dictation
 * transcribed silence. Drive the session PipeWire directly instead: the client
 * connects to $XDG_RUNTIME_DIR/pipewire-0, i.e. the instance that belongs to
 * the session of whoever is logged in. No user name appears anywhere, which is
 * also what makes this work for whatever account the customer creates.
 *
 * The stream asks for the node name (default: the PAL capture node) and lets
 * PipeWire resample/remix to mono 16 kHz; capture_rate records what was
 * negotiated so a mis-set graph is reported instead of silently decoded at the
 * wrong rate. */

#define PW_CAPTURE_NODE_DEFAULT "pal_source_speaker_mic"
/* 3 s of 16 kHz mono is 96 000 bytes; one second of buffer is plenty for a
 * callback that appends to a growing array, and it keeps the graph from
 * stalling the daemon while it is busy. */
#define PW_TARGET_SECONDS 1

struct pw_rec {
	struct pw_main_loop *loop;
	struct pw_stream *stream;
	struct pcm *pcm;
	unsigned rate;
	unsigned channels;
	int32_t acc;
	unsigned phase;
	bool failed;
	bool started;
	bool entered;
	struct spa_hook listener;
};

/* Called on the realtime data thread when PW_STREAM_FLAG_RT_PROCESS is set.
 *
 * The process callback must not realloc: the malloc arena is not realtime
 * safe and a slow realloc under the graph lock stalls every other client. So
 * it only ever appends into space reserved before the stream started
 * (max_samples is exactly the 60 s ceiling), and a full buffer just stops
 * pushing. */
static void consume(struct pw_rec *r, struct pw_buffer *b);

static void on_pw_process(void *userdata)
{
	struct pw_rec *r = userdata;
	struct pw_buffer *b;

	/* Drain every queued buffer, not just one: PipeWire only calls process
	 * once per wakeup, so handling a single buffer leaves the rest queued
	 * and the recorded audio falls behind real time (a 5 s hold captured
	 * 0.8 s). dequeue returns NULL when the queue is empty. */
	while ((b = pw_stream_dequeue_buffer(r->stream)) != NULL)
		consume(r, b);
}

static void consume(struct pw_rec *r, struct pw_buffer *b)
{
	struct spa_buffer *buf = b->buffer;
	struct spa_data *d;
	size_t n_samples;
	uint8_t *data;
	if (buf->n_datas < 1) {
		pw_stream_queue_buffer(r->stream, b);
		return;
	}
	d = &buf->datas[0];
	/* chunk->offset is only meaningful modulo maxsize; PipeWire's own
	 * examples apply the modulo before touching the data pointer. */
	if (!d->data || !d->chunk || d->chunk->size == 0 || d->maxsize == 0 ||
	    d->chunk->size > d->maxsize) {
		pw_stream_queue_buffer(r->stream, b);
		return;
	}
	data = (uint8_t *)d->data + (d->chunk->offset % d->maxsize);
	n_samples = d->chunk->size / sizeof(int16_t);
	if (!stop_capture && !r->failed) {
		const int16_t *in = (const int16_t *)data;
		size_t frames = n_samples / NODE_CHANNELS;
		size_t i;

		/* Downmix FL+FR to mono and decimate 48 kHz -> 16 kHz with a
		 * box filter (average the 3 samples per output sample), which is
		 * also the anti-alias filter for this ratio. r->phase carries the
		 * position inside the current group across callbacks, so buffer
		 * boundaries do not shift the decimation grid. */
		for (i = 0; i < frames; i++) {
			int32_t l = in[i * NODE_CHANNELS];
			int32_t c = (NODE_CHANNELS > 1)
				  ? in[i * NODE_CHANNELS + 1] : l;
			int32_t m = (l + c) / 2;

			r->acc += m;
			if (++r->phase < DECIMATE)
				continue;
			if (r->pcm->count < r->pcm->cap)
				r->pcm->samples[r->pcm->count++] =
					(int16_t)(r->acc / DECIMATE);
			r->acc = 0;
			r->phase = 0;
		}
	}
	pw_stream_queue_buffer(r->stream, b);
}

static void on_pw_state_changed(void *userdata, enum pw_stream_state old,
				enum pw_stream_state state, const char *error)
{
	struct pw_rec *r = userdata;

	/* Only the transitions that matter are logged. A full dump of every
	 * state is what identified the "stuck in connecting" case, but it is
	 * four lines per utterance in normal use; an error or a stream that
	 * never reaches STREAMING still prints the whole path below. */
	if (state == PW_STREAM_STATE_STREAMING) {
		r->started = true;
	} else if (state == PW_STREAM_STATE_ERROR) {
		logmsg("pipewire capture stream error (%s -> %s): %s",
		       pw_stream_state_as_string(old),
		       pw_stream_state_as_string(state),
		       error ? error : "unknown");
		r->failed = true;
		pw_main_loop_quit(r->loop);
	}
}

/* The graph is free to hand us a different format than the one we asked for
 * (AUDIO_FORMAT negotiation happens in audioconvert), and decoding 48 kHz
 * stereo bytes as 16 kHz mono produces confident nonsense rather than an
 * error. Refuse anything that is not what the model expects, loudly. */
static void on_pw_param_changed(void *userdata, uint32_t id, const struct spa_pod *param)
{
	struct pw_rec *r = userdata;
	struct spa_audio_info_raw info;

	if (id != SPA_PARAM_Format || !param)
		return;
	if (spa_format_audio_raw_parse(param, &info) < 0) {
		logmsg("pipewire: cannot parse the negotiated audio format");
		r->failed = true;
		pw_main_loop_quit(r->loop);
		return;
	}
	r->rate = info.rate;
	r->channels = info.channels;
	if (info.format != SPA_AUDIO_FORMAT_S16_LE ||
	    info.rate != NODE_RATE || info.channels != NODE_CHANNELS) {
		logmsg("pipewire: negotiated format=%u %u Hz %u ch, "
		       "need S16_LE(%u) %u Hz %u ch (downmix+decimate in the "
		       "callback would be wrong for any other format)",
		       (unsigned)info.format, info.rate, info.channels,
		       (unsigned)SPA_AUDIO_FORMAT_S16_LE, NODE_RATE,
		       NODE_CHANNELS);
		r->failed = true;
		pw_main_loop_quit(r->loop);
	}
}

static const struct pw_stream_events pw_stream_events = {
	PW_VERSION_STREAM_EVENTS,
	.state_changed = on_pw_state_changed,
	.param_changed = on_pw_param_changed,
	.process = on_pw_process,
};

/* One PipeWire client context for the whole daemon lifetime.
 *
 * pw_main_loop_new() builds the client context and loads its module set
 * (client-node, adapter, metadata, session-manager, rt...). Measured on this
 * board: 4.1 s the first time, ~30 ms afterwards. Creating it per utterance
 * put the capture 4 s behind the key press, so an ordinary press-and-release
 * had already set stop_capture and the take ended with 0 samples and "never
 * reached STREAMING". It is therefore created once, when the switch turns on
 * (pw_prewarm()), and only destroyed when the switch turns off.
 *
 * Guarded by state_lock; the pointer is only read in record(). */
static struct pw_main_loop *pw_loop;
static struct pw_context *pw_ctx;
static struct pw_core *pw_core;

/* pw_stream_new_simple() is not enough on its own: it calls pw_context_new()
 * internally, i.e. it builds a whole client context (and loads client-node,
 * adapter, metadata, session-manager, rt...) for every stream. That is the
 * 4 s. So the context is created once here and the per-utterance stream is
 * made with pw_stream_new() on top of it. */
static void pw_prewarm(void)
{
	struct pw_main_loop *l = NULL;
	struct pw_context *c = NULL;

	pthread_mutex_lock(&state_lock);
	if (!pw_loop) {
		l = pw_main_loop_new(NULL);
		if (l) {
			struct pw_properties *props = pw_properties_new(
				PW_KEY_APP_NAME, "pi-voice-inputd", NULL);

			c = pw_context_new(pw_main_loop_get_loop(l), props, 0);
			if (c) {
				struct pw_core *core = pw_context_connect(c, NULL, 0);

				if (core) {
					pw_core = core;
				} else {
					pw_context_destroy(c);
					c = NULL;
					pw_main_loop_destroy(l);
					l = NULL;
				}
			} else {
				pw_main_loop_destroy(l);
				l = NULL;
			}
		}
		pw_loop = l;
		pw_ctx = c;
	}
	pthread_mutex_unlock(&state_lock);
	if (l && c)
		logmsg("pipewire client context ready");
}

static void pw_release(void)
{
	struct pw_context *c;
	struct pw_main_loop *l;

	pthread_mutex_lock(&state_lock);
	c = pw_ctx;
	l = pw_loop;
	pw_ctx = NULL;
	pw_core = NULL;
	pw_loop = NULL;
	pthread_mutex_unlock(&state_lock);
	if (c)
		pw_context_destroy(c);
	if (l)
		pw_main_loop_destroy(l);
}

static int record(struct pcm *p)
{
	struct pw_rec r = { 0 };
	struct pw_properties *props;
	const struct spa_pod *params[1];
	struct spa_audio_info_raw info = {
		.format = SPA_AUDIO_FORMAT_S16_LE,
		.rate = NODE_RATE,
		.channels = NODE_CHANNELS,
	};
	/* The ASR side is always 16 kHz mono, whatever the node produces. */
	size_t max_samples = (size_t)CAPTURE_RATE * MAX_RECORD_SECONDS;
	uint8_t pod_buf[1024];
	struct spa_pod_builder b =
		SPA_POD_BUILDER_INIT(pod_buf, sizeof(pod_buf));
	int rc = -1;

	r.pcm = p;
	/* Reserve the whole 60 s up front. The process callback runs on the
	 * realtime thread and must not realloc, so the buffer has to be big
	 * enough before the stream starts. */
	if (!pcm_reserve(p, max_samples)) {
		logmsg("cannot allocate the %zu-sample capture buffer",
		       max_samples);
		return -1;
	}
	/* The loop is created once, when the switch is turned on (see
	 * pw_prewarm()), and reused here. pw_main_loop_new() loads the PipeWire
	 * client context and its module set, which measured 4.1 s on this
	 * board: creating it per utterance meant the capture started 4 s after
	 * the user pressed the key, and a normal press-and-release had already
	 * set stop_capture by then, so the take ended with 0 samples. */
	if (!pw_loop)
		pw_prewarm();
	r.loop = pw_loop;
	if (!r.loop) {
		logmsg("cannot create the PipeWire main loop");
		return -1;
	}

	props = pw_properties_new(
		PW_KEY_MEDIA_TYPE, "Audio",
		PW_KEY_MEDIA_CATEGORY, "Capture",
		/* media.role is deliberately NOT set: wireplumber's policy keys off
		 * it, and "Communication" made the stream sit in "connecting" on
		 * this image instead of being linked (pw-record, which sets no
		 * role, linked immediately). */
		NULL);
	if (!props) {
		logmsg("cannot allocate PipeWire properties");
		return -1;
	}
	/* Which capture node to record from. Empty means "whatever the session
	 * manager elected as the default source", so a board with a differently
	 * named PAL node still records.
	 *
	 * target.object takes effect in wireplumber's link step, and on this
	 * image (pipewire 1.6, Ubuntu 26.04) the stream sat in "connecting" and
	 * was never linked when the target was given as a *node name* while
	 * `pw-record --target <id>` worked. Passing it as an integer id in the
	 * same property is what pw-record does internally, so a numeric value is
	 * used as an id and anything else falls back to the name form. */
	if (opt.pw_node && *opt.pw_node) {
		char *end = NULL;
		unsigned long id = strtoul(opt.pw_node, &end, 10);

		if (end && *end == '\0' && id > 0 && id <= UINT32_MAX)
			pw_properties_setf(props, PW_KEY_TARGET_OBJECT, "%lu", id);
		else
			pw_properties_set(props, PW_KEY_TARGET_OBJECT, opt.pw_node);
	}
	if (!props) {
		logmsg("cannot allocate PipeWire properties");
		return -1;
	}
	pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%u/%u",
			   NODE_RATE / 20, NODE_RATE);
	/* PW_KEY_NODE_ALWAYS_PROCESS is deliberately NOT set to 0: that tells
	 * PipeWire the node must not be driven when nothing else in the graph
	 * needs it, and on this image a capture stream with it set stayed in
	 * "suspended" and was never linked (the node never reached STREAMING).
	 * pw-record does not set it either, and it links immediately. */

	if (!pw_core) {
		logmsg("no PipeWire connection (switch off?)");
		return -1;
	}
	r.stream = pw_stream_new(pw_core, "pi-voice-input-capture", props);
	if (!r.stream) {
		logmsg("cannot create the PipeWire capture stream");
		return -1;
	}

	/* pw_main_loop_run() would call this itself, but we drive the loop with
	 * pw_loop_iterate() so a release is honoured on a 100 ms tick. Entering
	 * has to happen before connect(): the stream's server-side handshake
	 * (core round-trips, node creation, the link wireplumber negotiates)
	 * only advances while this loop is iterated and *owned* by this thread.
	 * Entering afterwards leaves the stream in "connecting" until the stop
	 * flag trips, which is what "never reached STREAMING, 0 samples" was. */
	pw_loop_enter(pw_main_loop_get_loop(r.loop));
	r.entered = true;

	pw_stream_add_listener(r.stream, &r.listener, &pw_stream_events, &r);

	params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);
	/* No PW_STREAM_FLAG_RT_PROCESS: the process callback then runs on the
	 * main loop, which is the thread that already owns this buffer and the
	 * one pw_loop_iterate() below is driving. With RT_PROCESS the callback
	 * would run on PipeWire's own data thread, and this loop -- which owns
	 * the buffer and the stop flag -- would race with it for no benefit at
	 * dictation latency (a 50 ms quantum is 3 orders of magnitude below
	 * the 250 ms release threshold). */
	if (pw_stream_connect(r.stream, PW_DIRECTION_INPUT, PW_ID_ANY,
			      PW_STREAM_FLAG_AUTOCONNECT |
			      PW_STREAM_FLAG_MAP_BUFFERS,
			      params, 1) < 0) {
		logmsg("cannot connect the PipeWire capture stream");
		goto out;
	}

	logmsg("capturing from PipeWire node '%s' (%u Hz, %u ch -> %u Hz mono)",
	       opt.pw_node, NODE_RATE, NODE_CHANNELS, CAPTURE_RATE);

	while (!stop_capture && r.pcm->count < max_samples && !r.failed) {
		/* pw_loop_iterate() blocks for at most 100 ms, so a release is
		 * honoured within one tick even when the graph is quiet. Do NOT
		 * sleep on top of it: the tick already bounds the wait, and the
		 * extra sleep throttled the loop to ~10 buffers/s, so a 5 s hold
		 * captured under a second of audio. */
		int prc = pw_loop_iterate(pw_main_loop_get_loop(r.loop), 100);

		if (prc < 0) {
			logmsg("PipeWire loop error: %s", strerror(-prc));
			goto out;
		}
	}

	if (!r.started && !r.failed)
		logmsg("PipeWire capture never reached STREAMING "
		       "(node '%s'); is the session PipeWire running?",
		       opt.pw_node);
	else if (r.started && !r.failed)
		logmsg("captured %zu samples (%u Hz, %u ch)", p->count,
		       r.rate ? r.rate : NODE_RATE,
		       r.channels ? r.channels : NODE_CHANNELS);
	rc = r.failed ? -1 : 0;

out:
	/* Order matters: pw_stream_destroy() only *queues* the removal, it needs
	 * the loop to be iterated for the request to reach the server. Calling
	 * pw_loop_leave() first (as this used to) abandoned the queue, so the
	 * server kept the capture node alive in the "running" state and the
	 * microphone stayed occupied after the key was released -- measured on
	 * the board as a leftover pw-dump node named pi-voice-inputd with
	 * media.class=Stream/Input/Audio. Destroy, pump long enough for the
	 * round trip, then leave. */
	pw_stream_destroy(r.stream);
	if (r.entered) {
		struct pw_loop *l = pw_main_loop_get_loop(r.loop);
		int i;

		for (i = 0; i < 20; i++)
			pw_loop_iterate(l, 10);
		pw_loop_leave(l);
	}
	return rc;
}

/* --------------------------------------------------------------- recognize */

static char *transcribe(const struct pcm *p)
{
	const SherpaOnnxOfflineRecognizer *r;
	const SherpaOnnxOfflineStream *stream;
	const SherpaOnnxOfflineRecognizerResult *res;
	float *f;
	char *text = NULL;
	size_t i;

	r = recognizer_get();
	if (!r)
		return NULL;
	stream = api_stream_new(r);
	if (!stream)
		return NULL;

	f = malloc(p->count * sizeof(float));
	if (f) {
		for (i = 0; i < p->count; i++)
			f[i] = p->samples[i] / 32768.0f;
		api_accept(stream, CAPTURE_RATE, f, (int32_t)p->count);
		free(f);
		api_decode(r, stream);
		res = api_result(stream);
		if (res) {
			if (res->text && *res->text)
				text = strdup(res->text);
			api_result_free(res);
		}
	}
	api_stream_free(stream);
	return text;
}

static void *dictate(void *arg)
{
	struct pcm pcm = { 0 };
	struct timespec t0, now;
	int64_t ms;
	double rms;
	char *text;

	(void)arg;
	/* stop_capture is a one-shot latch shared with StopRecording and the
	 * signal handlers, and the previous utterance left it set. Reset it on
	 * this thread (the only writer from here on is StopRecording) before
	 * the capture loop looks at it -- otherwise every take after the first
	 * exits the loop on entry with "never reached STREAMING" and 0
	 * samples. */
	stop_capture = false;
	set_state("recording");
	emit_state_changed();
	clock_gettime(CLOCK_MONOTONIC, &t0);

	if (record(&pcm) < 0) {
		emit_error("麦克风打开失败");
		goto done;
	}
	clock_gettime(CLOCK_MONOTONIC, &now);
	ms = (now.tv_sec - t0.tv_sec) * 1000LL +
	     (now.tv_nsec - t0.tv_nsec) / 1000000LL;
	rms = pcm_rms(pcm.samples, pcm.count);
	logmsg("captured %zu samples (%.2f s), rms=%.0f",
	       pcm.count, pcm.count / (double)CAPTURE_RATE, rms);

	if (ms < MIN_UTTERANCE_MS) {
		logmsg("shorter than %d ms, ignored", MIN_UTTERANCE_MS);
	} else if (rms < SILENCE_RMS) {
		emit_error("没有听到声音");
	} else {
		set_state("recognizing");
		emit_state_changed();
		text = transcribe(&pcm);
		if (text) {
			emit_text(text);
			free(text);
		} else {
			emit_error("识别失败");
		}
	}

done:
	free(pcm.samples);
	pthread_mutex_lock(&state_lock);
	recording = false;
	worker_running = false;
	state_name = enabled ? "idle" : "off";
	pthread_mutex_unlock(&state_lock);
	emit_state_changed();
	return NULL;
}

/* ---------------------------------------------------------------- control  */

/* Returns false when dictation cannot start (disabled, or thread failed). */
static bool start_dictation(void)
{
	pthread_mutex_lock(&state_lock);
	if (!enabled) {
		pthread_mutex_unlock(&state_lock);
		return false;
	}
	if (recording) {
		/* A second press while recording is a no-op; the release ends the
		 * utterance. Key auto-repeat must not restart capture. */
		pthread_mutex_unlock(&state_lock);
		return true;
	}
	recording = true;
	stop_capture = false;
	pthread_mutex_unlock(&state_lock);

	/* Do NOT warm the model here. recognizer_get() holds state_lock for
	 * the 10-15 s the SenseVoice weights take to load, and this function
	 * runs on the D-Bus main loop: the whole service would stop answering
	 * (GetState, StopRecording, the switch) for that long, and a release
	 * arriving during the load would set stop_capture before the capture
	 * loop had started, so the take ended with 0 samples and "never
	 * reached STREAMING". The worker loads it instead: dictate() calls
	 * recognizer_get() right before decoding, and set_enabled(true) already
	 * preloads at switch-on, so in practice the load is done long before
	 * the first press. */
	if (pthread_create(&worker, NULL, dictate, NULL) != 0) {
		pthread_mutex_lock(&state_lock);
		recording = false;
		worker_running = false;
		state_name = enabled ? "idle" : "off";
		pthread_mutex_unlock(&state_lock);
		emit_error("无法启动录音线程");
		return false;
	}
	worker_running = true;
	return true;
}

static void stop_dictation(void)
{
	pthread_mutex_lock(&state_lock);
	if (!recording) {
		pthread_mutex_unlock(&state_lock);
		return;
	}
	stop_capture = true;
	pthread_mutex_unlock(&state_lock);

	if (worker_running)
		pthread_join(worker, NULL);
}

static void set_enabled(bool want)
{
	struct snapshot s = snap();

	if (want == s.enabled) {
		emit_state_changed();
		return;
	}
	if (!want) {
		/* Stop, then free: the worker may be mid-decode. */
		stop_dictation();
		recognizer_free();
		pthread_mutex_lock(&state_lock);
		enabled = false;
		state_name = "off";
		pthread_mutex_unlock(&state_lock);
		state_save(false);
		remembered = state_load();
		/* Drop the PipeWire client context too: "off" means every
		 * resource this service holds is gone. */
		pw_release();
		logmsg("disabled; model memory released");
	} else {
		pthread_mutex_lock(&state_lock);
		enabled = true;
		state_name = "idle";
		pthread_mutex_unlock(&state_lock);
		state_save(true);
		remembered = state_load();
		/* Build the PipeWire client context now rather than on the first
		 * press: it takes ~4 s, and doing it here is what makes the first
		 * Ctrl+Q as fast as every later one. */
		pw_prewarm();
		/* Load eagerly so the switch turning on means "ready" and the first
		 * Ctrl+Q costs the same as the rest. A failure is reported but does
		 * not flip the switch back: the next press retries. */
		if (!recognizer_get())
			emit_error("语音模型加载失败");
		else
			logmsg("enabled; model resident");
	}
	emit_state_changed();
}

/* ------------------------------------------------------------ dbus server  */

static char *dict_get_string(DBusMessage *msg, const char *want)
{
	DBusMessageIter it, dict, entry, val;
	const char *key;
	char *out = NULL;

	if (!dbus_message_iter_init(msg, &it))
		return NULL;
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
		return NULL;
	dbus_message_iter_recurse(&it, &dict);
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(&dict, &entry);
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&entry, &key);
			dbus_message_iter_next(&entry);
			if (strcmp(key, want) == 0 &&
			    dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
				const char *val_str = "";

				dbus_message_iter_recurse(&entry, &val);
				if (dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_STRING) {
					dbus_message_iter_get_basic(&val, &val_str);
					out = strdup(val_str);
				}
				break;
			}
		}
		dbus_message_iter_next(&dict);
	}
	return out;
}

/* Sends the reply for an incoming message. The incoming message is owned by
 * libdbus (it is freed after the filter returns), so it is deliberately NOT
 * unreffed here -- doing that is what produced
 * "arguments to dbus_message_unref() were incorrect". */
static void send_reply(DBusMessage *msg, DBusMessage *reply)
{
	if (reply) {
		dbus_connection_send(bus_conn, reply, NULL);
		dbus_connection_flush(bus_conn);
		dbus_message_unref(reply);
	}
	(void)msg;
}

static void reply_simple(DBusMessage *msg)
{
	send_reply(msg, dbus_message_new_method_return(msg));
}

static void append_dict_string(DBusMessageIter *arr, const char *key,
			       const char *value)
{
	DBusMessageIter entry, var;

	dbus_message_iter_open_container(arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &value);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(arr, &entry);
}

static void reply_all_properties(DBusMessage *msg)
{
	DBusMessage *reply = dbus_message_new_method_return(msg);
	DBusMessageIter it, arr;
	struct snapshot s = snap();
	const char *st = s.state;
	dbus_bool_t en = s.enabled ? TRUE : FALSE;
	dbus_uint32_t boot = (dbus_uint32_t)boot_count;
	dbus_bool_t rem = remembered ? TRUE : FALSE;
	/* Whether a readable keyboard is attached, i.e. whether "hold right Ctrl"
	 * is even possible. Decided here and not by the shell: only this process
	 * knows if the device was there *and* readable. A board driven from a
	 * touch panel has none, and the shell then shows an on-screen button.
	 *
	 * Deliberately a property of this daemon and not of pi-voice-hotkey: the
	 * Button gets one call and no second bus name to watch, and the answer
	 * stays right when the hotkey unit is the thing that broke. */
	dbus_bool_t kbd = input_keyboard_present() ? TRUE : FALSE;
	DBusMessageIter entry, var;

	dbus_message_iter_init_append(reply, &it);
	dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &arr);
	append_dict_string(&arr, "state", st);

	dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &(const char *){"enabled"});
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &en);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(&arr, &entry);

	dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &(const char *){"remembered"});
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &rem);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(&arr, &entry);

	dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &(const char *){"keyboard"});
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &kbd);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(&arr, &entry);

	dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &(const char *){"boots"});
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &boot);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(&arr, &entry);

	dbus_message_iter_close_container(&it, &arr);
	send_reply(msg, reply);
}

static void reply_get_property(DBusMessage *msg)
{
	const char *iface = NULL, *prop = NULL;
	DBusMessage *reply;
	DBusMessageIter it, var;
	struct snapshot s = snap();
	const char *st = s.state;
	dbus_bool_t en = s.enabled ? TRUE : FALSE;
	dbus_uint32_t boot = (dbus_uint32_t)boot_count;
	dbus_bool_t rem = remembered ? TRUE : FALSE;
	/* Same value GetAll publishes; Properties.Get has to agree with it or a
	 * caller that reads one property gets a different answer than one that
	 * reads all of them. */
	dbus_bool_t kbd = input_keyboard_present() ? TRUE : FALSE;

	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface,
				   DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID))
		return;
	reply = dbus_message_new_method_return(msg);
	if (!reply)
		return;
	dbus_message_iter_init_append(reply, &it);
	if (strcmp(prop, "enabled") == 0) {
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "b", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &en);
	} else if (strcmp(prop, "keyboard") == 0) {
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "b", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &kbd);
	} else if (strcmp(prop, "remembered") == 0) {
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "b", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &rem);
	} else if (strcmp(prop, "boots") == 0) {
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "u", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &boot);
	} else if (strcmp(prop, "state") == 0) {
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &st);
	} else {
		/* A property the interface does not have must be answered with
		 * an error, not with silence: libdbus does not reply on our
		 * behalf, so returning here leaves the caller blocked until its
		 * own timeout fires (busctl reports "Connection timed out").
		 * That is exactly what a 'State' vs 'state' typo produced. */
		DBusMessage *err;

		dbus_message_unref(reply);
		err = dbus_message_new_error_printf(
			msg, DBUS_ERROR_UNKNOWN_PROPERTY,
			"no property '%s' on %s", prop, BUS_IFACE);
		if (err) {
			dbus_connection_send(bus_conn, err, NULL);
			dbus_connection_flush(bus_conn);
			dbus_message_unref(err);
		}
		return;
	}
	dbus_message_iter_close_container(&it, &var);
	send_reply(msg, reply);
}

static void reply_set_property(DBusMessage *msg)
{
	const char *iface = NULL, *prop = NULL;
	DBusMessageIter it;
	dbus_bool_t want;

	if (!dbus_message_iter_init(msg, &it))
		goto bad;
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		goto bad;
	dbus_message_iter_get_basic(&it, &iface);
	dbus_message_iter_next(&it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		goto bad;
	dbus_message_iter_get_basic(&it, &prop);
	dbus_message_iter_next(&it);
	if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT)
		dbus_message_iter_recurse(&it, &it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_BOOLEAN)
		goto bad;
	dbus_message_iter_get_basic(&it, &want);
	if (strcmp(prop, "enabled") != 0)
		goto bad;
	set_enabled(want ? true : false);
	reply_simple(msg);
	return;
bad:
	/* Nothing to send: the caller passed something we do not accept. */
	return;
}

static void reply_introspect(DBusMessage *msg)
{
	static const char xml[] =
		"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object "
		"Introspection 1.0//EN\"\n"
		" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
		"<node>\n"
		"  <interface name=\"" BUS_IFACE "\">\n"
		"    <method name=\"SetEnabled\">\n"
		"      <arg type=\"b\" name=\"enabled\" direction=\"in\"/>\n"
		"    </method>\n"
		"    <method name=\"StartRecording\">\n"
		"      <arg type=\"a{sv}\" name=\"params\" direction=\"in\"/>\n"
		"      <arg type=\"b\" name=\"ok\" direction=\"out\"/>\n"
		"    </method>\n"
		"    <method name=\"StopRecording\"/>\n"
		"    <method name=\"GetState\">\n"
		"      <arg type=\"s\" name=\"state\" direction=\"out\"/>\n"
		"      <arg type=\"b\" name=\"enabled\" direction=\"out\"/>\n"
		"      <arg type=\"u\" name=\"boots\" direction=\"out\"/>\n"
		"    </method>\n"
		"    <signal name=\"StateChanged\">\n"
		"      <arg type=\"s\" name=\"state\"/>\n"
		"      <arg type=\"b\" name=\"enabled\"/>\n"
		"      <arg type=\"u\" name=\"boots\"/>\n"
		"    </signal>\n"
		"    <signal name=\"TextRecognized\">\n"
		"      <arg type=\"s\" name=\"text\"/>\n"
		"    </signal>\n"
		"    <signal name=\"Error\">\n"
		"      <arg type=\"s\" name=\"message\"/>\n"
		"    </signal>\n"
		"  </interface>\n"
		"  <interface name=\"org.freedesktop.DBus.Properties\">\n"
		"    <method name=\"Get\">\n"
		"      <arg type=\"s\" name=\"interface\" direction=\"in\"/>\n"
		"      <arg type=\"s\" name=\"property\" direction=\"in\"/>\n"
		"      <arg type=\"v\" name=\"value\" direction=\"out\"/>\n"
		"    </method>\n"
		"    <method name=\"GetAll\">\n"
		"      <arg type=\"s\" name=\"interface\" direction=\"in\"/>\n"
		"      <arg type=\"a{sv}\" name=\"properties\" direction=\"out\"/>\n"
		"    </method>\n"
		"    <property name=\"state\" type=\"s\" access=\"read\"/>\n"
		"    <property name=\"enabled\" type=\"b\" access=\"readwrite\"/>\n"
		"    <property name=\"remembered\" type=\"b\" access=\"read\"/>\n"
		"    <property name=\"keyboard\" type=\"b\" access=\"read\"/>\n"
		"    <property name=\"boots\" type=\"u\" access=\"read\"/>\n"
		"    <method name=\"Set\">\n"
		"      <arg type=\"s\" name=\"interface\" direction=\"in\"/>\n"
		"      <arg type=\"s\" name=\"property\" direction=\"in\"/>\n"
		"      <arg type=\"v\" name=\"value\" direction=\"in\"/>\n"
		"    </method>\n"
		"  </interface>\n"
		"  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
		"    <method name=\"Introspect\">\n"
		"      <arg type=\"s\" name=\"xml\" direction=\"out\"/>\n"
		"    </method>\n"
		"  </interface>\n"
		"</node>\n";
	DBusMessage *reply = dbus_message_new_method_return(msg);
	const char *xml_ptr = xml;

	dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml_ptr,
				 DBUS_TYPE_INVALID);
	send_reply(msg, reply);
}

static DBusHandlerResult bus_filter(DBusConnection *conn, DBusMessage *msg,
				    void *data)
{
	const char *member = dbus_message_get_member(msg);
	const char *iface = dbus_message_get_interface(msg);
	(void)conn;
	(void)data;

	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_METHOD_CALL || !member)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	/* The standard interfaces carry a bare member name ("Get", "GetAll",
	 * "Set", "Introspect"): dbus_message_get_member() never returns the
	 * dotted form, so matching on "org.freedesktop.DBus.Properties.Get"
	 * silently never fires and every property call falls through to
	 * NOT_YET_HANDLED -- which the caller sees as "no such property".
	 * Interface and member are therefore matched separately. */
	if (iface && strcmp(iface, "org.freedesktop.DBus.Properties") == 0) {
		if (strcmp(member, "Get") == 0)
			reply_get_property(msg);
		else if (strcmp(member, "GetAll") == 0)
			reply_all_properties(msg);
		else if (strcmp(member, "Set") == 0)
			reply_set_property(msg);
		else
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (iface && strcmp(iface, "org.freedesktop.DBus.Introspectable") == 0 &&
	    strcmp(member, "Introspect") == 0) {
		reply_introspect(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (strcmp(member, "SetEnabled") == 0) {
		dbus_bool_t want = FALSE;

		if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_BOOLEAN, &want,
					   DBUS_TYPE_INVALID))
			goto bail;
		set_enabled(want ? true : false);
		reply_simple(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "StartRecording") == 0) {
		char *purpose = dict_get_string(msg, "purpose");
		char *ctx = dict_get_string(msg, "context");
		DBusMessage *reply;
		dbus_bool_t ok;

		logmsg("StartRecording purpose='%s' context='%s'",
		       purpose ? purpose : "", ctx ? ctx : "");
		free(purpose);
		free(ctx);
		ok = start_dictation() ? TRUE : FALSE;
		reply = dbus_message_new_method_return(msg);
		dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &ok,
					 DBUS_TYPE_INVALID);
		send_reply(msg, reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "StopRecording") == 0) {
		stop_dictation();
		reply_simple(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "GetState") == 0) {
		DBusMessage *reply = dbus_message_new_method_return(msg);
		struct snapshot s = snap();
		dbus_bool_t en = s.enabled ? TRUE : FALSE;
		dbus_uint32_t boot = (dbus_uint32_t)boot_count;
		const char *st = s.state;

		dbus_message_append_args(reply, DBUS_TYPE_STRING, &st,
					 DBUS_TYPE_BOOLEAN, &en,
					 DBUS_TYPE_UINT32, &boot,
					 DBUS_TYPE_INVALID);
		send_reply(msg, reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Properties.Get") == 0) {
		reply_get_property(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Properties.GetAll") == 0) {
		reply_all_properties(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Properties.Set") == 0) {
		reply_set_property(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Introspectable.Introspect") == 0) {
		reply_introspect(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Properties.Get") == 0) {
		reply_get_property(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Properties.GetAll") == 0) {
		reply_all_properties(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Properties.Set") == 0) {
		reply_set_property(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (strcmp(member, "org.freedesktop.DBus.Introspectable.Introspect") == 0) {
		reply_introspect(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

bail:
	return DBUS_HANDLER_RESULT_HANDLED;
}

static bool bus_init(void)
{
	DBusError err;
	DBusObjectPathVTable vtable = { .message_function = bus_filter };
	int rc;

	dbus_error_init(&err);
	bus_conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!bus_conn) {
		logmsg("no session bus: %s",
		       dbus_error_is_set(&err) ? err.message : "unknown");
		dbus_error_free(&err);
		return false;
	}
	dbus_connection_set_exit_on_disconnect(bus_conn, FALSE);
	rc = dbus_bus_request_name(bus_conn, BUS_NAME,
				   DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
	if (dbus_error_is_set(&err)) {
		logmsg("cannot own %s: %s", BUS_NAME, err.message);
		dbus_error_free(&err);
		return false;
	}
	if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		logmsg("another instance already owns %s", BUS_NAME);
		/* Not an error: the service is already being provided. Report it
		 * separately so main() can exit 0 -- exiting non-zero makes
		 * Restart=on-failure respawn us every RestartSec (measured: 64
		 * restarts and a log line every 2 s) while the winner keeps the
		 * name, which is pure noise. */
		another_owner = true;
		return false;
	}
	if (!dbus_connection_register_object_path(bus_conn, BUS_PATH, &vtable,
						  NULL)) {
		logmsg("cannot register %s", BUS_PATH);
		return false;
	}
	return true;
}

/* ------------------------------------------------------------------- main  */

static void parse_args(int argc, char **argv)
{
	int i;

	memset(&opt, 0, sizeof(opt));
	/* PipeWire capture node. The default names the PAL handset mic, which is
	 * the board's built-in microphone; an empty string means "whatever
	 * PipeWire elected as the default source" and is also accepted. */
	opt.pw_node = strdup(getenv("PI_VOICE_PW_NODE")
			     ? getenv("PI_VOICE_PW_NODE") : PW_CAPTURE_NODE_DEFAULT);
	opt.language = strdup(getenv("PI_VOICE_ASR_LANGUAGE")
			      ? getenv("PI_VOICE_ASR_LANGUAGE") : "zh");
	opt.num_threads = getenv("PI_VOICE_ASR_THREADS")
			  ? atoi(getenv("PI_VOICE_ASR_THREADS")) : 2;
	if (getenv("PI_VOICE_STATE_FILE")) {
		/* Explicit path wins: this is what the board tests and the unit
		 * test use to keep a throwaway state file. */
		opt.state_file = strdup(getenv("PI_VOICE_STATE_FILE"));
	} else if (getenv("STATE_DIRECTORY")) {
		/* Set by systemd from the unit's StateDirectory=, which is the
		 * path that is writable despite ProtectHome=read-only. */
		if (asprintf(&opt.state_file, "%s/enabled",
			     getenv("STATE_DIRECTORY")) < 0)
			opt.state_file = NULL;
	} else if (getenv("XDG_STATE_HOME")) {
		if (asprintf(&opt.state_file, "%s/pi-voice-input/enabled",
			     getenv("XDG_STATE_HOME")) < 0)
			opt.state_file = NULL;
	} else if (getenv("HOME")) {
		if (asprintf(&opt.state_file,
			     "%s/.local/state/pi-voice-input/enabled",
			     getenv("HOME")) < 0)
			opt.state_file = NULL;
	}
	if (!opt.state_file)
		logmsg("no HOME/XDG_STATE_HOME: the switch position will not persist");
	else
		mkdir_parents(opt.state_file);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--node") && i + 1 < argc) {
			free(opt.pw_node);
			opt.pw_node = strdup(argv[++i]);
		} else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
			opt.num_threads = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--language") && i + 1 < argc) {
			free(opt.language);
			opt.language = strdup(argv[++i]);
		} else if (!strcmp(argv[i], "--help")) {
			printf("usage: %s [--node PIPEWIRE-NODE] [--threads N] "
			       "[--language zh|en|auto]\n", argv[0]);
			exit(0);
		}
	}
	if (opt.num_threads < 1)
		opt.num_threads = 1;
}

static void on_signal(int sig)
{
	(void)sig;
	quit = true;
	stop_capture = true;
}

int main(int argc, char **argv)
{
	const char *dir;
	struct stat st;

	/* pw_init() sets up the SPA module loader and the log handlers; without
	 * it every pw_* call fails at stream creation ("can't create
	 * context"). It takes the argv pointers, so it goes before
	 * parse_args() consumes them. */
	pw_init(&argc, &argv);
	parse_args(argc, argv);
	/* The model and tokens come from the npu-llm package; these are the same
	 * paths npu-chat uses, so there is one copy on the board, not two. */
	dir = getenv("PI_VOICE_MODEL_DIR") ? getenv("PI_VOICE_MODEL_DIR")
					  : "/usr/share/npu-llm/voice/asr";
	if (asprintf(&opt.asr_model, "%s/model.int8.onnx", dir) < 0)
		return 1;
	if (asprintf(&opt.asr_tokens, "%s/tokens.txt", dir) < 0)
		return 1;
	if (stat(opt.asr_model, &st) != 0) {
		logmsg("ASR model missing: %s (is npu-llm installed?)",
		       opt.asr_model);
		return 1;
	}
	if (stat(opt.asr_tokens, &st) != 0) {
		logmsg("ASR tokens missing: %s", opt.asr_tokens);
		return 1;
	}

	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGPIPE, SIG_IGN);

	if (!bus_init())
		return another_owner ? 0 : 1;

	logmsg("ready: pipewire-node=%s threads=%d model=%s",
	       opt.pw_node, opt.num_threads, opt.asr_model);

	/* Restore the switch position before announcing state, so the first
	 * StateChanged/GetState the shell sees is the real one and the indicator
	 * never flickers from "off" to "on". Loading here -- not on the first
	 * press -- is what makes a remembered "on" mean "ready at login". */
	if (state_load()) {
		logmsg("restoring remembered state: on");
		set_enabled(true);
	} else {
		logmsg("starting disabled (remembered state: off)");
	}
	emit_state_changed();

	while (!quit) {
		if (!dbus_connection_read_write_dispatch(bus_conn, 200))
			break;
		/* Flush here, not in the worker: dbus_connection_flush() blocks on
		 * the socket and measured 4.1 s when called from dictate() while
		 * this loop was dispatching on the same connection. The worker
		 * only queues (dbus_connection_send) and raises this flag. */
		if (queue_flush) {
			queue_flush = false;
			dbus_connection_flush(bus_conn);
		}
	}

	stop_capture = true;
	if (worker_running)
		pthread_join(worker, NULL);
	recognizer_free();
	dbus_connection_unregister_object_path(bus_conn, BUS_PATH);
	dbus_connection_close(bus_conn);
	dbus_connection_unref(bus_conn);
	logmsg("exiting");
	return 0;
}
