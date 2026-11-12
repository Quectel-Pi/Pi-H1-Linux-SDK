/* pi-voice-hotkey -- reads the keyboard directly so "hold to talk" really means
 * hold.
 *
 * Why this exists: a GNOME custom keybinding only fires on key *press*. A
 * gesture that ends on key *release* cannot be expressed with it, and on
 * GNOME/Wayland a normal client cannot grab a global chord either (the
 * compositor owns the keyboard). What a plain user *can* do is read
 * /dev/input/event*, which is group "input" -- and the desktop user is in that
 * group on these images. So this small process watches the keyboard for the
 * press and release of the push-to-talk key and tells the daemon exactly when
 * each happens.
 *
 * The primary binding is the RIGHT CTRL key held on its own: nothing in GNOME
 * or in any common application binds a bare modifier, so holding it to talk
 * cannot trigger anything else, and it needs no chord. Ctrl+Q and Ctrl+Alt+V
 * are still accepted as alternates because they are what earlier builds
 * documented, but Ctrl+Q is "quit" in most programs: this process only reads
 * the keyboard, so it cannot stop the chord from reaching the focused window.
 *
 * The daemon stays the only owner of the microphone and of the model; this
 * program forwards StartRecording/StopRecording over the session bus and exits
 * nothing. It runs for the life of the session.
 *
 * Failure is graceful by design: without permission to read the input devices
 * it logs one line and falls back to nothing, leaving the GNOME keybinding
 * (press-press) path working.
 */

#define _GNU_SOURCE

#include <dbus/dbus.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUS_NAME "org.quectel.PiVoiceInput"
#define BUS_PATH "/org/quectel/PiVoiceInput"
#define BUS_IFACE "org.quectel.PiVoiceInput"

#define MAX_DEVICES 32
/* A press shorter than this is a typo, not dictation; the daemon applies its
 * own minimum too, this only avoids send/receive chatter. */
#define MIN_HOLD_MS 150

struct device {
	int fd;
	struct libevdev *dev;
};

/* The bus calls go through a function pointer so the key state machine can be
 * tested on its own (tests/hotkey-unittest.c) without a session bus. In the
 * shipped program this is only ever hotkey_bus_call(). */
static DBusMessage *(*bus_call)(const char *member) = NULL;

static struct device devices[MAX_DEVICES];
static int device_count;
static DBusConnection *bus_conn;
static volatile bool quit;
static bool holding;
static struct timespec hold_started;
static bool enabled;
/* Set when the hold was started by the dedicated key, so its release ends it
 * even if the modifier state changed in between (e.g. the user let go of
 * Ctrl before the other key). */
static bool hold_by_ptt;

static void logmsg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "pi-voice-hotkey: ");
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* --------------------------------------------------------------- dbus     */

static DBusMessage *hotkey_bus_call(const char *member)
{
	DBusMessage *msg, *reply;
	DBusError err;

	msg = dbus_message_new_method_call(BUS_NAME, BUS_PATH, BUS_IFACE, member);
	if (!msg)
		return NULL;
	if (!strcmp(member, "StartRecording")) {
		DBusMessageIter it, dict;

		dbus_message_iter_init_append(msg, &it);
		dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
		dbus_message_iter_close_container(&it, &dict);
	}
	dbus_error_init(&err);
	reply = dbus_connection_send_with_reply_and_block(bus_conn, msg, 5000, &err);
	dbus_message_unref(msg);
	if (!reply) {
		logmsg("%s failed: %s", member,
		       err.message ? err.message : "no reply");
		dbus_error_free(&err);
	}
	return reply;
}

static void refresh_enabled(void)
{
	DBusMessage *reply = bus_call("GetState");
	DBusMessageIter it;
	const char *state = "";
	dbus_bool_t en = FALSE;

	if (!reply)
		return;
	if (dbus_message_iter_init(reply, &it)) {
		dbus_message_iter_get_basic(&it, &state);
		dbus_message_iter_next(&it);
		dbus_message_iter_get_basic(&it, &en);
	}
	enabled = en ? true : false;
	dbus_message_unref(reply);
}

