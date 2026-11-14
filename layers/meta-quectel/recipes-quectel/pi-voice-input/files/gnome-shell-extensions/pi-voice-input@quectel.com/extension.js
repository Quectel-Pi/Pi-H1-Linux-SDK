// pi-voice-input -- GNOME status-area switch + text insertion for the
// pi-voice-inputd dictation daemon.
//
// The extension is the daemon's only UI:
//   - top-bar indicator: a microphone icon plus an on/off switch; switching it
//     calls SetEnabled, which makes the daemon load or free the ASR model;
//   - press/release of the push-to-talk key (right Ctrl held alone, read by
//     pi-voice-hotkey) is forwarded to the daemon; Ctrl+Q remains as the
//     keybinding fallback;
//   - recognized text is committed into the focused application through
//     Clutter.InputMethod, the same path IBus engines use. This is the only
//     unprivileged way to insert text on GNOME/Wayland -- /dev/uinput is root
//     only, and keysym injection cannot produce CJK.

import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';

const BUS_NAME = 'org.quectel.PiVoiceInput';
const BUS_PATH = '/org/quectel/PiVoiceInput';
const BUS_IFACE = 'org.quectel.PiVoiceInput';

const IFACE_XML = `
<node>
  <interface name="${BUS_IFACE}">
    <method name="SetEnabled">
      <arg type="b" name="enabled" direction="in"/>
    </method>
    <method name="StartRecording">
      <arg type="a{sv}" name="params" direction="in"/>
      <arg type="b" name="ok" direction="out"/>
    </method>
    <method name="StopRecording"/>
    <method name="GetState">
      <arg type="s" name="state" direction="out"/>
      <arg type="b" name="enabled" direction="out"/>
      <arg type="u" name="boots" direction="out"/>
    </method>
    <signal name="StateChanged">
      <arg type="s" name="state"/>
      <arg type="b" name="enabled"/>
      <arg type="u" name="boots"/>
    </signal>
    <signal name="TextRecognized">
      <arg type="s" name="text"/>
    </signal>
    <signal name="Error">
      <arg type="s" name="message"/>
    </signal>
  </interface>
</node>`;
const STATE_LABEL = {
    off: '语音输入已关闭',
    idle: '语音输入就绪',
    recording: '正在听…',
    recognizing: '识别中…',
};

/* The on-screen push-to-talk button, shown only when no keyboard is attached.
 *
 * Why a button and not the on-screen keyboard: the OSK types characters, it
 * cannot express "held down". A press-and-hold gesture is exactly what a
 * touch panel can deliver and what dictation needs, so it is an St.Button on
 * the shell's own chrome layer (Main.layoutManager.addChrome) -- above every
 * window, present in every workspace, and hit by touch without any focus.
 *
 * Shown only when the daemon reports `keyboard` == false: on a desk with a
 * keyboard the right Ctrl key is strictly better (no pointer trip), and a
 * floating button over the work area is just in the way. */
const PTT_BUTTON = {
    /* Vertically centred and horizontally right of centre.
     *
     * Bottom right is out: once a text field is focused the on-screen keyboard
     * owns the bottom of the screen and would cover the button exactly when it
     * is needed. Dead centre is out too: it lands on the text the user is
     * dictating into. Right of centre, at mid height, stays clear of both the
     * keyboard and the usual text caret. */
    SIZE: 96,
    /* Horizontal anchor as a fraction of the monitor width: 0.78 puts the
     * button about three quarters across, i.e. right of centre. */
    ANCHOR_X: 0.78,
};

