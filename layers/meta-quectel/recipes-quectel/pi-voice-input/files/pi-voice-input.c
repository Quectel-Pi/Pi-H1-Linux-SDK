/* pi-voice-input -- command line front end for pi-voice-inputd.
 *
 * The GNOME custom keybinding runs "pi-voice-input client": Ctrl+Q pressed
 * starts the press, and the same binding firing again (while pressed) ends it.
 * That is how a "hold to talk" gesture is expressed through a plain global
 * shortcut: the shell gives us a keypress, not a keyup, so the second press of
 * the same chord is the release.
 *
 *   pi-voice-input client     press / release, whichever is due
 *   pi-voice-input on|off     flip the status-area switch from a script
 *   pi-voice-input toggle     same, inverted
 *   pi-voice-input state      print "state enabled boots remembered"
 *
 * "remembered" is what the daemon will restore at next login, which is useful
 * to check that the switch really persisted and not only changed in memory.
 */

#define _GNU_SOURCE

#include <dbus/dbus.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUS_NAME "org.quectel.PiVoiceInput"
#define BUS_PATH "/org/quectel/PiVoiceInput"
#define BUS_IFACE "org.quectel.PiVoiceInput"

static DBusConnection *conn;

static int connect_bus(void)
{
	DBusError err;
	int rc;

	dbus_error_init(&err);
	conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!conn) {
		fprintf(stderr, "pi-voice-input: no session bus: %s\n",
			err.message ? err.message : "unknown");
		dbus_error_free(&err);
		return 1;
	}
	dbus_connection_set_exit_on_disconnect(conn, FALSE);
	/* Fail fast and quietly when the daemon is not running: the last thing a
	 * hotkey should do is print a stack of D-Bus noise into the journal. */
	rc = dbus_bus_name_has_owner(conn, BUS_NAME, &err);
	if (dbus_error_is_set(&err)) {
		fprintf(stderr, "pi-voice-input: %s\n", err.message);
		dbus_error_free(&err);
		return 2;
	}
	if (!rc) {
		fprintf(stderr,
			"pi-voice-input: pi-voice-inputd is not running "
			"(systemctl --user start pi-voice-input)\n");
		return 2;
	}
	return 0;
}

/* Returns the reply, or NULL when the call failed. The caller unrefs. */
static DBusMessage *call_iface(const char *iface, const char *member,
			       int first_type, ...)
{
	DBusMessage *msg, *reply;
	DBusMessageIter it;
	DBusError err;
	va_list ap;
	int type;

	msg = dbus_message_new_method_call(BUS_NAME, BUS_PATH, iface, member);
	if (!msg)
		return NULL;
	if (first_type != DBUS_TYPE_INVALID) {
		dbus_message_iter_init_append(msg, &it);
		va_start(ap, first_type);
		for (type = first_type; type != DBUS_TYPE_INVALID;
		     type = va_arg(ap, int)) {
			if (type == DBUS_TYPE_BOOLEAN) {
				dbus_bool_t v = va_arg(ap, int) ? TRUE : FALSE;
				dbus_message_iter_append_basic(&it, type, &v);
			} else if (type == DBUS_TYPE_STRING) {
				const char *v = va_arg(ap, const char *);
				dbus_message_iter_append_basic(&it, type, &v);
			}
		}
		va_end(ap);
	}
	dbus_error_init(&err);
	reply = dbus_connection_send_with_reply_and_block(conn, msg, 30000, &err);
	dbus_message_unref(msg);
	if (!reply) {
		fprintf(stderr, "pi-voice-input: %s.%s failed: %s\n", iface,
			member, err.message ? err.message : "no reply");
		dbus_error_free(&err);
	}
	return reply;
}