static DBusHandlerResult bus_filter(DBusConnection *conn, DBusMessage *msg,
				    void *data)
{
	(void)conn;
	(void)data;
	if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_SIGNAL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if (dbus_message_is_signal(msg, BUS_IFACE, "StateChanged")) {
		DBusMessageIter it;
		const char *state = "";
		dbus_bool_t en = FALSE;

		if (dbus_message_iter_init(msg, &it)) {
			dbus_message_iter_get_basic(&it, &state);
			dbus_message_iter_next(&it);
			dbus_message_iter_get_basic(&it, &en);
			enabled = en ? true : false;
			if (!enabled && holding) {
				/* Switched off mid-utterance: drop the hold so the
				 * release does not send a StopRecording the daemon
				 * no longer cares about. */
				holding = false;
			}
		}
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static bool bus_init(void)
{
	DBusError err;

	dbus_error_init(&err);
	bus_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!bus_conn) {
		logmsg("no session bus: %s",
		       err.message ? err.message : "unknown");
		dbus_error_free(&err);
		return false;
	}
	dbus_connection_set_exit_on_disconnect(bus_conn, FALSE);
	dbus_connection_add_filter(bus_conn, bus_filter, NULL, NULL);
	dbus_bus_add_match(bus_conn,
			   "type='signal',interface='" BUS_IFACE "',"
			   "member='StateChanged'", &err);
	if (dbus_error_is_set(&err)) {
		logmsg("add_match: %s", err.message);
		dbus_error_free(&err);
	}
	return true;
}

/* --------------------------------------------------------------- devices  */

static bool is_keyboard(struct libevdev *dev)
{
	return libevdev_has_event_type(dev, EV_KEY) &&
	       libevdev_has_event_code(dev, EV_KEY, KEY_Q) &&
	       libevdev_has_event_code(dev, EV_KEY, KEY_LEFTCTRL);
}

static void add_input_file(const char *path)
{
	int fd;
	struct libevdev *dev = NULL;

	if (device_count >= MAX_DEVICES)
		return;
	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return;
	if (libevdev_new_from_fd(fd, &dev) < 0) {
		close(fd);
		return;
	}
	if (!is_keyboard(dev)) {
		libevdev_free(dev);
		close(fd);
		return;
	}
	devices[device_count].fd = fd;
	devices[device_count].dev = dev;
	device_count++;
	logmsg("watching %s (%s)", path, libevdev_get_name(dev));
}

static void scan_devices(void)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	char path[320];
	int before = device_count;

	if (!d) {
		logmsg("cannot list /dev/input: %s", strerror(errno));
		return;
	}
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		add_input_file(path);
	}
	closedir(d);
	if (device_count == before && device_count == 0)
		logmsg("no readable keyboard devices; hotkey disabled");
}

/* --------------------------------------------------------------- hotkey   */

static void hold_begin(void)
{
	DBusMessage *reply;

	if (holding)
		return;
	holding = true;
	clock_gettime(CLOCK_MONOTONIC, &hold_started);
	if (!enabled)
		refresh_enabled();
	if (!enabled) {
		holding = false;
		return;
	}
	reply = bus_call("StartRecording");
	if (!reply) {
		holding = false;
		return;
	}
	dbus_message_unref(reply);
	logmsg("recording");
}

static void hold_end(void)
{
	DBusMessage *reply;
	struct timespec now;
	long ms;

	if (!holding)
		return;
	holding = false;
	clock_gettime(CLOCK_MONOTONIC, &now);
	ms = (now.tv_sec - hold_started.tv_sec) * 1000L +
	     (now.tv_nsec - hold_started.tv_nsec) / 1000000L;
	if (ms < MIN_HOLD_MS) {
		logmsg("hold was %ld ms, ignored", ms);
		/* Still tell the daemon: it started capturing on the press. */
	}
	reply = bus_call("StopRecording");
	if (reply)
		dbus_message_unref(reply);
	logmsg("released after %ld ms", ms);
}