const PiVoicePttButton = GObject.registerClass(
class PiVoicePttButton extends St.Button {
    _init(extension) {
        super._init({
            style_class: 'pi-voice-ptt',
            reactive: true,
            can_focus: false,
            track_hover: true,
            width: PTT_BUTTON.SIZE,
            height: PTT_BUTTON.SIZE,
            /* A round button with the microphone glyph; styled by
             * stylesheet.css next to this file (loaded in enable()). */
            child: new St.Icon({
                icon_name: 'audio-input-microphone-symbolic',
                icon_size: 40,
                style_class: 'pi-voice-ptt-icon',
            }),
        });
        this._ext = extension;
        this._pressed = false;

        /* Two input paths, because one is not enough on this board.
         *
         * Clutter delivers a finger as a *touch* event, not as a button event,
         * so `button-press-event` alone never fires on the touch panel --
         * measured: a pointer press on the button started a capture, an ABS_MT
         * touch at the same coordinates did nothing at all, while the same
         * touch elsewhere in the UI worked. St.Button's own touch handling lives
         * in C, so it is not enough either; Clutter.ClickAction was tried next
         * and in Clutter 16 has no `pressed`/`released` signals any more (only
         * clicked/long-press/may-recognize), which fails at connect() time.
         * The actor's own `touch-event` signal is the supported hook.
         *
         * _onPressed/_onReleased are idempotent, so a device that delivers both a
         * touch and a synthesised button (some touchscreens do) is harmless. */
        this.connect('touch-event', (actor, event) => {
            /* Clutter.Event exposes its type as type(); get_event_type() does not
             * exist (the name would collide with GObject's). A throw in here hit
             * every poll that tried to build the button, so the button never
             * appeared at all. */
            switch (event.type()) {
            case Clutter.EventType.TOUCH_BEGIN:
                this._onPressed();
                return Clutter.EVENT_STOP;
            case Clutter.EventType.TOUCH_END:
            case Clutter.EventType.TOUCH_CANCEL:
                this._onReleased();
                return Clutter.EVENT_STOP;
            default:
                return Clutter.EVENT_STOP;
            }
        });

        this.connect('button-press-event', () => {
            this._onPressed();
            return Clutter.EVENT_STOP;
        });
        this.connect('button-release-event', () => {
            this._onReleased();
            return Clutter.EVENT_STOP;
        });
    }

    _onPressed() {
        if (this._pressed)
            return;
        this._pressed = true;
        this.add_style_pseudo_class('active');
        this._ext.startRecording();
    }

    _onReleased() {
        if (!this._pressed)
            return;
        this._pressed = false;
        this.remove_style_pseudo_class('active');
        this._ext.stopRecording();
    }

    setState(state) {
        if (state === 'recording')
            this.add_style_pseudo_class('recording');
        else
            this.remove_style_pseudo_class('recording');
    }
});

const PiVoiceIndicator = GObject.registerClass(
class PiVoiceIndicator extends PanelMenu.Button {
    _init(extension) {
        super._init(0.0, 'Voice Input', false);
        this._ext = extension;
        this._enabled = false;
        this._state = 'off';

        this._icon = new St.Icon({
            icon_name: 'audio-input-microphone-symbolic',
            style_class: 'system-status-icon',
            opacity: 130,
        });
        this.add_child(this._icon);

        this._toggle = new PopupMenu.PopupSwitchMenuItem('语音输入', false,
            {reactive: true});
        this._toggle.connect('toggled', (_item, value) => {
            this._ext.setEnabled(value);
        });
        this.menu.addMenuItem(this._toggle);

        this._statusItem = new PopupMenu.PopupMenuItem('正在连接…', {
            reactive: false,
        });
        this.menu.addMenuItem(this._statusItem);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        const hint = new PopupMenu.PopupMenuItem('按住右 Ctrl 说话，松开上屏', {
            reactive: false,
        });
        this.menu.addMenuItem(hint);
        this._hintItem = hint;

        /* The extension polls text-input focus four times a second; at session
         * end the shell destroys the panel buttons before the extension is
         * disabled, and a poll landing in that window wrote to an already
         * disposed label ("has been already disposed -- impossible to set any
         * property on it"). One flag closes the window. */
        this._alive = true;
        this.connect('destroy', () => {
            this._alive = false;
            this._hintItem = null;
            this._statusItem = null;
        });
    }

    setHint(text) {
        if (this._alive && this._hintItem)
            this._hintItem.label.text = text;
    }

    setState(state, enabled) {
        if (!this._alive)
            return;
        this._state = state;
        this._enabled = enabled;
        // Reflect externally-driven changes (a client calling SetEnabled from a
        // script) without re-emitting "toggled", which would loop back.
        if (this._toggle.state !== enabled)
            this._toggle.setToggleState(enabled);

        const label = STATE_LABEL[state] ?? state;
        if (this._statusItem)
            this._statusItem.label.text = label;

        let iconName = 'audio-input-microphone-symbolic';
        let opacity = 130;
        if (state === 'recording') {
            iconName = 'media-record-symbolic';
            opacity = 255;
        } else if (state === 'recognizing') {
            opacity = 255;
        } else if (enabled) {
            opacity = 200;
        }
        this._icon.icon_name = iconName;
        this._icon.opacity = opacity;
    }
});

