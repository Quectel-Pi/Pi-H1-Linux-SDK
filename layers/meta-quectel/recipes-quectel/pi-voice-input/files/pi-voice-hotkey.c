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
 *
 * Devices are re-scanned, not just opened once. A USB keyboard that is
 * unplugged and plugged back in -- or a hub that browns out -- comes back as a
 * *different* /dev/input/eventN: the old nodes keep their fds valid but marked
 * (deleted), so reads never return again and push-to-talk silently stops
 * working until the unit is restarted. Seen on the board (14:00 disconnect,
 * 14:09 reconnect, hotkey still holding event5/7/8 three hours later). Two
 * independent net layers catch that: inotify on /dev/input for the fast path,
 * and a periodic health check (nlink==0 on every open fd) so a missed event
 * still recovers. A hold that is in flight when its device disappears is ended
 * by itself, otherwise the daemon would sit in "recording" until the 60 s cap.
 */

#define _GNU_SOURCE

#include <dbus/dbus.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
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
/* A hold with no D-Bus traffic back from the daemon (reply to StopRecording)
 * might be stuck on a dead bus. Keep it comfortably above the daemon's 5000 ms
 * reply timeout so a healthy but slow daemon is never mistaken for a dead one. */
#define STALE_HOLD_MS 10000
/* Safety net under a hold: the daemon ends its own capture at 60 s, but if it
 * dies (or is restarted) mid-utterance nothing tells this program the hold is
 * over, and the next press would be swallowed by the "already holding" check.
 * A genuine press-to-talk utterance is a sentence, not a minute. */
#define MAX_HOLD_MS 60000
/* How often the open fds are checked for the device having gone away, and --
 * as the fallback if inotify never fires -- /dev/input is rescanned. */
#define HEALTH_INTERVAL_MS 1000
#define RESCAN_INTERVAL_MS 5000
#define INOTIFY_RETRY_MS 5000

#define INPUT_DIR "/dev/input"

struct device {
	int fd;
	struct libevdev *dev;
	char path[PATH_MAX];
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

static int64_t now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int64_t hold_started_ms(void)
{
	return (int64_t)hold_started.tv_sec * 1000 + hold_started.tv_nsec / 1000000;
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

/* The fd goes dead in two different ways when the device disappears, and both
 * are checked because neither is guaranteed on its own:
 *  - nlink drops to 0 as soon as udev unlinks /dev/input/eventN (a plain
 *    unplug), which is the cheap check;
 *  - a device that is replaced in place, or an fd held on another mount
 *    namespace, keeps nlink, but EVIOCGNAME starts failing on it.
 * Filesystems without meaningful link counts report 1, so nlink==0 is only ever
 * treated as "gone", never as "alive". */
static bool fd_alive(struct device *d)
{
	struct stat st;
	char name[256];

	if (fstat(d->fd, &st) < 0)
		return false;
	if (st.st_nlink == 0)
		return false;
	memset(name, 0, sizeof(name));
	if (ioctl(d->fd, EVIOCGNAME(sizeof(name) - 1), name) < 0)
		return false;
	return true;
}

static void drop_device(int i)
{
	logmsg("dropping %s (%s)", devices[i].path,
	       libevdev_get_name(devices[i].dev));
	libevdev_free(devices[i].dev);
	close(devices[i].fd);
	devices[i] = devices[device_count - 1];
	device_count--;
}

static bool already_watching(const char *path)
{
	int i;

	for (i = 0; i < device_count; i++)
		if (!strcmp(devices[i].path, path))
			return true;
	return false;
}

/* The same physical device can appear as more than one event node -- a HID
 * keyboard with two interfaces, or an eventN that was re-added next to a node
 * that is still open. Watching both would double every press. One open fd per
 * (major,minor) is enough, so the device number of the candidate is compared
 * against the ones already open. */
static bool device_number_open(dev_t rdev)
{
	int i;
	struct stat st;

	for (i = 0; i < device_count; i++) {
		if (fstat(devices[i].fd, &st) < 0)
			continue;
		if (st.st_rdev == rdev)
			return true;
	}
	return false;
}

static void add_input_file(const char *path)
{
	int fd;
	struct stat st;
	struct libevdev *dev = NULL;

	if (device_count >= MAX_DEVICES)
		return;
	/* A rescan runs while the old fd is still open on the same node when a
	 * device was only readded; watching it twice would double every press. */
	if (already_watching(path))
		return;
	if (stat(path, &st) < 0)
		return;
	if (device_number_open(st.st_rdev))
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
	/* A keyboard that is already printing events from before we opened it has
	 * a stale queue; without this the first poll after a reconnect can replay
	 * a press that happened while the device was gone. */
	libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_SYNC,
			    &(struct input_event){ 0 });
	devices[device_count].fd = fd;
	devices[device_count].dev = dev;
	snprintf(devices[device_count].path, sizeof(devices[device_count].path),
		 "%s", path);
	device_count++;
	logmsg("watching %s (%s)", path, libevdev_get_name(dev));
}

static int scan_devices(void)
{
	DIR *d = opendir(INPUT_DIR);
	struct dirent *e;
	char path[PATH_MAX];

	if (!d) {
		logmsg("cannot list %s: %s", INPUT_DIR, strerror(errno));
		return -1;
	}
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		snprintf(path, sizeof(path), INPUT_DIR "/%s", e->d_name);
		add_input_file(path);
	}
	closedir(d);
	/* Only on the transition. The loop rescans every 5 s while no keyboard is
	 * attached (a touch-panel board can sit like that for hours) and one line
	 * per scan would be 17k journal lines a day for a condition that has not
	 * changed. */
	{
		static bool warned;

		if (device_count == 0 && !warned) {
			logmsg("no readable keyboard devices; waiting for one");
			warned = true;
		} else if (device_count > 0) {
			warned = false;
		}
	}
	return device_count;
}

