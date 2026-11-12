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
    }

    setState(state, enabled) {
        this._state = state;
        this._enabled = enabled;
        // Reflect externally-driven changes (a client calling SetEnabled from a
        // script) without re-emitting "toggled", which would loop back.
        if (this._toggle.state !== enabled)
            this._toggle.setToggleState(enabled);

        const label = STATE_LABEL[state] ?? state;
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
        this._watchId = Gio.bus_watch_name(
            Gio.BusType.SESSION,
            BUS_NAME,
            Gio.BusNameWatcherFlags.NONE,
            () => this._onDaemonAppeared(),
            () => this._onDaemonVanished());
    }

    disable() {
        if (this._watchId) {
            Gio.bus_unwatch_name(this._watchId);
            this._watchId = 0;
        }
        this._teardownProxy();
        this._indicator?.destroy();
        this._indicator = null;
    }

    _onDaemonAppeared() {
        const node = Gio.DBusNodeInfo.new_for_xml(IFACE_XML);
        this._nodeInfo = node;
        Gio.DBus.session.call(
            BUS_NAME, BUS_PATH, 'org.freedesktop.DBus.Properties',
            'GetAll',
            new GLib.Variant('(s)', [BUS_IFACE]),
            new GLib.VariantType('(a{sv})'),
            Gio.DBusCallFlags.NONE, -1, null,
            (_conn, res) => {
                try {
                    const [props] = Gio.DBus.session.call_finish(res);
                    this._applyState(props);
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
                        const [state, enabled] = this._proxy.call_finish(result);
                        this._applyState(state, enabled);
                    } catch (e) {
                        log(`pi-voice-input: GetState failed: ${e}`);
                    }
                });
        });
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