export default class PiVoiceInputExtension extends Extension {
    enable() {
        this._indicator = new PiVoiceIndicator(this);

        // The left box, next to the clock, is deliberate: the right box is
        // where Ubuntu/GNOME 50 draws the Quick Settings panel (sound, Wi-Fi,
        // Bluetooth), and an indicator parked there is both visually glued to
        // that panel and easy to hit by mistake -- clicking the neighbour
        // opens the system menu instead of this switch. The left box holds
        // only the clock, so the microphone stands on its own.
        Main.panel.addToStatusArea(this.uuid, this._indicator, 0, 'left');

        this._proxy = null;
        this._signals = [];
        this._pttButton = null;
        this._pttPollId = 0;
        this._stylesheet = null;
        this._hadKeyboard = true;
        this._enabled = false;
        this._keyboardPresent = true;
        /* The button's visibility follows the text-input focus, which has no
         * signal, so it is sampled. Quarter second: fast enough that the button
         * is there by the time the user reaches for it, slow enough to be free. */
        this._pttPollId = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 250, () => {
            /* One line per *change* of the inputs the decision uses; this is
             * what tells a missing button apart from a broken daemon. */
            const im = !!Main.inputMethod;
            const focus = this._textInputFocused();
            if (focus !== this._lastFocus || im !== this._lastIm ||
                this._enabled !== this._lastEnabled ||
                this._keyboardPresent !== this._lastKeyboard) {
                log(`pi-voice-input: focus=${focus} inputMethod=${im} ` +
                    `enabled=${this._enabled} keyboard=${this._keyboardPresent}`);
                this._lastFocus = focus;
                this._lastIm = im;
                this._lastEnabled = this._enabled;
                this._lastKeyboard = this._keyboardPresent;
            }
            this._syncPttButton(this._enabled, this._keyboardPresent);
            return GLib.SOURCE_CONTINUE;
        });
        this._watchId = Gio.bus_watch_name(
            Gio.BusType.SESSION,
            BUS_NAME,
            Gio.BusNameWatcherFlags.NONE,
            () => this._onDaemonAppeared(),
            () => this._onDaemonVanished());
    }

    disable() {
        if (this._pttPollId) {
            GLib.source_remove(this._pttPollId);
            this._pttPollId = 0;
        }
        if (this._watchId) {
            Gio.bus_unwatch_name(this._watchId);
            this._watchId = 0;
        }
        this._teardownProxy();
        this._hidePttButton();
        if (this._stylesheet) {
            try {
                const theme = St.ThemeContext.get_for_stage(global.stage).get_theme();
                theme.unload_stylesheet(Gio.File.new_for_path(this._stylesheet));
            } catch (e) {
                log(`pi-voice-input: stylesheet unload failed: ${e}`);
            }
            this._stylesheet = null;
        }
        this._indicator?.destroy();
        this._indicator = null;    }

    _onDaemonAppeared() {
        const node = Gio.DBusNodeInfo.new_for_xml(IFACE_XML);
        this._nodeInfo = node;
        /* A second appearance without a vanish in between (the shell calls
         * this again after a session-mode change) would leak the previous
         * proxy and its signal handlers, and two proxies mean two commits of
         * every recognized sentence. */
        this._teardownProxy();
        Gio.DBus.session.call(
            BUS_NAME, BUS_PATH, 'org.freedesktop.DBus.Properties',
            'GetAll',
            new GLib.Variant('(s)', [BUS_IFACE]),
            new GLib.VariantType('(a{sv})'),
            Gio.DBusCallFlags.NONE, -1, null,
            (_conn, res) => {
                try {
                    /* call_finish() hands back one GLib.Variant for the whole
                     * reply tuple, not an array: `const [props] = ...` threw
                     * "(intermediate value).call_finish(...) is not iterable"
                     * on gjs 1.82 and left the indicator stuck on
                     * "正在连接…" while the daemon was running fine. Unpack
                     * first, then destructure. */
                    const reply = Gio.DBus.session.call_finish(res);
                    const [props] = reply.deepUnpack();
                    this._applyProps(props);
                } catch (e) {
                    log(`pi-voice-input: GetAll failed: ${e}`);
                }
            });

        this._proxy = new Gio.DBusProxy({
            g_connection: Gio.DBus.session,
            g_name: BUS_NAME,
            g_object_path: BUS_PATH,
            g_interface_name: BUS_IFACE,
            g_interface_info: node.interfaces[0],
            g_flags: Gio.DBusProxyFlags.GET_INVALIDATED_PROPERTIES,
        });

        this._proxy.init_async(GLib.PRIORITY_DEFAULT, null, (_p, res) => {
            try {
                this._proxy.init_finish(res);
            } catch (e) {
                log(`pi-voice-input: proxy init failed: ${e}`);
                return;
            }
            this._signals.push(this._proxy.connectSignal('StateChanged',
                (_proxy, _sender, [state, enabled, _boots]) => {
                    this._applyState(state, enabled);
                }));
            this._signals.push(this._proxy.connectSignal('TextRecognized',
                (_proxy, _sender, [text]) => this._commit(text)));
            this._signals.push(this._proxy.connectSignal('Error',
                (_proxy, _sender, [message]) => this._notify(message)));

            this._proxy.call('GetState', null, Gio.DBusCallFlags.NONE, -1,
                null, (_proxy, result) => {
                    try {
                        /* Same trap as GetAll above: unpack, then destructure. */
                        const reply = this._proxy.call_finish(result);
                        const [state, enabled] = reply.deepUnpack();
                        this._applyState(state, enabled);
                    } catch (e) {
                        log(`pi-voice-input: GetState failed: ${e}`);
                    }
                });
        });
    }

    /* Properties.GetAll returns a{sv}; the four keys the shell needs are pulled
     * out by hand so a daemon that grows more properties does not break the
     * switch. */
    _applyProps(props) {
        if (!props)
            return;
        const get = (k, dflt) =>
            props[k] ? props[k].deepUnpack() : dflt;
        const state = get('state', undefined);
        if (typeof state !== 'string')
            return;
        const enabled = get('enabled', false) === true;
        const keyboard = get('keyboard', true) === true;
        this._applyState(state, enabled);
        this._enabled = enabled;
        this._keyboardPresent = keyboard;
        this._syncPttButton(enabled, keyboard);
    }

    /* Show the button when the feature is on, no keyboard can carry right Ctrl,
     * and a text field has focus -- the same condition that makes the on-screen
     * keyboard useful. Leaving the field hides the button again, which is what
     * the user expects: dictation without somewhere to type has no meaning, and
     * a floating button that outlives the input box is just clutter.
     *
     * Rebuilt only on a transition, so a state change while the button is up
     * does not steal the pointer mid-hold. */
    _syncPttButton(enabled, hasKeyboard) {
        const want = enabled && !hasKeyboard && this._textInputFocused();
        this._hadKeyboard = hasKeyboard;
        if (want && !this._pttButton) {
            this._ensureStylesheet();
            this._indicator?.setHint('按住屏幕中间的话筒说话，松开上屏');
            this._pttButton = new PiVoicePttButton(this);
            Main.layoutManager.addChrome(this._pttButton, {
                affectsInputRegion: true,
                affectsStruts: false,
            });
            this._placePttButton();
            /* Re-place on every monitor change; the work area is only known
             * after the layout manager has one. */
            this._pttPlacedId = Main.layoutManager.connect('monitors-changed',
                () => this._placePttButton());
            /* One line per transition, not per poll: this is the only trace of
             * why the button is or is not on screen when it is reported missing. */
            log('pi-voice-input: on-screen button shown (no keyboard, text field focused)');
        } else if (!want) {
            this._hidePttButton();
        }
    }

    /* True while a Wayland client has a text input focused.
     *
     * Main.inputMethod is the shell's Clutter.InputMethod; its _currentFocus is
     * set in vfunc_focus_in() and cleared in vfunc_focus_out(), i.e. exactly
     * "a text field is being edited". There is no signal for it (the hooks are
     * vfuncs), so the state is polled -- one property read every quarter
     * second, which is nothing next to what the shell already does per frame.
     *
     * Main.keyboard.visible is NOT used: it tracks the on-screen keyboard, which
     * this image leaves disabled, so it would always report false. */
    _textInputFocused() {
        const im = Main.inputMethod;
        if (!im)
            return false;
        return !!im._currentFocus;
    }

    _placePttButton() {
        if (!this._pttButton || !Main.layoutManager.primaryMonitor)
            return;
        const mon = Main.layoutManager.primaryMonitor;
        this._pttButton.set_position(
            mon.x + Math.floor(mon.width * PTT_BUTTON.ANCHOR_X - PTT_BUTTON.SIZE / 2),
            mon.y + Math.floor((mon.height - PTT_BUTTON.SIZE) / 2));
        this._pttButton.show();
    }

    _hidePttButton() {
        if (this._pttPlacedId) {
            Main.layoutManager.disconnect(this._pttPlacedId);
            this._pttPlacedId = 0;
        }
        if (!this._pttButton)
            return;
        /* The field lost focus mid-hold: the release will never arrive on a
         * button that is about to be destroyed, so end the capture here or the
         * daemon keeps recording until its 60 s cap. */
        if (this._pttButton._pressed) {
            this._pttButton._pressed = false;
            this.stopRecording();
        }
        this._pttButton.destroy();
        this._pttButton = null;
        this._indicator?.setHint('按住右 Ctrl 说话，松开上屏');
        log('pi-voice-input: on-screen button hidden');
    }

    /* Loaded from the extension's own directory so the shell manages its
     * lifetime: pairing load_stylesheet/unload_stylesheet with enable()/disable()
     * is the documented way, and disable() then cannot leave a half-removed
     * class behind. */
    _ensureStylesheet() {
        if (this._stylesheet)
            return;
        try {
            this._stylesheet = `${this.dir.get_path()}/stylesheet.css`;
            const theme = St.ThemeContext.get_for_stage(global.stage).get_theme();
            theme.load_stylesheet(Gio.File.new_for_path(this._stylesheet));
        } catch (e) {
            log(`pi-voice-input: stylesheet failed: ${e}`);
            this._stylesheet = null;
        }
    }

    /* Called by the on-screen button: a touch 'pressed' is the press half of
     * the same gesture the keyboard hotkey forwards. */
    startRecording() {
        this._callDaemon('StartRecording', new GLib.Variant('(a{sv})', [{}]),
            `pi-voice-input: StartRecording (button) failed:`);
    }

    stopRecording() {
        this._callDaemon('StopRecording', null,
            `pi-voice-input: StopRecording (button) failed:`);
    }

    _callDaemon(method, variant, errPrefix) {
        const send = (obj, res) => {
            try {
                obj.call_finish(res);
            } catch (e) {
                log(`${errPrefix} ${e}`);
            }
        };
        if (this._proxy) {
            this._proxy.call(method, variant, Gio.DBusCallFlags.NONE, -1,
                null, (p, r) => send(p, r));
            return;
        }
        Gio.DBus.session.call(BUS_NAME, BUS_PATH, BUS_IFACE, method, variant,
            null, Gio.DBusCallFlags.NONE, -1, null, (c, r) => send(c, r));
    }

    _onDaemonVanished() {
        this._teardownProxy();
        this._applyState('off', false);
        if (this._indicator)
            this._indicator._statusItem.label.text = '语音输入服务未运行';
    }

    _teardownProxy() {
        if (!this._proxy)
            return;
        for (const id of this._signals)
            this._proxy.disconnectSignal(id);
        this._signals = [];
        this._proxy = null;
    }

    _applyState(state, enabled = false) {
        this._indicator?.setState(state, enabled);
        this._pttButton?.setState(state);
    }

    setEnabled(enabled) {
        // Call through the connection rather than the proxy when the proxy is
        // not up yet: the daemon is D-Bus activated, so the call itself starts
        // it. Without this the switch only worked once the daemon happened to
        // be running already, which is exactly what a fresh login is not.
        const done = (proxy, res) => {
            try {
                proxy.call_finish(res);
            } catch (e) {
                log(`pi-voice-input: SetEnabled failed: ${e}`);
                this._notify('语音输入服务启动失败');
            }
        };
        const variant = new GLib.Variant('(b)', [enabled]);

        if (this._proxy) {
            this._proxy.call('SetEnabled', variant, Gio.DBusCallFlags.NONE,
                -1, null, (proxy, res) => done(proxy, res));
            return;
        }
        Gio.DBus.session.call(BUS_NAME, BUS_PATH, BUS_IFACE, 'SetEnabled',
            variant, null, Gio.DBusCallFlags.NONE, -1, null, (proxy, res) => {
                try {
                    proxy.call_finish(res);
                } catch (e) {
                    log(`pi-voice-input: SetEnabled (activation) failed: ${e}`);
                    this._notify('语音输入服务启动失败');
                }
            });
    }

    _commit(text) {
        if (!text)
            return;
        if (!Main.inputMethod || typeof Main.inputMethod.commit !== 'function') {
            this._notify('当前环境不支持文字输入');
            return;
        }
        try {
            Main.inputMethod.commit(text);
        } catch (e) {
            log(`pi-voice-input: commit failed: ${e}`);
            this._notify('文字输入失败');
        }
    }

    _notify(message) {
        Main.notify('语音输入', message);
    }
}