/* Unplugging the keyboard mid-sentence leaves the hold open: the release event
 * will never arrive. End it here so the daemon transcribes what it already has
 * instead of recording silence until its 60 s cap and the next push being
 * swallowed by "already holding". Defined below, next to hold_end(). */
static void hold_abandon(const char *why);

/* Returns the number of devices dropped. */
static int health_check(void)
{
	int i, dropped = 0;

	for (i = device_count - 1; i >= 0; i--) {
		if (fd_alive(&devices[i]))
			continue;
		drop_device(i);
		dropped++;
	}
	if (dropped) {
		/* The release for any hold in flight lived on a dropped device. */
		if (holding)
			hold_abandon("device disappeared");
		scan_devices();
	}
	return dropped;
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

/* Like hold_end(), but the release event is never coming: the device that would
 * have delivered it is gone. Still send StopRecording -- the daemon is capturing
 * and would otherwise run to its 60 s cap. */
static void hold_abandon(const char *why)
{
	DBusMessage *reply;

	if (!holding)
		return;
	holding = false;
	hold_by_ptt = false;
	logmsg("hold abandoned (%s), stopping the capture", why);
	reply = bus_call("StopRecording");
	if (reply)
		dbus_message_unref(reply);
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

/* One read of the inotify queue. The events themselves are not interpreted:
 * any add/remove/move under /dev/input means the set of event nodes may have
 * changed, so the cheap and always-correct reaction is a rescan. */
static void drain_inotify(int ifd)
{
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

	while (read(ifd, buf, sizeof(buf)) > 0)
		;
}

/* inotify is the fast path off a plug event; it is not available when the
 * caller runs out of watch descriptors, and /dev/input is not guaranteed to be
 * watchable at all in a container. Failure is not fatal -- the periodic health
 * check below still recovers. */
static int setup_inotify(void)
{
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

	if (fd < 0)
		return -1;
	if (inotify_add_watch(fd, INPUT_DIR,
			      IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM |
				      IN_ATTRIB | IN_ONLYDIR) < 0) {
		logmsg("inotify on %s failed: %s (falling back to polling)",
		       INPUT_DIR, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

int main(void)
{
	/* +1 slot: index 0 is the inotify fd when it exists. */
	struct pollfd pfd[MAX_DEVICES + 1];
	bool ctrl = false, alt = false, shift = false;
	int ifd = setup_inotify();
	int base = ifd >= 0 ? 1 : 0;
	int64_t last_health, last_scan, last_inotify_try;

	signal(SIGPIPE, SIG_IGN);

	if (!bus_init())
		return 1;
	bus_call = hotkey_bus_call;
	scan_devices();
	/* Deliberately *not* "exit if no keyboard yet": on this board the USB
	 * keyboard is enumerated a second after the session starts (11:31:01 USB
	 * probe, 11:31:37 watch), and on a cold boot with a slow hub it can be
	 * later still. Exiting here left the unit dead for the whole session
	 * because Restart=on-failure does not retrigger a clean exit. Idle and
	 * keep looking instead. */
	last_health = last_scan = last_inotify_try = now_ms();
	if (device_count == 0)
		logmsg("no keyboard yet; waiting for one to appear");

	while (!quit) {
		int i, n;
		int64_t now;

		pfd[0].fd = ifd;
		pfd[0].events = POLLIN;
		pfd[0].revents = 0;
		for (i = 0; i < device_count; i++) {
			pfd[base + i].fd = devices[i].fd;
			pfd[base + i].events = POLLIN;
			pfd[base + i].revents = 0;
		}
		/* A short timeout keeps the D-Bus connection serviced even on a
		 * session with no keyboard attached, where nothing would otherwise
		 * wake the loop. */
		n = poll(pfd, (nfds_t)(base + device_count), 500);
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

		if (ifd >= 0 && (pfd[0].revents & POLLIN)) {
			drain_inotify(ifd);
			health_check();
			scan_devices();
			last_health = last_scan = now_ms();
		}

		/* A device node that was replaced in place (same name, new fd) or
		 * an inotify event we did not get: the periodic check catches both,
		 * so a missed inotify wakeup cannot leave the hotkey dead. */
		now = now_ms();
		if (now - last_health >= HEALTH_INTERVAL_MS) {
			health_check();
			last_health = now;
		}
		if (now - last_scan >= RESCAN_INTERVAL_MS) {
			if (scan_devices() > 0)
				last_inotify_try = now;
			last_scan = now;
		}
		if (ifd < 0 && now - last_inotify_try >= INOTIFY_RETRY_MS) {
			/* Retry: a watch setup can fail during early boot when the
			 * devtmpfs mount is not settled yet. */
			ifd = setup_inotify();
			base = ifd >= 0 ? 1 : 0;
			last_inotify_try = now;
		}

		/* A hold whose release never arrives (keyboard unplugged, dead
		 * session bus, daemon that went away) must not latch forever. */
		if (holding) {
			long held = (long)(now - hold_started_ms());

			if (held > MAX_HOLD_MS)
				hold_abandon("held longer than the 60 s cap");
			else if (held > STALE_HOLD_MS && !device_count)
				hold_abandon("no keyboard left to release it");
		}

		for (i = 0; i < device_count; i++) {
			struct input_event ev;

			if (!(pfd[base + i].revents & POLLIN))
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
	if (ifd >= 0)
		close(ifd);
	dbus_connection_unref(bus_conn);
	return 0;
}
