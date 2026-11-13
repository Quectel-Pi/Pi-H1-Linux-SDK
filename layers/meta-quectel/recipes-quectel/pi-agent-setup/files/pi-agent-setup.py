#!/usr/bin/env python3
# pi-agent-setup -- first-run assistant for the Pi coding agent
#
# A GTK4 wizard that the GNOME session starts on login while pi is not
# installed yet. It never installs anything by itself: the person in front of
# the board either starts the installation, postpones it, or declines.
# Everything it does runs as that user (see engine.py) -- no polkit dialog, no
# password, nothing written outside $HOME.
#
# Widgets are plain GTK4 on purpose. The two sample rootfs images ship
#   Debian 13  GTK 4.18 + libadwaita 1.7
#   Ubuntu 26  GTK 4.22 + libadwaita 1.9
# but the SDK's own Yocto (LINUX) rootfs carries GTK 4.14, and whoever runs
# this in a container or over a different image may have less. libadwaita is
# used only when it is new enough for the classes we need, so the same file
# renders on all of them.

import json
import os
import sys
import threading
import time
import traceback

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gio, GLib, Gtk  # noqa: E402

# engine.py sits next to this file in the source tree but in
# ${datadir}/pi-agent-setup once installed, so both are searched.
_HERE = os.path.dirname(os.path.abspath(__file__))
for _candidate in (_HERE, os.path.join(_HERE, "..", "share/pi-agent-setup"),
                   "/usr/share/pi-agent-setup"):
    _candidate = os.path.abspath(_candidate)
    if os.path.exists(os.path.join(_candidate, "engine.py")):
        sys.path.insert(0, _candidate)
        break
import engine  # noqa: E402

APP_ID = "org.quectel.PiAgentSetup"
TITLE = "Pi 编程助手安装向导"

# libadwaita is used for the window itself (header bar look, and the toast
# overlay used for the "7 天后再提醒" confirmation). The bar is deliberately
# low: this wizard only needs Adw.ApplicationWindow, Adw.ToastOverlay and
# Adw.Toast, all of which exist since 1.0. The form widgets are plain GTK, so
# libadwaita 1.2's EntryRow is not required.
#
# The base class has to follow this flag -- Gtk.ApplicationWindow has
# set_child(), Adw.ApplicationWindow has set_content() and neither has the
# other. Getting that wrong is invisible on a machine with an old libadwaita
# (the Adw path is never taken there) and crashes the moment the wizard runs
# on a board that has it, so _make_window_base() picks the base class from the
# same condition the rest of the file uses.
Adw = None
try:
    gi.require_version("Adw", "1")
    from gi.repository import Adw as _Adw  # noqa: E402

    if hasattr(_Adw, "ApplicationWindow") and hasattr(_Adw, "ToastOverlay"):
        Adw = _Adw
except (ValueError, ImportError):
    Adw = None

_WINDOW_BASE = Adw.ApplicationWindow if Adw is not None else Gtk.ApplicationWindow

HOW_TO_START = """\
安装完成后这样开始用 pi：

  1. 打开终端（Ctrl+Alt+T，或在应用列表里找“终端”）
  2. 进入你要工作的目录，例如：
       cd ~/my-project
  3. 输入 pi 回车

进入 pi 之后：
  /model    切换模型
  /login    更换 API Key 或用账号登录
  /help     查看全部命令
  Ctrl+C    退出（会话会自动保存，pi -c 继续上次对话）

配置文件都在 ~/.pi/agent/ 下：
  models.json    端点与模型
  auth.json      API Key（权限 600）
  settings.json  默认模型与扩展
"""