static DBusMessage *call(const char *member, int first_type, ...)
{
	/* Reuse the attribute-carrying variant by forwarding the same args. The
	 * varargs cannot be forwarded twice, so this is a thin duplicate of the
	 * common case: our own interface, no interface prefix. */
	DBusMessage *msg, *reply;
	DBusMessageIter it;
	DBusError err;
	va_list ap;
	int type;

	msg = dbus_message_new_method_call(BUS_NAME, BUS_PATH, BUS_IFACE, member);
	if (!msg)
		return NULL;
	if (first_type != DBUS_TYPE_INVALID) {
		dbus_message_iter_init_append(msg, &it);
		va_start(ap, first_type);
		for (type = first_type; type != DBUS_TYPE_INVALID;
		     type = va_arg(ap, int)) {
			if (type == DBUS_TYPE_BOOLEAN) {
				dbus_bool_t v = va_arg(ap, int) ? TRUE : FALSE;
				dbus_message_iter_append_basic(&it, type, &v);
			} else if (type == DBUS_TYPE_STRING) {
				const char *v = va_arg(ap, const char *);
				dbus_message_iter_append_basic(&it, type, &v);
			}
		}
		va_end(ap);
	}
	dbus_error_init(&err);
	reply = dbus_connection_send_with_reply_and_block(conn, msg, 30000, &err);
	dbus_message_unref(msg);
	if (!reply) {
		fprintf(stderr, "pi-voice-input: %s failed: %s\n", member,
			err.message ? err.message : "no reply");
		dbus_error_free(&err);
	}
	return reply;
}

static int get_enabled(void)
{
	DBusMessage *reply = call("GetState", DBUS_TYPE_INVALID);
	DBusMessageIter it;
	const char *state = "";
	dbus_bool_t enabled = FALSE;
	dbus_uint32_t boots = 0;

	if (!reply)
		return -1;
	if (!dbus_message_iter_init(reply, &it)) {
		dbus_message_unref(reply);
		return -1;
	}
	dbus_message_iter_get_basic(&it, &state);
	dbus_message_iter_next(&it);
	dbus_message_iter_get_basic(&it, &enabled);
	dbus_message_iter_next(&it);
	dbus_message_iter_get_basic(&it, &boots);
	if (getenv("PI_VOICE_VERBOSE"))
		fprintf(stderr, "pi-voice-input: state=%s enabled=%d\n", state,
			enabled);
	dbus_message_unref(reply);
	return enabled ? 1 : 0;
}

static int start_recording(void)
{
	DBusMessage *msg, *reply;
	DBusMessageIter it, dict;
	dbus_bool_t ok = FALSE;

	msg = dbus_message_new_method_call(BUS_NAME, BUS_PATH, BUS_IFACE,
					   "StartRecording");
	if (!msg)
		return 1;
	dbus_message_iter_init_append(msg, &it);
	dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
	dbus_message_iter_close_container(&it, &dict);
	{
		DBusError err;

		dbus_error_init(&err);
		reply = dbus_connection_send_with_reply_and_block(conn, msg,
								  30000, &err);
		dbus_message_unref(msg);
		if (!reply) {
			fprintf(stderr, "pi-voice-input: StartRecording: %s\n",
				err.message ? err.message : "no reply");
			dbus_error_free(&err);
			return 1;
		}
	}
	dbus_message_get_args(reply, NULL, DBUS_TYPE_BOOLEAN, &ok,
			      DBUS_TYPE_INVALID);
	dbus_message_unref(reply);
	if (!ok) {
		fprintf(stderr, "pi-voice-input: dictation not started "
			"(is the switch off?)\n");
		return 1;
	}
	return 0;
}

/* client: press when idle, release when a dictation is already running. The
 * daemon reports "recording"/"recognizing" in GetState, which is what makes
 * the second press of the chord act as the release. */
static int do_client(void)
{
	DBusMessage *reply = call("GetState", DBUS_TYPE_INVALID);
	DBusMessageIter it;
	const char *state = "";
	dbus_bool_t enabled = FALSE;

	if (!reply)
		return 1;
	if (!dbus_message_iter_init(reply, &it)) {
		dbus_message_unref(reply);
		return 1;
	}
	dbus_message_iter_get_basic(&it, &state);
	dbus_message_iter_next(&it);
	dbus_message_iter_get_basic(&it, &enabled);
	dbus_message_unref(reply);

	if (!enabled) {
		fprintf(stderr, "pi-voice-input: 语音输入已关闭，"
			"请在状态栏打开\n");
		return 1;
	}
	if (!strcmp(state, "recording")) {
		reply = call("StopRecording", DBUS_TYPE_INVALID);
		if (reply)
			dbus_message_unref(reply);
		return 0;
	}
	/* idle / recognizing: a new press starts a new utterance. While the
	 * previous one is still decoding the daemon queues nothing; the press is
	 * refused there, so do not start a capture we would have to drop. */
	if (!strcmp(state, "recognizing"))
		return 0;
	return start_recording();
}

