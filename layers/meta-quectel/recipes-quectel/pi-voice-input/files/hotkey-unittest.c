/* Unit test for the Ctrl+Q state machine in pi-voice-hotkey.
 *
 * It includes the program's source and replaces the two things that need a
 * desktop: the bus transport (bus_call) and the enable check. Everything else --
 * key decoding, repeat suppression, hold/release pairing -- runs for real, which
 * is the part that is easy to get wrong and impossible to eyeball on a board.
 *
 * Build and run on the target (or on a host with libevdev + dbus headers):
 *   cc -I. -I... hotkey-unittest.c -o hotkey-unittest -ldbus-1 -levdev
 *   ./hotkey-unittest        # prints PASS lines, exit 0
 */

#include <stdio.h>
#include <string.h>

/* The daemon-facing transport is replaced before the source is included, so the
 * program's own copy binds to nothing and the pointer is ours to set. main() is
 * renamed away: only the helpers and the key state machine are under test. */
#define main pi_voice_hotkey_main
#include "pi-voice-hotkey.c"
#undef main

static int started;
static int stopped;

static DBusMessage *fake_bus_call(const char *member)
{
	DBusMessage *reply;

	if (!strcmp(member, "StartRecording"))
		started++;
	else if (!strcmp(member, "StopRecording"))
		stopped++;
	/* A non-NULL reply means "the daemon accepted it", which is the path the
	 * success assertions need. It is never sent anywhere. */
	reply = dbus_message_new_signal("/test", "test.Signal", "Test");
	return reply;
}

static void key(bool *ctrl, bool *alt, bool *shift, int code, int value)
{
	struct input_event e;

	memset(&e, 0, sizeof(e));
	e.type = EV_KEY;
	e.code = (unsigned short)code;
	e.value = value;
	handle_key(&e, ctrl, alt, shift);
}

static int failures;

static void check(const char *what, int cond)
{
	printf("%s %s\n", cond ? "PASS" : "FAIL", what);
	if (!cond)
		failures++;
}

int main(void)
{
	bool ctrl = false, alt = false, shift = false;

	bus_call = fake_bus_call;
	enabled = true;

	/* Right Ctrl held on its own is the primary push-to-talk binding: the
	 * whole point is that nothing else on the desktop wants a bare
	 * modifier, so holding it to talk cannot trigger another action. */
	key(&ctrl, &alt, &shift, KEY_RIGHTCTRL, 1);
	check("right ctrl press starts recording", started == 1 && stopped == 0);
	/* Key auto-repeat delivers value 2 and must not restart the capture. */
	key(&ctrl, &alt, &shift, KEY_RIGHTCTRL, 2);
	check("right ctrl repeat does not restart", started == 1);
	key(&ctrl, &alt, &shift, KEY_RIGHTCTRL, 0);
	check("right ctrl release stops recording", stopped == 1);

	/* Ctrl+Q: press starts, release stops. Counters are per-case. */
	started = stopped = 0;
	key(&ctrl, &alt, &shift, KEY_LEFTCTRL, 1);
	key(&ctrl, &alt, &shift, KEY_Q, 1);
	check("ctrl+q press starts recording", started == 1 && stopped == 0);
	/* Key repeat must not restart capture: the daemon would see a second
	 * StartRecording while it is already recording. */
	key(&ctrl, &alt, &shift, KEY_Q, 2);
	key(&ctrl, &alt, &shift, KEY_Q, 2);
	check("key repeat does not restart recording", started == 1);
	key(&ctrl, &alt, &shift, KEY_Q, 0);
	check("ctrl+q release stops recording", stopped == 1);

	/* A bare q is ordinary typing and must never reach the daemon. */
	started = stopped = 0;
	key(&ctrl, &alt, &shift, KEY_LEFTCTRL, 0);
	key(&ctrl, &alt, &shift, KEY_Q, 1);
	key(&ctrl, &alt, &shift, KEY_Q, 0);
	check("plain q is ignored", started == 0 && stopped == 0);

	/* Ctrl+Shift+Q belongs to something else (reverse search, close tab). */
	started = stopped = 0;
	key(&ctrl, &alt, &shift, KEY_LEFTCTRL, 1);
	key(&ctrl, &alt, &shift, KEY_LEFTSHIFT, 1);
	key(&ctrl, &alt, &shift, KEY_Q, 1);
	key(&ctrl, &alt, &shift, KEY_Q, 0);
	check("ctrl+shift+q is ignored", started == 0 && stopped == 0);

	/* Ctrl+Alt+V is the alternate chord. */
	started = stopped = 0;
	key(&ctrl, &alt, &shift, KEY_LEFTSHIFT, 0);
	key(&ctrl, &alt, &shift, KEY_LEFTALT, 1);
	key(&ctrl, &alt, &shift, KEY_V, 1);
	check("ctrl+alt+v starts recording", started == 1);
	key(&ctrl, &alt, &shift, KEY_V, 0);
	check("ctrl+alt+v stops recording", stopped == 1);

	/* Switching the feature off mid-utterance must drop the hold, so the
	 * release does not send a StopRecording for a capture nobody wants. */
	started = stopped = 0;
	key(&ctrl, &alt, &shift, KEY_LEFTALT, 0);
	key(&ctrl, &alt, &shift, KEY_Q, 1);
	check("recording starts before disable", started == 1);
	holding = false; /* what the StateChanged handler does */
	enabled = false;
	key(&ctrl, &alt, &shift, KEY_Q, 0);
	check("release after disable sends nothing", stopped == 0);

	/* And with the feature off, a press is not forwarded at all. */
	started = stopped = 0;
	key(&ctrl, &alt, &shift, KEY_LEFTCTRL, 1);
	key(&ctrl, &alt, &shift, KEY_Q, 1);
	key(&ctrl, &alt, &shift, KEY_Q, 0);
	check("press is ignored while disabled", started == 0);

	printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL TESTS PASSED",
	       failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