static void handle_key(const struct input_event *ev, bool *ctrl, bool *alt,
		       bool *shift)
{
	if (ev->type != EV_KEY) {
		/* Key repeat delivers EV_KEY value 2, which the branches below
		 * ignore, but a SYN after it would otherwise re-trigger. */
		return;
	}
	switch (ev->code) {
	case KEY_LEFTCTRL:
		*ctrl = ev->value != 0;
		break;
	case KEY_RIGHTCTRL:
		/* The push-to-talk key: a single key, held. Right Ctrl alone is
		 * bound to nothing in GNOME or in any common application, which
		 * is exactly why it is the primary binding -- Ctrl+Q is "quit"
		 * in most apps, and since this program only *reads* the
		 * keyboard it cannot stop the chord from reaching them, so
		 * holding it could quit the window the user was dictating
		 * into. */
		*ctrl = ev->value != 0;
		if (ev->value == 1) {
			hold_by_ptt = true;
			hold_begin();
		} else if (ev->value == 0) {
			if (hold_by_ptt) {
				hold_by_ptt = false;
				hold_end();
			}
		}
		break;
	case KEY_LEFTALT:
	case KEY_RIGHTALT:
		*alt = ev->value != 0;
		break;
	case KEY_LEFTSHIFT:
	case KEY_RIGHTSHIFT:
		*shift = ev->value != 0;
		break;
	case KEY_Q:
		if (!*ctrl || *alt || *shift || hold_by_ptt)
			break;
		if (ev->value == 1)
			hold_begin();
		else if (ev->value == 0)
			hold_end();
		break;
	case KEY_V:
		/* Ctrl+Alt+V, the alternate chord. */
		if (*ctrl && *alt && !*shift && !hold_by_ptt) {
			if (ev->value == 1)
				hold_begin();
			else if (ev->value == 0)
				hold_end();
		}
		break;
	default:
		break;
	}
}

int main(void)
{
	struct pollfd pfd[MAX_DEVICES];
	bool ctrl = false, alt = false, shift = false;
	struct sigaction sa = { 0 };

	(void)sa;
	signal(SIGPIPE, SIG_IGN);

	if (!bus_init())
		return 1;
	bus_call = hotkey_bus_call;
	scan_devices();
	if (device_count == 0)
		return 0; /* the press-press keybinding path still works */

	while (!quit) {
		int i, n;

		for (i = 0; i < device_count; i++) {
			pfd[i].fd = devices[i].fd;
			pfd[i].events = POLLIN;
			pfd[i].revents = 0;
		}
		n = poll(pfd, (nfds_t)device_count, 500);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			logmsg("poll: %s", strerror(errno));
			break;
		}
		/* Keep the D-Bus connection serviced so StateChanged arrives even
		 * while no key is pressed. */
		dbus_connection_read_write(bus_conn, 0);
		while (dbus_connection_dispatch(bus_conn) ==
		       DBUS_DISPATCH_DATA_REMAINS)
			;

		for (i = 0; i < device_count; i++) {
			struct input_event ev;

			if (!(pfd[i].revents & POLLIN))
				continue;
			while (libevdev_next_event(devices[i].dev,
						   LIBEVDEV_READ_FLAG_NORMAL,
						   &ev) == LIBEVDEV_READ_STATUS_SUCCESS)
				handle_key(&ev, &ctrl, &alt, &shift);
		}
	}

	for (int i = 0; i < device_count; i++) {
		libevdev_free(devices[i].dev);
		close(devices[i].fd);
	}
	dbus_connection_unref(bus_conn);
	return 0;
}