int main(int argc, char **argv)
{
	const char *cmd = argc > 1 ? argv[1] : "client";
	int rc;

	if (connect_bus() != 0)
		return 2;

	if (!strcmp(cmd, "client") || !strcmp(cmd, "press")) {
		rc = do_client();
	} else if (!strcmp(cmd, "on") || !strcmp(cmd, "off")) {
		DBusMessage *reply = call("SetEnabled",
					  DBUS_TYPE_BOOLEAN,
					  !strcmp(cmd, "on") ? 1 : 0,
					  DBUS_TYPE_INVALID);
		rc = reply ? 0 : 1;
		if (reply)
			dbus_message_unref(reply);
	} else if (!strcmp(cmd, "toggle")) {
		int cur = get_enabled();

		if (cur < 0) {
			rc = 1;
		} else {
			DBusMessage *reply = call("SetEnabled",
						  DBUS_TYPE_BOOLEAN,
						  cur ? 0 : 1,
						  DBUS_TYPE_INVALID);
			rc = reply ? 0 : 1;
			if (reply)
				dbus_message_unref(reply);
		}
	} else if (!strcmp(cmd, "state")) {
		DBusMessage *reply = call("GetState", DBUS_TYPE_INVALID);
		DBusMessageIter it;
		const char *state = "";
		char state_buf[32];
		dbus_bool_t enabled = FALSE;
		dbus_uint32_t boots = 0;
		const char *rem = "?";

		if (!reply)
			return 1;
		if (!dbus_message_iter_init(reply, &it)) {
			dbus_message_unref(reply);
			return 1;
		}
		dbus_message_iter_get_basic(&it, &state);
		/* Copy before unref: `state` points into the message body, which
		 * dbus_message_unref() frees. Reading it afterwards printed an empty
		 * field -- the trailing bytes happened to survive for `enabled` but
		 * not for the string. */
		snprintf(state_buf, sizeof(state_buf), "%s", state);
		dbus_message_iter_next(&it);
		dbus_message_iter_get_basic(&it, &enabled);
		dbus_message_iter_next(&it);
		dbus_message_iter_get_basic(&it, &boots);
		dbus_message_unref(reply);

		/* The remembered position comes from a second call: it is a property,
		 * not part of GetState, so that a caller that only cares about the
		 * live state does not depend on the persistence layer at all. */
		{
			DBusMessage *pr;
			DBusMessageIter pit, pvar;

			pr = call_iface("org.freedesktop.DBus.Properties", "Get",
					DBUS_TYPE_STRING, BUS_IFACE,
					DBUS_TYPE_STRING, "remembered",
					DBUS_TYPE_INVALID);
			if (pr && dbus_message_iter_init(pr, &pit) &&
			    dbus_message_iter_get_arg_type(&pit) == DBUS_TYPE_VARIANT) {
				dbus_bool_t rem_val = FALSE;

				dbus_message_iter_recurse(&pit, &pvar);
				if (dbus_message_iter_get_arg_type(&pvar) ==
				    DBUS_TYPE_BOOLEAN) {
					dbus_message_iter_get_basic(&pvar, &rem_val);
					rem = rem_val ? "on" : "off";
				}
			}
			if (pr)
				dbus_message_unref(pr);
		}
		printf("%s %d %u %s\n", state_buf, enabled ? 1 : 0, boots, rem);
		rc = 0;
	} else if (!strcmp(cmd, "remembered")) {
		/* The persisted switch position, i.e. what the next login will
		 * restore. It is a separate question from the current state: a
		 * read-only home leaves them different, and that difference is
		 * otherwise invisible. */
		DBusMessage *reply;
		DBusMessageIter it, var;
		dbus_bool_t rem = FALSE;

		reply = call_iface("org.freedesktop.DBus.Properties", "Get",
				   DBUS_TYPE_STRING, BUS_IFACE,
				   DBUS_TYPE_STRING, "remembered",
				   DBUS_TYPE_INVALID);
		if (!reply)
			return 1;
		if (dbus_message_iter_init(reply, &it)) {
			if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT) {
				dbus_message_iter_recurse(&it, &var);
				if (dbus_message_iter_get_arg_type(&var) ==
				    DBUS_TYPE_BOOLEAN)
					dbus_message_iter_get_basic(&var, &rem);
			}
		}
		printf("%s\n", rem ? "on" : "off");
		dbus_message_unref(reply);
		rc = 0;
	} else {
		fprintf(stderr,
			"usage: %s [client|press|on|off|toggle|state|remembered]\n",
			argv[0]);
		rc = 64;
	}

	dbus_connection_flush(conn);
	dbus_connection_unref(conn);
	return rc;
}