class SetupWindow(_WINDOW_BASE):
    def __init__(self, app, start_page="welcome"):
        super().__init__(application=app, title=TITLE)
        self.set_default_size(780, 640)
        self.set_size_request(560, 520)

        self.result = "closed"          # install | configured | snoozed | declined
        self.busy = False
        self.mirror_check = None

        if Adw is not None:
            self.toast_overlay = Adw.ToastOverlay()
            self.holder = self.toast_overlay
        else:
            self.toast_overlay = None
            self.holder = None

        self.stack = Gtk.Stack()
        self.stack.set_transition_type(Gtk.StackTransitionType.CROSSFADE)
        self.stack.add_named(self._build_welcome(), "welcome")
        self.stack.add_named(self._build_progress(), "progress")
        self.stack.add_named(self._build_failure(), "failure")
        self.stack.add_named(self._build_config(), "config")
        self.stack.add_named(self._build_done(), "done")

        self.header = Gtk.HeaderBar()
        self.header.set_show_title_buttons(False)

        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        box.append(self.header)
        box.append(self.stack)
        # Adw.ApplicationWindow takes set_content(); Gtk.ApplicationWindow takes
        # set_child(). Calling the wrong one is an AttributeError at runtime.
        if self.holder is not None:
            self.holder.set_child(box)
            self.set_content(self.holder)
        else:
            self.set_child(box)

        css = Gtk.CssProvider()
        css.load_from_data(CSS.encode("utf-8"))
        Gtk.StyleContext.add_provider_for_display(
            Gdk.Display.get_default(), css,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        self.connect("close-request", self._on_close_request)
        # --page config is the half-finished case (pi installed, endpoint never
        # filled in). Opening on "install pi?" there would ask the wrong
        # question, so land on the form and prefill what is known.
        if start_page in ("welcome", "config", "done"):
            if start_page == "config":
                self._prefill_from_install()
            self.stack.set_visible_child_name(start_page)
        else:
            self.stack.set_visible_child_name("welcome")

    def _prefill_from_install(self):
        info = engine._read_json(engine.MARKER_PATH, {}) or {}
        config = info.get("config") or {}
        if config.get("baseUrl"):
            self.url_row.entry.set_text(config["baseUrl"])
        if config.get("model"):
            self.model_row.entry.set_text(config["model"])
        self.header.set_show_title_buttons(True)

    # ------------------------------------------------------------------
    # reusable bits
    # ------------------------------------------------------------------
    def _page(self):
        """Scrollable page body with the usual GNOME margins."""
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        box.set_margin_top(30)
        box.set_margin_bottom(28)
        box.set_margin_start(32)
        box.set_margin_end(32)

        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        sw.set_child(box)
        sw.body = box
        return sw

    def _heading(self, text):
        label = Gtk.Label(label=text)
        label.set_xalign(0)
        label.set_wrap(True)
        label.add_css_class("pi-setup-title")
        return label

    def _paragraph(self, text, dim=True):
        label = Gtk.Label(label=text)
        label.set_xalign(0)
        label.set_wrap(True)
        if dim:
            label.add_css_class("dim-label")
        return label

    def _field(self, title, placeholder="", secret=False):
        row = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        label = Gtk.Label(label=title)
        label.set_xalign(0)
        label.add_css_class("pi-setup-field")
        row.append(label)

        # Gtk.Entry, not Gtk.PasswordEntry: PasswordEntry has no
        # set_placeholder_text() in GTK 4.6 (it gained one later), and the
        # plain entry plus our own reveal button behaves the same on every
        # version this wizard has to run on.
        entry = Gtk.Entry()
        entry.set_hexpand(True)
        entry.set_placeholder_text(placeholder)
        if secret:
            entry.set_visibility(False)
            entry.set_input_purpose(Gtk.InputPurpose.PASSWORD)
            reveal = Gtk.ToggleButton()
            reveal.set_icon_name("view-reveal-symbolic")
            reveal.set_tooltip_text("显示/隐藏")
            reveal.set_valign(Gtk.Align.CENTER)
            reveal.connect(
                "toggled",
                lambda btn: entry.set_visibility(btn.get_active()))
            wrapper = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=6)
            wrapper.append(entry)
            wrapper.append(reveal)
            wrapper.entry = entry
            row.append(wrapper)
            row.entry = entry
            row.reveal = reveal
            return row

        row.append(entry)
        row.entry = entry
        return row

    def _icon_line(self, icon_name, text):
        row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        image = Gtk.Image.new_from_icon_name(icon_name)
        image.set_pixel_size(18)
        image.add_css_class("dim-label")
        image.set_valign(Gtk.Align.START)
        label = Gtk.Label(label=text)
        label.set_wrap(True)
        label.set_xalign(0)
        label.set_hexpand(True)
        row.append(image)
        row.append(label)
        return row

    def _toast(self, message):
        if self.toast_overlay is not None:
            self.toast_overlay.add_toast(Adw.Toast.new(message))
        else:
            self.status_hint.set_text(message)

    # ------------------------------------------------------------------
    # page 1: what is this
    # ------------------------------------------------------------------
    def _build_welcome(self):
        sw = self._page()
        box = sw.body

        icon = Gtk.Image.new_from_icon_name("utilities-terminal-symbolic")
        icon.set_pixel_size(72)
        icon.set_margin_bottom(4)
        box.append(icon)

        box.append(self._heading("安装 Pi 编程助手？"))
        box.append(self._paragraph(
            "Pi 是一个在终端里工作的编程智能体：能读写文件、执行命令、"
            "帮你排查和修改代码。它不是系统组件，会装在你自己的主目录里，"
            "随时可以删除。"))

        details = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        details.set_margin_top(6)
        for icon_name, text in (
            ("network-transmit-receive-symbolic",
             "需要联网：向导会先检查网络，再下载 Node.js 运行时和 pi 本体"
             "（合计约 250 MB，视网速需要几分钟）"),
            ("system-lock-screen-symbolic",
             "不需要管理员密码：全部装在你的主目录（~/.local），不修改系统文件"),
            ("edit-clear-symbolic",
             "随时可卸载：安装完成后会给出一条清理命令"),
        ):
            details.append(self._icon_line(icon_name, text))
        box.append(details)

        buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        buttons.set_halign(Gtk.Align.CENTER)
        buttons.set_margin_top(10)

        self.install_button = Gtk.Button(label="开始安装")
        self.install_button.add_css_class("suggested-action")
        self.install_button.set_size_request(200, 44)
        self.install_button.connect("clicked", self._on_install_clicked)

        self.later_button = Gtk.Button(label="下次询问")
        self.later_button.set_size_request(150, 44)
        self.later_button.set_tooltip_text("7 天后再提醒")
        self.later_button.connect("clicked", self._on_later_clicked)

        self.never_button = Gtk.Button(label="不再安装")
        self.never_button.set_size_request(150, 44)
        self.never_button.set_tooltip_text("以后不再自动弹出这个向导")
        self.never_button.connect("clicked", self._on_never_clicked)

        buttons.append(self.install_button)
        buttons.append(self.later_button)
        buttons.append(self.never_button)
        box.append(buttons)

        self.status_hint = self._paragraph("", dim=True)
        self.status_hint.set_xalign(0.5)
        box.append(self.status_hint)
        return sw

    # ------------------------------------------------------------------
    # page 2: progress
    # ------------------------------------------------------------------
    def _build_progress(self):
        sw = self._page()
        box = sw.body

        self.progress_title = self._heading("准备中…")
        box.append(self.progress_title)

        self.progress_bar = Gtk.ProgressBar()
        self.progress_bar.set_show_text(False)
        box.append(self.progress_bar)

        self.progress_hint = self._paragraph("")
        box.append(self.progress_hint)

        self.log_view = Gtk.TextView()
        self.log_view.set_editable(False)
        self.log_view.set_monospace(True)
        self.log_view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.log_view.set_margin_top(8)
        self.log_view.set_margin_bottom(8)
        self.log_view.set_margin_start(8)
        self.log_view.set_margin_end(8)
        self.log_buffer = self.log_view.get_buffer()

        log_scroll = Gtk.ScrolledWindow()
        log_scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        log_scroll.set_vexpand(True)
        log_scroll.set_child(self.log_view)

        log_frame = Gtk.Frame()
        log_frame.set_child(log_scroll)
        box.append(log_frame)

        actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        actions.set_halign(Gtk.Align.END)
        hide = Gtk.Button(label="转到后台")
        hide.set_tooltip_text("关闭窗口，安装继续进行")
        hide.connect("clicked", lambda *_: self.close())
        actions.append(hide)
        box.append(actions)
        return sw

    # ------------------------------------------------------------------
    # page 3: failure (network / download / npm)
    # ------------------------------------------------------------------
    def _build_failure(self):
        sw = self._page()
        box = sw.body

        icon = Gtk.Image.new_from_icon_name("dialog-warning-symbolic")
        icon.set_pixel_size(64)
        box.append(icon)
        box.append(self._heading("安装没有完成"))

        self.failure_description = Gtk.Label()
        self.failure_description.set_wrap(True)
        self.failure_description.set_xalign(0)
        self.failure_description.set_selectable(True)
        self.failure_description.add_css_class("dim-label")
        box.append(self.failure_description)

        self.mirror_check = Gtk.CheckButton(label="下载失败时改用国内镜像（nodejs.org 不通时）")
        self.mirror_check.set_active(bool(engine.read_state().get("use_mirror")))
        box.append(self.mirror_check)

        buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        buttons.set_halign(Gtk.Align.CENTER)
        retry = Gtk.Button(label="重试")
        retry.add_css_class("suggested-action")
        retry.set_size_request(160, 44)
        retry.connect("clicked", self._on_install_clicked)
        later = Gtk.Button(label="稍后再说")
        later.set_size_request(140, 44)
        later.connect("clicked", self._on_later_clicked)
        buttons.append(retry)
        buttons.append(later)
        box.append(buttons)
        return sw

    # ------------------------------------------------------------------
    # page 4: API endpoint
    # ------------------------------------------------------------------
    def _build_config(self):
        sw = self._page()
        box = sw.body

        box.append(self._heading("配置 API 端点"))
        box.append(self._paragraph(
            "pi 本身不带模型。填一个 OpenAI 兼容的服务地址和 Key，"
            "向导会写进 ~/.pi/agent/ 并设为默认模型。"))

        self.url_row = self._field(
            "API URL", "https://api.openai.com/v1")
        self.url_row.entry.set_text("https://api.openai.com/v1")
        self.url_row.entry.connect("changed", self._on_url_changed)
        box.append(self.url_row)

        self.key_row = self._field("API Key", "sk-…", secret=True)
        box.append(self.key_row)

        api_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        api_label = Gtk.Label(label="接口类型")
        api_label.set_xalign(0)
        api_label.add_css_class("pi-setup-field")
        api_box.append(api_label)
        self.api_row = Gtk.DropDown.new_from_strings(
            [label for _, label in engine.API_TYPES])
        self.api_row.set_selected(0)
        # Switching the API flavour invalidates a probe done against the old one.
        self.api_row.connect("notify::selected", self._invalidate_probe)
        api_box.append(self.api_row)
        box.append(api_box)

        # Model chooser: a DropDown filled from the endpoint plus a plain entry
        # for anything the endpoint does not list (virtual models, routers that
        # hide the list). Both write into the same value, so typing is never
        # blocked by a failed probe -- that is the important half: a wizard that
        # only allowed picking from a list would be unusable on an endpoint that
        # refuses GET /models, which many gateways do.
        model_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        model_head = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        model_label = Gtk.Label(label="模型 ID")
        model_label.set_xalign(0)
        model_label.set_hexpand(True)
        model_label.add_css_class("pi-setup-field")
        model_head.append(model_label)
        self.probe_button = Gtk.Button(label="探测模型")
        self.probe_button.set_tooltip_text("用上面的 URL 和 Key 查询端点提供哪些模型")
        self.probe_button.connect("clicked", self._on_probe_clicked)
        model_head.append(self.probe_button)
        model_box.append(model_head)

        self.model_dropdown = Gtk.DropDown.new_from_strings(["（未探测）"])
        self.model_dropdown.set_sensitive(False)
        self.model_dropdown.set_tooltip_text("点“探测模型”后这里会列出端点提供的模型")
        self.model_dropdown.connect("notify::selected", self._on_model_selected)
        model_box.append(self.model_dropdown)

        self.model_entry = Gtk.Entry()
        self.model_entry.set_placeholder_text("gpt-4o-mini，或直接填端点要求的模型名")
        self.model_entry.set_text("gpt-4o-mini")
        self.model_entry.set_tooltip_text("可以直接手填；探测到的模型也会写进这里")
        model_box.append(self.model_entry)

        # kept as .entry so the rest of the wizard (which was written against a
        # single entry) keeps working unchanged.
        self.model_row = model_box
        self.model_row.entry = self.model_entry
        box.append(model_box)

        self.models_found = []          # [(id, label)] from the last probe
        self._clear_placeholder_models()

        self.test_result = self._paragraph("")
        self.test_result.set_selectable(True)
        box.append(self.test_result)

        buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        buttons.set_halign(Gtk.Align.END)
        self.test_button = Gtk.Button(label="测试连接")
        self.test_button.set_size_request(130, 44)
        self.test_button.connect("clicked", self._on_test_clicked)
        self.finish_button = Gtk.Button(label="保存并完成")
        self.finish_button.add_css_class("suggested-action")
        self.finish_button.set_size_request(170, 44)
        self.finish_button.connect("clicked", self._on_finish_clicked)
        buttons.append(self.test_button)
        buttons.append(self.finish_button)
        box.append(buttons)
        return sw

    # ------------------------------------------------------------------
    # page 5: done
    # ------------------------------------------------------------------
    def _build_done(self):
        sw = self._page()
        box = sw.body

        icon = Gtk.Image.new_from_icon_name("emblem-ok-symbolic")
        icon.set_pixel_size(64)
        box.append(icon)
        box.append(self._heading("安装完成"))

        self.done_description = self._paragraph("")
        self.done_description.set_xalign(0)
        box.append(self.done_description)

        howto = Gtk.TextView()
        howto.set_editable(False)
        howto.set_cursor_visible(False)
        howto.set_monospace(True)
        howto.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        howto.set_margin_top(10)
        howto.set_margin_bottom(10)
        howto.set_margin_start(12)
        howto.set_margin_end(12)
        howto.get_buffer().set_text(HOW_TO_START)

        howto_scroll = Gtk.ScrolledWindow()
        howto_scroll.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        howto_scroll.set_min_content_height(260)
        howto_scroll.set_child(howto)
        box.append(howto_scroll)

        buttons = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=12)
        buttons.set_halign(Gtk.Align.CENTER)

        copy = Gtk.Button(label="复制启动步骤")
        copy.set_size_request(160, 44)
        copy.connect("clicked", self._on_copy_clicked)
        buttons.append(copy)

        close = Gtk.Button(label="知道了")
        close.add_css_class("suggested-action")
        close.set_size_request(150, 44)
        close.connect("clicked", lambda *_: self.close())
        buttons.append(close)
        box.append(buttons)
        return sw

    # ------------------------------------------------------------------
    # log / progress plumbing
    # ------------------------------------------------------------------
    def _append_log(self, line):
        self.log_buffer.insert(self.log_buffer.get_end_iter(), line + "\n")
        mark = self.log_buffer.create_mark(
            None, self.log_buffer.get_end_iter(), False)
        self.log_view.scroll_mark_onscreen(mark)
        return False

    def _log_from_thread(self, line):
        GLib.idle_add(self._append_log, line)

    def _set_progress(self, title=None, hint=None, fraction=None):
        def apply():
            if title is not None:
                self.progress_title.set_text(title)
            if hint is not None:
                self.progress_hint.set_text(hint)
            if fraction is None:
                self.progress_bar.pulse()
            else:
                self.progress_bar.set_fraction(max(0.0, min(1.0, fraction)))
            return False
        GLib.idle_add(apply)

    def _on_url_changed(self, entry):
        text = entry.get_text().strip()
        # People paste the full endpoint; models.json wants the base URL.
        if text.endswith("/chat/completions"):
            entry.set_text(text[: -len("/chat/completions")])
            return
        # A different host/port means a different model list.
        self._invalidate_probe()

    def _invalidate_probe(self, *_args):
        if getattr(self, "models_found", None):
            self._clear_placeholder_models()
            self.test_result.set_text("端点已改动，之前的模型列表已失效，请重新探测。")

    # ------------------------------------------------------------------
    # welcome / failure actions
    # ------------------------------------------------------------------
    def _on_later_clicked(self, *_):
        state = engine.read_state()
        days = state.get("snooze_days") or engine.DEFAULT_SNOOZE_DAYS
        engine.snooze(days)
        self.result = "snoozed"
        self._toast("%d 天后再提醒。也可以随时从应用列表重新打开向导" % days)
        GLib.timeout_add(900, self.close)

    def _on_never_clicked(self, *_):
        engine.decline()
        self.result = "declined"
        self._toast("不再自动提醒。以后要装可以重新打开向导")
        GLib.timeout_add(900, self.close)

    def _on_install_clicked(self, *_):
        if self.busy:
            return
        self.busy = True
        self.header.set_show_title_buttons(True)
        self.log_buffer.set_text("")
        self.progress_bar.set_fraction(0.0)
        self.stack.set_visible_child_name("progress")
        use_mirror = bool(self.mirror_check and self.mirror_check.get_active())
        engine.write_state(use_mirror=use_mirror)
        threading.Thread(target=self._install_worker, args=(use_mirror,),
                         daemon=True).start()

    # ------------------------------------------------------------------
    # worker thread
    # ------------------------------------------------------------------
    def _install_worker(self, use_mirror):
        try:
            self._set_progress(title="检查网络…", hint="", fraction=0.05)
            self._log_from_thread("=== 检查网络 ===")
            ok, detail = engine.check_network()
            for line in detail.splitlines():
                self._log_from_thread(line)
            if not ok:
                self._fail(
                    "网络不通，无法下载安装文件。\n\n%s\n\n"
                    "请确认网线或 Wi-Fi 已连接后点“重试”。" % detail)
                return
            self._set_progress(title="检查网络…", hint="网络正常", fraction=0.1)

            self._log_from_thread("")
            self._log_from_thread("=== 安装 Node.js 运行时与 pi ===")
            self._set_progress(title="下载并安装…",
                               hint="正在获取 Node.js 运行时", fraction=0.15)
            version = engine.install_pi(use_mirror=use_mirror,
                                        steps=self._engine_step)
            self._log_from_thread("")
            self._log_from_thread("=== 安装完成：pi %s ===" % version)
            GLib.idle_add(self._after_install, version)
        except engine.SetupError as exc:
            self._fail(str(exc))
        except Exception:                    # noqa: BLE001
            self._fail("安装过程中出现未预期的错误：\n\n%s"
                       % traceback.format_exc(limit=6))

    def _engine_step(self, text):
        """engine progress callback -> log line + coarse progress bar."""
        self._log_from_thread(text)
        if text.startswith("下载 node"):
            self._set_progress(hint=text)
        elif text.startswith("node 安装完成"):
            self._set_progress(hint=text, fraction=0.5)
        elif text.startswith("安装 "):
            self._set_progress(title="安装 pi…", hint=text, fraction=0.6)

    def _after_install(self, version):
        self.busy = False
        self.progress_bar.set_fraction(1.0)
        self.result = "install"
        self.done_description.set_text(
            "pi %s 已就绪（Node %s）。填一下 API 端点就能用了。"
            % (version, engine.node_version_for_pi() or "?"))
        self.stack.set_visible_child_name("config")
        self.key_row.entry.grab_focus()
        return False

    def _fail(self, message):
        self.busy = False
        self.failure_description.set_text(message)
        GLib.idle_add(self.stack.set_visible_child_name, "failure")
        GLib.idle_add(self.progress_bar.set_fraction, 0.0)

    # ------------------------------------------------------------------
    # config actions
    # ------------------------------------------------------------------
    def _current_api(self):
        index = self.api_row.get_selected()
        if index < 0 or index >= len(engine.API_TYPES):
            return engine.API_TYPES[0][0]
        return engine.API_TYPES[index][0]

    def _form(self):
        return (self.url_row.entry.get_text().strip(),
                self.key_row.entry.get_text().strip(),
                self.model_row.entry.get_text().strip(),
                self._current_api())

    def _on_test_clicked(self, *_):
        url, key, model, api = self._form()
        if not url or not key or not model:
            self.test_result.set_text("先把 API URL、API Key、模型 ID 都填上")
            return
        self.test_button.set_sensitive(False)
        self.test_result.set_text("正在测试连接…")

        # Same queue-and-poll handoff as the model probe, for the same reason.
        self._test_result = None

        def work():
            try:
                ok, detail = engine.test_endpoint(url, key, model, api)
            except Exception:                # noqa: BLE001
                ok, detail = False, traceback.format_exc(limit=4)
            self._test_result = (ok, detail)

        threading.Thread(target=work, daemon=True).start()
        self._test_started = time.monotonic()
        GLib.timeout_add(120, self._poll_test_result)

    def _poll_test_result(self):
        result = getattr(self, "_test_result", None)
        if result is None:
            if time.monotonic() - getattr(self, "_test_started", 0) > 60:
                self._show_test_result(False, "测试超时")
                return False
            return True
        self._show_test_result(*result)
        return False

    def _clear_placeholder_models(self):
        """Reset the dropdown to its 'nothing probed yet' state."""
        self.model_dropdown.set_model(Gtk.StringList.new(["（未探测）"]))
        self.model_dropdown.set_sensitive(False)
        self.models_found = []

    def _on_probe_clicked(self, *_):
        # Read the widgets HERE, on the main thread: GTK is not thread safe.
        url, key, _model, api = self._form()
        if not url or not key:
            self.test_result.set_text("探测模型需要先填 API URL 和 API Key")
            return
        self.probe_button.set_sensitive(False)
        self.probe_button.set_label("探测中…")
        self.test_result.set_text("正在查询模型列表…")

        # The request runs in a worker thread and hands its result back through
        # a queue that the main loop polls, instead of GLib.idle_add from the
        # worker. Both are legal, but idle_add from a thread that is itself
        # blocked in a socket read can be starved on a busy frame (seen under
        # Xvfb: list_models() returned in 2 ms in a plain thread and never
        # returned from inside this callback). A plain dict + timeout poll
        # keeps the UI responsive regardless of how the main loop is scheduled.
        self._probe_result = None
        holder = self._probe_result

        def work():
            box = {}
            try:
                models, detail = engine.list_models(url, key, api)
            except engine.SetupError as exc:
                box = {"models": [], "detail": str(exc)}
            except Exception:               # noqa: BLE001
                box = {"models": [], "detail": traceback.format_exc(limit=4)}
            else:
                box = {"models": models, "detail": detail}
            self._probe_result = box

        threading.Thread(target=work, daemon=True).start()
        self._probe_started = time.monotonic()
        GLib.timeout_add(120, self._poll_probe_result)

    def _poll_probe_result(self):
        result = getattr(self, "_probe_result", None)
        elapsed = time.monotonic() - getattr(self, "_probe_started", 0)
        if result is None:
            # 45 s ceiling: one GET per candidate URL with a 15 s timeout can
            # take ~45 s in the worst case, after which the button must come
            # back rather than stay greyed out forever.
            if elapsed > 45:
                self._show_probe_result([], "探测超时，请检查网络后重试，或直接手填模型 ID")
                return False
            self.test_result.set_text("正在查询模型列表…（%.0f 秒）" % elapsed)
            return True
        self._show_probe_result(result.get("models") or [], result.get("detail") or "")
        return False

    def _show_probe_result(self, models, detail):
        self.probe_button.set_sensitive(True)
        self.probe_button.set_label("探测模型")
        self.models_found = list(models or [])

        if not self.models_found:
            # Keep whatever the user typed: a failed probe must not break the
            # flow, since many gateways do not implement GET /models.
            self.model_dropdown.set_model(Gtk.StringList.new(["（未探测到）"]))
            self.model_dropdown.set_sensitive(False)
            self.test_result.set_text(
                "没能从端点拿到模型列表，请直接手填模型 ID。\n\n" + (detail or ""))
            return

        labels = ["%s%s" % (ident, "  ·  " + extra if extra else "")
                  for ident, extra in self.models_found]
        self.model_dropdown.set_model(Gtk.StringList.new(labels))
        self.model_dropdown.set_sensitive(True)

        # Select the entry that matches what is currently typed, so re-probing
        # does not silently switch the choice away from the user's own value.
        current = self.model_entry.get_text().strip()
        selected = 0
        for index, (ident, _extra) in enumerate(self.models_found):
            if ident == current:
                selected = index
                break
        else:
            # Nothing typed matches: adopt the first model the endpoint offers.
            # Choosing a model that exists beats keeping a guess that does not.
            self.model_entry.set_text(self.models_found[0][0])
        self.model_dropdown.set_selected(selected)

        self.test_result.set_text("探测到 %d 个模型，可在下拉框里选，也可以直接改下面的输入框。\n%s"
                                  % (len(self.models_found), detail or ""))

    def _on_model_selected(self, dropdown, _param):
        index = dropdown.get_selected()
        if 0 <= index < len(self.models_found):
            self.model_entry.set_text(self.models_found[index][0])

    def _show_test_result(self, ok, detail):
        self.test_button.set_sensitive(True)
        self.test_result.set_text(("✔ 连接正常\n" if ok else "✘ 连接失败\n") + detail)
        return False

    def _on_finish_clicked(self, *_):
        url, key, model, api = self._form()
        if not url or not key or not model:
            self.test_result.set_text("API URL、API Key、模型 ID 都不能为空")
            return
        labels = {value: label for value, label in engine.API_TYPES}
        try:
            engine.configure(url, key, model, api,
                             provider_name="Pi 端点（%s）" % labels.get(api, api))
        except engine.SetupError as exc:
            self.test_result.set_text("保存失败：%s" % exc)
            return
        except OSError as exc:
            self.test_result.set_text("写配置文件失败：%s" % exc)
            return

        engine.write_state(status="configured", snooze_until=0)
        self.result = "configured"
        self.header.set_show_title_buttons(False)
        base = url.rstrip("/")
        if base.endswith("/chat/completions"):
            base = base[: -len("/chat/completions")]
        self.done_description.set_text(
            "pi 已安装并配置好：\n默认模型 %s\n端点 %s" % (model, base))
        self.stack.set_visible_child_name("done")

    def _on_copy_clicked(self, *_):
        clipboard = Gdk.Display.get_default().get_clipboard()
        clipboard.set(HOW_TO_START)
        self._toast("启动步骤已复制到剪贴板")

    # ------------------------------------------------------------------
    def _on_close_request(self, *_):
        if self.busy:
            self._toast("安装仍在进行，完成前请不要关机")
        return False


CSS = """
.pi-setup-title { font-size: 22px; font-weight: 800; }
.pi-setup-field { font-size: 13px; font-weight: 700; opacity: 0.75; }
.pi-setup-howto { font-size: 13px; }
"""


class SetupApp(Gtk.Application):
    def __init__(self, start_page="welcome"):
        super().__init__(application_id=APP_ID,
                         flags=Gio.ApplicationFlags.NON_UNIQUE)
        self.window = None
        self.start_page = start_page

    def do_activate(self):
        if self.window is None:
            self.window = SetupWindow(self, start_page=self.start_page)
            self.window.present()
        else:
            self.window.present()


def is_real_user_session():
    """False for the display-manager greeter and for headless contexts.

    /etc/xdg/autostart is read by the GDM greeter session as well as by real
    logins, so without this check the wizard would appear on the login screen
    of a board nobody has logged into yet.
    """
    if os.environ.get("GNOME_SHELL_SESSION_MODE") == "gdm":
        return False
    if os.environ.get("RUNNING_UNDER_GDM") == "true":
        return False
    try:
        import pwd
        entry = pwd.getpwuid(os.getuid())
    except (ImportError, KeyError):
        return True
    # Debian uses 'Debian-gdm', Fedora/Ubuntu's gdm packaging uses 'gdm'.
    if entry.pw_name in ("Debian-gdm", "gdm"):
        return False
    return True


def main(argv):
    # The autostart entry runs on every login: stay silent unless there is
    # something to ask.
    if "--only-if-needed" in argv:
        if not is_real_user_session():
            return 0
        if not engine.should_prompt():
            return 0
        argv = [a for a in argv if a != "--only-if-needed"]
    if "--status" in argv:
        print(json.dumps(engine.status(), indent=2, ensure_ascii=False))
        return 0

    # --page <name> opens the wizard on a specific page. The launcher uses it
    # for the half-finished state (pi installed, API never filled in): asking
    # "install pi?" again there is wrong, the only thing missing is the
    # endpoint.
    start_page = "welcome"
    if "--page" in argv:
        index = argv.index("--page")
        if index + 1 < len(argv):
            start_page = argv[index + 1]
        del argv[max(0, index):index + 2]

    app = SetupApp(start_page=start_page)
    return app.run([sys.argv[0]] + [a for a in argv if a.startswith("-")])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
