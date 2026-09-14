#!/usr/bin/env python3
"""
TimeCapsule - QuecPi OTA 图形化管理工具

封装板上 /usr/sbin/ota_run 命令行工具,提供:
  - 快照/版本列表 (历史版本可视化)
  - 手动回退到指定历史版本 (ota_run rollback)
  - 通过文件选择升级脚本执行系统升级 (ota_run upgrade)
  - 手动从 @backup 还原 (ota_run restore)
  - 创建备份 (ota_run backup)
  - 首次初始化 (ota_run init)

后端基于 Btrfs 子卷快照的 A/B 分区升级方案,详见
/usr/sbin/ota_run 与 /opt/system_upgrade.sh。

GUI: GTK3 (与 qcom-npu-test 同技术栈,不引入额外依赖)。
"""

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Pango', '1.0')
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import Gtk, Gdk, GLib, Pango

import os
import re
import shutil
import subprocess
import threading
import datetime
import logging

# ========== 配置 ==========
OTA_RUN = "/usr/sbin/ota_run"
UPGRADE_SCRIPT = "/opt/system_upgrade.sh"
CURRENT_VOL_FILE = "/opt/current_vol"
HISTORY_VER_FILE = "/opt/history_ver"
RESTORE_FLAG = "/restore_flag"
FCT_FLAG = "/var/persist/fct_done_flag"
EFI_KERNEL = "/efi/EFI/Linux/linux-qcm6490-idp.efi"
KERNEL_UPGRADE_DIR = "/opt/kernel_upgrade"
KERNEL_UPGRADE_EFI = "/opt/kernel_upgrade/linux-qcm6490-idp.efi"
KERNEL_UPGRADE_MD5 = "/opt/kernel_upgrade/linux-qcm6490-idp.efi.md5"
# 板上当前内核的配套 md5 (ota 维护), 用于升级前对比
KERNEL_BAK_MD5 = "/opt/linux-qcm6490-idp.efi.md5"

LOG_DIR = "/tmp/quectel_ota_gui_logs"
APP_NAME = "TimeCapsule"
APP_TITLE = "时间胶囊 · QuecPi OTA 管理工具"


# ========== 工具函数 ==========
def is_root():
    return os.geteuid() == 0


def _pkexec_suid_ok():
    """pkexec 是否设置了 setuid root 位。

    设备镜像偶尔会把 /usr/bin/pkexec 装成 0755 (缺 setuid), 这会导致
    `pkexec must be setuid root` (rc=127)。检测到这里提示用户一键修复。
    """
    p = shutil.which("pkexec")
    if not p:
        return False
    try:
        st = os.stat(p)
    except OSError:
        return False
    # S_ISUID = 0o4000
    return bool(st.st_mode & 0o4000)


def fix_pkexec_suid():
    """若 pkexec 缺 setuid 位, 提示用户用 sudo 修复一次。

    gnome-initial-setup 创建的用户在 sudo 组, 可走一次交互式 sudo 补齐
    chmod u+s /usr/bin/pkexec; 之后 pkexec 正常弹 polkit 图形密码框。
    返回 (ok, message)。
    """
    p = shutil.which("pkexec")
    if not p:
        return False, "未找到 pkexec"
    if _pkexec_suid_ok():
        return True, "pkexec setuid 正常"
    # 需要 root 修权限位。用 sudo (交互式, 会要密码)。
    # 这里只构造命令, 由调用方在合适时机执行并提示用户。
    cmd = ["sudo", "chmod", "u+s", p]
    return False, (
        "pkexec 未设置 setuid root 位, 无法提权执行 OTA 操作。\n"
        f"请在终端执行一次修复命令 (会要求输入用户密码):\n"
        f"    {' '.join(cmd)}\n"
        "修复后 pkexec 将弹出图形密码框正常工作。")


def elevation_cmd(args):
    """构造可能需要提权的命令。

    策略: root 直执; 否则用 pkexec (polkit 会弹图形密码框, 适合 GUI)。
    要求 pkexec 带 setuid root 位 (_pkexec_suid_ok), 否则该命令会返回 127。
    调用方应在 GUI 启动时检测并提示用户修复 (fix_pkexec_suid)。
    sudo 不适用于无 tty 的 GUI 子进程, 故不使用。
    """
    if is_root():
        return list(args)
    if shutil.which("pkexec"):
        return ["pkexec"] + list(args)
    # 无 pkexec 时裸执行 (多半失败, 但 run_cmd 会把错误返回给用户)
    return list(args)


def run_cmd(cmd, timeout=None, cwd=None, env=None):
    """运行命令, 返回 (stdout+stderr, returncode)。"""
    log = logging.getLogger("ota_gui")
    log.debug(f"CMD: {' '.join(cmd)}")
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout, cwd=cwd, env=env)
        out = (r.stdout or "") + (r.stderr or "")
        return out, r.returncode
    except subprocess.TimeoutExpired:
        return f"ERROR: 命令超时({timeout}s)", -1
    except FileNotFoundError as e:
        return f"ERROR: 程序不存在: {e}", -1
    except Exception as e:
        return f"ERROR: {e}", -1


def run_ota(*ota_args, timeout=None):
    """运行 ota_run 子命令 (自动提权)。"""
    return run_cmd(elevation_cmd([OTA_RUN, *ota_args]), timeout=timeout)


def run_cmd_stream(cmd, timeout=None, cwd=None, env=None):
    """运行命令, 实时逐行把 stdout/stderr 打到日志, 返回 (完整输出, returncode)。

    run_cmd 用 subprocess.run(capture_output=True), 会把全部输出缓冲到进程结束才
    一次性返回。ota_run upgrade 这种命令会跑数分钟的 apt update/upgrade 并以 reboot
    结尾, 在此期间 UI 日志长期空白, 看起来像卡死 (实际正在升级)。本函数改用 Popen
    逐行读取并通过 logger 实时打印 (GTKLogHandler 经 GLib.idle_add 追加到日志窗口)。
    """
    log = logging.getLogger("ota_gui")
    log.info(f"CMD: {' '.join(cmd)}")
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd=cwd,
            env=env,
        )
    except FileNotFoundError as e:
        log.error(f"程序不存在: {e}")
        return f"ERROR: 程序不存在: {e}", -1
    except Exception as e:
        log.error(f"启动失败: {e}")
        return f"ERROR: {e}", -1

    out = []
    stream = proc.stdout
    if stream is None:
        log.error("子进程 stdout 不可读 (PIPE 未生效?)")
        return "ERROR: stdout 不可读", -1
    try:
        while True:
            line = stream.readline()
            if not line:
                break
            out.append(line)
            log.info(line.rstrip('\n'))
    except Exception as e:
        log.error(f"读取输出异常: {e}")
        out.append(f"ERROR: {e}")
    finally:
        try:
            stream.close()
        except Exception:
            pass

    try:
        rc = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        log.error(f"命令超时({timeout}s), 终止子进程")
        proc.kill()
        proc.wait()
        rc = -1

    return ''.join(out), rc


def run_ota_stream(*ota_args, timeout=None):
    """流式运行 ota_run 子命令 (自动提权), 逐行实时输出到日志。"""
    return run_cmd_stream(elevation_cmd([OTA_RUN, *ota_args]), timeout=timeout)


def upgrade_reached_success(out):
    """根据 ota_run upgrade 的输出判断升级是否真正跑到成功阶段。

    system_upgrade() 的 STEP 3 才执行 system_upgrade.sh (apt update/upgrade);
    跑到 STEP 4 及以后说明升级脚本已成功完成。后续 STEP 5-9 + reboot 会把子进程
    信号终止 (rc 为负), 不能单凭 rc 判失败, 必须看进度标记。
    """
    return any(m in out for m in (
        "[STEP 4]", "[STEP 5]", "[STEP 6]", "[STEP 7]", "[STEP 8]",
        "[STEP 9]", "Upgrade Finished", "Reboot now",
    ))


def read_file(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return ""


def read_md5_value(path):
    """读取 md5 文件, 只取摘要部分 (支持 'md5  filename' 和纯 md5 两种格式)。"""
    raw = read_file(path)
    if not raw:
        return ""
    return raw.split()[0]


def kernel_upgrade_ready():
    """/opt/kernel_upgrade 是否已有 efi+md5 待刷写。"""
    return os.path.isfile(KERNEL_UPGRADE_EFI) and os.path.isfile(KERNEL_UPGRADE_MD5)


def current_subvol_from_mount():
    """从 mount 信息解析当前根的 subvol。"""
    try:
        r = subprocess.run(["mount"], capture_output=True, text=True, timeout=5)
    except Exception:
        return ""
    for line in r.stdout.splitlines():
        if " on / " in line and "btrfs" in line:
            m = re.search(r'subvol=([^,)\s]+)', line)
            if m:
                return m.group(1)
    return ""


def parse_subvol_list(text):
    """解析 `btrfs subvolume list` 输出, 返回 [(id, gen, toplevel, path), ...]。"""
    items = []
    for line in text.splitlines():
        m = re.match(
            r'ID\s+(\d+)\s+gen\s+(\d+)(?:\s+top\s+level\s+(\d+))?\s+path\s+(.+)', line.strip())
        if m:
            items.append({
                "id": m.group(1),
                "gen": m.group(2),
                "toplevel": m.group(3) or "5",
                "path": m.group(4).strip(),
            })
    return items


def next_subvol_name(existing):
    """根据已有 @Vn 子卷, 推荐下一个名称。"""
    used = set()
    for s in existing:
        m = re.match(r'^@V(\d+)$', s)
        if m:
            used.add(int(m.group(1)))
    n = 0
    while n in used:
        n += 1
    return f"@V{n}"


def fmt_ts(dt=None):
    return (dt or datetime.datetime.now()).strftime("%Y-%m-%d %H:%M:%S")


# ========== 日志系统 ==========
class GTKLogHandler(logging.Handler):
    def __init__(self, textview):
        super().__init__()
        self.textview = textview

    def emit(self, record):
        GLib.idle_add(self._append, self.format(record))

    def _append(self, msg):
        buf = self.textview.get_buffer()
        buf.insert(buf.get_end_iter(), msg + "\n")
        mark = buf.create_mark(None, buf.get_end_iter(), False)
        self.textview.scroll_to_mark(mark, 0.0, False, 0.0, 1.0)
        return False


def setup_logger(textview):
    os.makedirs(LOG_DIR, exist_ok=True)
    logger = logging.getLogger("ota_gui")
    logger.setLevel(logging.DEBUG)
    logger.handlers.clear()

    gtk_h = GTKLogHandler(textview)
    gtk_h.setLevel(logging.INFO)
    gtk_h.setFormatter(logging.Formatter("%(asctime)s [%(levelname)s] %(message)s",
                                         datefmt="%H:%M:%S"))
    logger.addHandler(gtk_h)

    log_file = os.path.join(LOG_DIR,
                            f"quectel_ota_gui_{datetime.datetime.now():%Y%m%d_%H%M%S}.log")
    fh = logging.FileHandler(log_file, encoding='utf-8')
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(logging.Formatter("%(asctime)s [%(levelname)s] %(message)s"))
    logger.addHandler(fh)
    logger.log_file = log_file
    return logger


# ========== GUI ==========
class TimeCapsuleWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title=APP_TITLE)
        self.set_default_size(980, 700)
        self.set_position(Gtk.WindowPosition.CENTER)
        self.set_icon_name("ota-gui")

        # 运行状态
        self._busy = False
        self.subvol_rows = []
        self.current_running = ""
        self.current_configured = ""
        self.history_vol = ""
        self.btrfs_inited = False

        css = Gtk.CssProvider()
        css.load_from_data(b"""
            .title-label { font-size: 1.4em; font-weight: bold; }
            .danger { color: #c62828; }
            .ok { color: #2e7d32; }
            .warn { color: #ef6c00; }
        """)
        Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), css,
                                                 Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        vbox.set_margin_start(10); vbox.set_margin_end(10)
        vbox.set_margin_top(10); vbox.set_margin_bottom(10)
        self.add(vbox)

        # 标题
        title = Gtk.Label(xalign=0)
        title.set_markup(
            f'<span size="xx-large" weight="bold">⏳ {APP_NAME}</span>\n'
            f'<span size="small" foreground="gray">QuecPi (QCS6490) · '
            f'Btrfs 子卷快照 A/B 升级管理</span>')
        vbox.pack_start(title, False, False, 0)

        # 设备信息栏
        info_frame = Gtk.Frame(label="系统状态")
        info_grid = Gtk.Grid(column_spacing=14, row_spacing=3)
        info_grid.set_margin_start(8); info_grid.set_margin_end(8)
        info_grid.set_margin_top(5); info_grid.set_margin_bottom(5)
        info_frame.add(info_grid)
        vbox.pack_start(info_frame, False, False, 0)

        self.lbl_subvol = Gtk.Label(xalign=0)
        self.lbl_kernel = Gtk.Label(xalign=0)
        self.lbl_status = Gtk.Label(xalign=0)

        rows = [
            ("当前运行子卷:", self.lbl_subvol),
            ("内核文件:", self.lbl_kernel),
            ("OTA 状态:", self.lbl_status),
        ]
        for i, (k, v) in enumerate(rows):
            info_grid.attach(Gtk.Label(label=k, xalign=0), 0, i, 1, 1)
            info_grid.attach(v, 1, i, 1, 1)

        self.btn_refresh_top = Gtk.Button(label="🔄 刷新")
        self.btn_refresh_top.connect("clicked", lambda b: self.refresh_state())
        info_grid.attach(self.btn_refresh_top, 2, 0, 1, 3)

        # 主体: 左边操作 + 右边版本列表
        body = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        vbox.pack_start(body, True, True, 0)

        # 左侧操作面板
        actions = self._build_action_panel()
        body.pack_start(actions, False, False, 0)

        # 右侧版本列表
        list_box = self._build_version_list()
        body.pack_start(list_box, True, True, 0)

        # 日志面板
        log_frame = Gtk.Frame(label="运行日志")
        log_hbox = Gtk.Box(spacing=4)
        log_hbox.set_margin_start(4); log_hbox.set_margin_end(4)
        log_hbox.set_margin_top(2); log_hbox.set_margin_bottom(2)
        log_frame.add(log_hbox)

        log_sw = Gtk.ScrolledWindow()
        log_sw.set_size_request(-1, 130)
        log_sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        self.log_tv = Gtk.TextView()
        self.log_tv.set_editable(False)
        self.log_tv.set_monospace(True)
        self.log_tv.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.log_tv.modify_font(Pango.FontDescription("monospace 9"))
        log_sw.add(self.log_tv)
        log_hbox.pack_start(log_sw, True, True, 0)

        log_btns = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        b = Gtk.Button(label="清空"); b.set_size_request(56, -1)
        b.connect("clicked", lambda x: self.log_tv.get_buffer().set_text(""))
        log_btns.pack_start(b, False, False, 0)
        b = Gtk.Button(label="保存"); b.set_size_request(56, -1)
        b.connect("clicked", self.on_save_log)
        log_btns.pack_start(b, False, False, 0)
        log_hbox.pack_start(log_btns, False, False, 0)
        vbox.pack_start(log_frame, False, False, 0)

        # 状态栏
        self.statusbar = Gtk.Label(xalign=0)
        self.statusbar.set_markup('<span foreground="gray">就绪</span>')
        vbox.pack_start(self.statusbar, False, False, 0)

        self.logger = setup_logger(self.log_tv)
        self.logger.info(f"{APP_NAME} 启动")
        self.logger.info(f"日志文件: {self.logger.log_file}")
        self.logger.info(f"root 权限: {'是' if is_root() else '否 (将用 pkexec 提权)'}")

        # pkexec setuid 位检测: 缺失则提权会 127, 提示用户一键修复
        if not is_root() and not _pkexec_suid_ok():
            ok, msg = fix_pkexec_suid()
            self.logger.warning(msg.replace("\n", " "))
            GLib.idle_add(lambda: self._warn_pkexec_suid(msg))
        else:
            self.logger.info("pkexec setuid 位正常")

        # 初始加载
        self.refresh_state()

    def _warn_pkexec_suid(self, msg):
        d = Gtk.MessageDialog(transient_for=self, modal=True,
                              message_type=Gtk.MessageType.WARNING,
                              buttons=Gtk.ButtonsType.OK,
                              text="pkexec 提权不可用")
        d.format_secondary_text(msg)
        d.run()
        d.destroy()
        return False

    # ---------- 操作面板 ----------
    def _build_action_panel(self):
        frame = Gtk.Frame(label="操作")
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        outer.set_margin_start(8); outer.set_margin_end(8)
        outer.set_margin_top(8); outer.set_margin_bottom(8)
        frame.add(outer)

        # ---- 初始化 ----
        box_init = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        lbl = Gtk.Label(xalign=0)
        lbl.set_markup("<b>首次初始化</b>")
        box_init.pack_start(lbl, False, False, 0)
        desc = Gtk.Label(xalign=0, wrap=True)
        desc.set_markup("<span size='small' foreground='gray'>"
                        "创建初始 @V0 快照并备份内核, 之后才能使用升级/回退</span>")
        box_init.pack_start(desc, False, False, 0)
        self.btn_init = Gtk.Button(label="⚡ 初始化 OTA (init)")
        self.btn_init.get_style_context().add_class("suggested-action")
        self.btn_init.connect("clicked", self.on_init_clicked)
        box_init.pack_start(self.btn_init, False, False, 0)
        outer.pack_start(box_init, False, False, 0)

        outer.pack_start(Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL),
                         False, False, 0)

        # ---- 备份 ----
        box_bk = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        lbl = Gtk.Label(xalign=0); lbl.set_markup("<b>备份</b>")
        box_bk.pack_start(lbl, False, False, 0)
        desc = Gtk.Label(xalign=0, wrap=True)
        desc.set_markup("<span size='small' foreground='gray'>"
                        "将当前系统快照为 @backup, 升级前建议先备份</span>")
        box_bk.pack_start(desc, False, False, 0)
        self.btn_backup = Gtk.Button(label="🛡️ 创建备份 (backup)")
        self.btn_backup.connect("clicked", self.on_backup_clicked)
        box_bk.pack_start(self.btn_backup, False, False, 0)
        outer.pack_start(box_bk, False, False, 0)

        outer.pack_start(Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL),
                         False, False, 0)

        # ---- 升级 ----
        box_up = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        lbl = Gtk.Label(xalign=0); lbl.set_markup("<b>系统升级</b>")
        box_up.pack_start(lbl, False, False, 0)

        row = Gtk.Box(spacing=6)
        row.pack_start(Gtk.Label(label="升级脚本:"), False, False, 0)
        self.ent_script = Gtk.Entry()
        self.ent_script.set_text(UPGRADE_SCRIPT)
        self.ent_script.set_tooltip_text("选择本地升级脚本, 将替换 /opt/system_upgrade.sh 后执行升级")
        row.pack_start(self.ent_script, True, True, 0)
        self.btn_pick = Gtk.Button(label="浏览...")
        self.btn_pick.connect("clicked", self.on_pick_script)
        row.pack_start(self.btn_pick, False, False, 0)
        box_up.pack_start(row, False, False, 0)

        # 内核 EFI 文件 (可选): ota_run upgrade 会自动检测 /opt/kernel_upgrade/
        # 的 efi + md5 并刷写 EFI 分区。本工具负责生成配套 md5 并放入该目录。
        row_k = Gtk.Box(spacing=6)
        row_k.pack_start(Gtk.Label(label="内核 EFI:"), False, False, 0)
        self.ent_kernel = Gtk.Entry()
        self.ent_kernel.set_tooltip_text(
            "可选: 选择新内核 .efi 文件, 升级时自动计算 MD5 并放入 "
            "/opt/kernel_upgrade/, ota_run 会在校验后刷写 EFI 分区")
        row_k.pack_start(self.ent_kernel, True, True, 0)
        self.btn_pick_kernel = Gtk.Button(label="浏览...")
        self.btn_pick_kernel.connect("clicked", self.on_pick_kernel)
        row_k.pack_start(self.btn_pick_kernel, False, False, 0)
        self.btn_kernel_md5 = Gtk.Button(label="生成 MD5")
        self.btn_kernel_md5.set_tooltip_text(
            "计算所选 EFI 文件的 MD5 并显示, 校验通过后写入 "
            "/opt/kernel_upgrade/linux-qcm6490-idp.efi(.md5)")
        self.btn_kernel_md5.connect("clicked", self.on_compute_kernel_md5)
        row_k.pack_start(self.btn_kernel_md5, False, False, 0)
        box_up.pack_start(row_k, False, False, 0)
        self.lbl_kernel_md5 = Gtk.Label(xalign=0)
        self.lbl_kernel_md5.set_markup(
            "<span size='small' foreground='gray'>未选内核 (将沿用当前内核)</span>")
        box_up.pack_start(self.lbl_kernel_md5, False, False, 0)

        row2 = Gtk.Box(spacing=6)
        row2.pack_start(Gtk.Label(label="新版本名:"), False, False, 0)
        self.ent_newvol = Gtk.Entry()
        self.ent_newvol.set_placeholder_text("@V1")
        self.ent_newvol.set_tooltip_text("新子卷名, 以 @ 开头, 如 @V1")
        row2.pack_start(self.ent_newvol, True, True, 0)
        box_up.pack_start(row2, False, False, 0)

        self.chk_autobackup = Gtk.CheckButton(label="升级前自动创建备份 (推荐)")
        self.chk_autobackup.set_active(True)
        box_up.pack_start(self.chk_autobackup, False, False, 0)

        self.btn_preview = Gtk.Button(label="👁 预览脚本")
        self.btn_preview.connect("clicked", self.on_preview_script)
        box_up.pack_start(self.btn_preview, False, False, 0)

        self.btn_upgrade = Gtk.Button(label="⬆ 执行升级 (升级后重启)")
        self.btn_upgrade.get_style_context().add_class("suggested-action")
        self.btn_upgrade.connect("clicked", self.on_upgrade_clicked)
        box_up.pack_start(self.btn_upgrade, False, False, 0)
        outer.pack_start(box_up, False, False, 0)

        outer.pack_start(Gtk.Separator(orientation=Gtk.Orientation.HORIZONTAL),
                         False, False, 0)

        # ---- 恢复 ----
        box_rs = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        lbl = Gtk.Label(xalign=0); lbl.set_markup("<b>从备份还原</b>")
        box_rs.pack_start(lbl, False, False, 0)
        desc = Gtk.Label(xalign=0, wrap=True)
        desc.set_markup("<span size='small' foreground='gray'>"
                        "从 @backup 子卷还原系统并重启 (ota_run restore)</span>")
        box_rs.pack_start(desc, False, False, 0)
        self.btn_restore = Gtk.Button(label="♻️ 还原备份 (restore)")
        self.btn_restore.connect("clicked", self.on_restore_clicked)
        self.btn_restore.get_style_context().add_class("destructive-action")
        box_rs.pack_start(self.btn_restore, False, False, 0)
        outer.pack_start(box_rs, False, False, 0)

        return frame

    # ---------- 版本列表 ----------
    def _build_version_list(self):
        frame = Gtk.Frame(label="版本快照列表")
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        box.set_margin_start(6); box.set_margin_end(6)
        box.set_margin_top(4); box.set_margin_bottom(4)
        frame.add(box)

        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        self.liststore = Gtk.ListStore(str, str, str, str, str)
        # 列: 子卷名, 类型, ID, Gen, 状态图标
        self.treeview = Gtk.TreeView(model=self.liststore)
        self.treeview.get_selection().set_mode(Gtk.SelectionMode.SINGLE)

        def col(title, idx, w):
            rend = Gtk.CellRendererText()
            c = Gtk.TreeViewColumn(title, rend, text=idx)
            c.set_min_width(w)
            self.treeview.append_column(c)

        col("子卷", 0, 120)
        col("类型", 1, 90)
        col("ID", 2, 50)
        col("Gen", 3, 50)
        col("状态", 4, 110)
        sw.add(self.treeview)
        box.pack_start(sw, True, True, 0)

        btnbox = Gtk.Box(spacing=6)
        self.btn_rollback = Gtk.Button(label="⏪ 回退到选中版本")
        self.btn_rollback.connect("clicked", self.on_rollback_clicked)
        btnbox.pack_start(self.btn_rollback, False, False, 0)
        self.btn_delete = Gtk.Button(label="🗑 删除选中版本")
        self.btn_delete.connect("clicked", self.on_delete_clicked)
        btnbox.pack_start(self.btn_delete, False, False, 0)
        self.btn_refresh_list = Gtk.Button(label="🔄 刷新列表")
        self.btn_refresh_list.connect("clicked", lambda b: self.refresh_state())
        btnbox.pack_end(self.btn_refresh_list, False, False, 0)
        box.pack_start(btnbox, False, False, 0)
        return frame

    # ---------- 状态刷新 ----------
    def refresh_state(self):
        def _load():
            # 当前运行 subvol
            self.current_running = current_subvol_from_mount()
            self.current_configured = read_file(CURRENT_VOL_FILE)
            self.history_vol = read_file(HISTORY_VER_FILE)

            # 内核
            kern = ""
            if os.path.exists(EFI_KERNEL):
                try:
                    r = subprocess.run(["md5sum", EFI_KERNEL],
                                       capture_output=True, text=True, timeout=10)
                    kern = (r.stdout.split()[0] if r.stdout else "?")[:12]
                except Exception:
                    kern = "?"
            else:
                kern = "未找到"

            # btrfs 是否已初始化
            out, rc = run_ota("current", timeout=15)
            self.btrfs_inited = (rc == 0 and "not initialized" not in out.lower())

            # 子卷列表
            list_out, _ = run_ota("list", timeout=20)
            self.subvol_rows = parse_subvol_list(list_out) if list_out else []

            # 状态
            if not os.path.exists(FCT_FLAG):
                status = "⚠️ 工厂初始化未完成 (缺 fct_done_flag)"
            elif os.path.exists(RESTORE_FLAG):
                status = "⚠️ restore_flag 存在 (升级可能中断, 下次启动将回滚)"
            elif not self.btrfs_inited:
                status = "⚠️ OTA 未初始化, 请先执行 [初始化]"
            else:
                status = "✅ OTA 就绪"

            GLib.idle_add(self._apply_state, kern, status)
            GLib.idle_add(self._populate_list)
            GLib.idle_add(self._suggest_new_vol)
            self.logger.info(f"状态: {status}")
        threading.Thread(target=_load, daemon=True).start()

    def _apply_state(self, kern, status):
        inited = self.btrfs_inited
        sub = self.current_running or ("(顶层 rootfs)" if not inited else "未知")
        hist = self.history_vol or "无"
        self.lbl_subvol.set_text(f"{sub}    [history: {hist}]")
        self.lbl_kernel.set_text(f"{EFI_KERNEL}  md5:{kern}")

        if "✅" in status:
            self.lbl_status.set_markup(f'<span foreground="green">{status}</span>')
        elif "⚠️" in status:
            self.lbl_status.set_markup(f'<span foreground="#ef6c00">{status}</span>')
        else:
            self.lbl_status.set_text(status)

        # 按钮可用性
        ready = inited
        for b in (self.btn_backup, self.btn_upgrade, self.btn_restore,
                  self.btn_preview, self.btn_pick, self.btn_pick_kernel,
                  self.btn_kernel_md5):
            b.set_sensitive(ready)
        self.btn_init.set_sensitive(not inited)
        self.btn_rollback.set_sensitive(inited)
        self.btn_delete.set_sensitive(inited)
        return False

    def _populate_list(self):
        self.liststore.clear()
        if not self.subvol_rows:
            self.liststore.append(["(无子卷)", "-", "-", "-", "—"])
            return False

        cur = (self.current_running or "").lstrip("/")
        hist = self.history_vol

        for r in self.subvol_rows:
            path = r["path"]
            # 类型判定: 只认实际运行子卷 (mount 的 subvol=, 即 ota_run current)。
            # 不读 current_vol 文件: 它存在子卷内部, 被快照冻结后会读到过期值,
            # 升级中途 (STEP6 已写 current_vol 但要等 reboot 才生效) 会和实际
            # mount 不一致, 用来标"启动项"会误导用户。启动配置不在列表展示。
            if path == "@backup":
                typ, icon = "备份", "🛡️ 备份"
            elif path == cur and cur:
                typ, icon = "当前运行", "✅ 当前"
            elif path == hist:
                typ, icon = "上一历史", "📦 历史"
            else:
                typ, icon = "历史版本", "🗂️ 快照"
            self.liststore.append([path, typ, r["id"], r["gen"], icon])
        return False

    def _suggest_new_vol(self):
        if not self.ent_newvol.get_text():
            existing = [r["path"] for r in self.subvol_rows]
            self.ent_newvol.set_text(next_subvol_name(existing))
        return False

    # ---------- 选中项辅助 ----------
    def get_selected_subvol(self):
        sel = self.treeview.get_selection()
        model, it = sel.get_selected()
        if it is None:
            return None
        return model.get_value(it, 0)

    # ---------- 按钮回调 ----------
    def on_pick_script(self, button):
        dlg = Gtk.FileChooserDialog(
            title="选择升级脚本", parent=self, action=Gtk.FileChooserAction.OPEN)
        dlg.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                        Gtk.STOCK_OPEN, Gtk.ResponseType.OK)
        filt = Gtk.FileFilter()
        filt.set_name("脚本文件 (*.sh)")
        filt.add_pattern("*.sh")
        dlg.add_filter(filt)
        filt2 = Gtk.FileFilter()
        filt2.set_name("所有文件")
        filt2.add_pattern("*")
        dlg.add_filter(filt2)
        if os.path.exists(self.ent_script.get_text()):
            dlg.set_filename(self.ent_script.get_text())
        if dlg.run() == Gtk.ResponseType.OK:
            self.ent_script.set_text(dlg.get_filename())
        dlg.destroy()

    def on_preview_script(self, button):
        path = self.ent_script.get_text().strip()
        if not path or not os.path.isfile(path):
            self.show_error(f"脚本文件不存在: {path}")
            return
        dlg = Gtk.Dialog(title="升级脚本预览", transient_for=self,
                         modal=True)
        dlg.add_buttons(Gtk.STOCK_CLOSE, Gtk.ResponseType.OK)
        dlg.set_default_size(680, 460)
        sw = Gtk.ScrolledWindow()
        tv = Gtk.TextView()
        tv.set_editable(False); tv.set_monospace(True)
        tv.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        tv.modify_font(Pango.FontDescription("monospace 9"))
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                txt = f.read()
        except OSError as e:
            txt = f"读取失败: {e}"
        tv.get_buffer().set_text(txt)
        sw.add(tv)
        dlg.get_content_area().pack_start(sw, True, True, 0)
        dlg.show_all()
        dlg.run()
        dlg.destroy()

    # ---- 内核 EFI 升级 ----
    def on_pick_kernel(self, button):
        dlg = Gtk.FileChooserDialog(
            title="选择内核 EFI 文件", parent=self, action=Gtk.FileChooserAction.OPEN)
        dlg.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                        Gtk.STOCK_OPEN, Gtk.ResponseType.OK)
        filt = Gtk.FileFilter()
        filt.set_name("EFI 内核 (*.efi)")
        filt.add_pattern("*.efi")
        dlg.add_filter(filt)
        filt2 = Gtk.FileFilter()
        filt2.set_name("所有文件")
        filt2.add_pattern("*")
        dlg.add_filter(filt2)
        cur = self.ent_kernel.get_text().strip()
        if cur and os.path.isfile(cur):
            dlg.set_filename(cur)
        if dlg.run() == Gtk.ResponseType.OK:
            self.ent_kernel.set_text(dlg.get_filename())
            self.lbl_kernel_md5.set_markup(
                "<span size='small' foreground='gray'>文件已选定, 点击 [生成 MD5] 校验并写入</span>")
        dlg.destroy()

    def _md5_of_file(self, path):
        """计算文件 MD5, 返回十六进制摘要。"""
        import hashlib
        h = hashlib.md5()
        with open(path, "rb") as f:
            while True:
                chunk = f.read(1 << 20)
                if not chunk:
                    break
                h.update(chunk)
        return h.hexdigest()

    def on_compute_kernel_md5(self, button):
        """计算所选 EFI 的 MD5, 校验大小, 写入 /opt/kernel_upgrade/。

        ota_run upgrade STEP 4 要求 /opt/kernel_upgrade/ 同时存在
        linux-qcm6490-idp.efi 和 linux-qcm6490-idp.efi.md5,
        且 md5sum -c 通过, 才会刷写 EFI 分区。本方法生成这两个文件。
        """
        path = self.ent_kernel.get_text().strip()
        if not path:
            self.show_error("请先选择内核 EFI 文件")
            return
        if not os.path.isfile(path):
            self.show_error(f"内核文件不存在: {path}")
            return

        # 基本健全性检查: EFI 内核镜像不该太小
        try:
            size = os.path.getsize(path)
        except OSError as e:
            self.show_error(f"读取文件大小失败: {e}")
            return
        if size < 1024 * 1024:
            if not self._confirm_action(
                    "内核文件偏小",
                    f"文件大小仅 {size} 字节, 通常内核 EFI 远大于此。\n仍要继续吗?"):
                return

        self.btn_kernel_md5.set_sensitive(False)
        self.lbl_kernel_md5.set_markup("<span size='small'>⏳ 计算 MD5 中...</span>")

        def work():
            try:
                md5 = self._md5_of_file(path)
            except OSError as e:
                GLib.idle_add(self._kernel_md5_fail, f"计算 MD5 失败: {e}",
                              [self.btn_kernel_md5])
                return
            self.logger.info(f"内核 EFI: {path}  size={size}  md5={md5}")

            # 写入 /opt/kernel_upgrade/ (自动提权)
            self._install_kernel_files(path, md5)
        self._run_task(work,
                       lambda ok, e: self._kernel_md5_done(ok, e),
                       [self.btn_kernel_md5])

    def _install_kernel_files(self, efi_path, md5_unused=None):
        """调用 ota_kernel_install 把 efi + 自动 MD5 放入 /opt/kernel_upgrade/ 并自检。

        提权由 elevation_cmd (sudo -n /usr/sbin/ota_kernel_install) 统一处理,
        脚本内部完成 mkdir/cp/md5 生成/chmod/md5sum -c, GUI 不再直接碰 /opt。
        md5_unused 保留形参仅为兼容旧调用签名, 实际 md5 由脚本自身计算 (避免
        GUI 进程与 root 脚本算出不一致)。
        """
        SCRIPT = "/usr/sbin/ota_kernel_install"
        if not os.path.exists(SCRIPT):
            self._task_err = f"缺少内核安装助手: {SCRIPT}\n请确认 quectel-ota 已安装"
            self.logger.error(self._task_err)
            return
        if not os.path.isfile(efi_path):
            self._task_err = f"内核文件不存在: {efi_path}"
            self.logger.error(self._task_err)
            return
        self.logger.info(f"调用 {SCRIPT} 安装内核 {efi_path}")
        out, rc = run_cmd(elevation_cmd([SCRIPT, efi_path]), timeout=180)
        self.logger.info(f"ota_kernel_install rc={rc}\n{out}")
        if rc != 0:
            self._task_err = f"内核安装失败 (rc={rc}):\n{out}"
            self.logger.error(self._task_err)
            return
        # 脚本已完成 md5sum -c 自检, 这里只读取结果用于显示
        self.logger.info("内核升级文件已就绪 (脚本自检通过)")


    def _kernel_md5_fail(self, msg, widgets):
        for w in widgets:
            w.set_sensitive(True)
        self.show_error(msg)
        return False

    def _kernel_md5_done(self, ok, err):
        if err:
            self.show_error(err)
            self.lbl_kernel_md5.set_markup(
                "<span size='small' foreground='#c62828'>❌ 内核准备失败</span>")
            return
        md5 = read_md5_value(KERNEL_UPGRADE_MD5)
        self.lbl_kernel_md5.set_markup(
            f"<span size='small' foreground='green'>✅ 内核已就绪 md5:{md5[:12]} "
            f"(升级时自动刷写 EFI)</span>")
        self.statusbar.set_markup(
            '<span foreground="green">✅ 内核升级文件已就绪, 升级时将一并刷写</span>')
        self.logger.info("内核升级文件已就绪, 等待升级触发")

    # ----- 通用任务执行器 -----
    def _run_task(self, do, done_cb=None, busy_widgets=None):
        if self._busy:
            self.logger.warning("已有任务在运行, 请等待完成")
            return
        self._busy = True
        # 每次任务开始前清空错误标记: work() 失败时会重新设置它
        self._task_err = None
        widgets = busy_widgets or []
        for w in widgets:
            w.set_sensitive(False)
        def run():
            err = None
            try:
                do()
            except Exception as e:
                self.logger.error(f"任务异常: {e}")
                err = str(e)
            else:
                # work() 通过设置 self._task_err 报告命令失败 (rc != 0 等)
                if getattr(self, "_task_err", None):
                    err = self._task_err
                    self.logger.error(f"任务失败: {err}")
            GLib.idle_add(self._task_finish, widgets, done_cb, err is None, err)
        threading.Thread(target=run, daemon=True).start()

    def _task_finish(self, widgets, done_cb, ok, err):
        self._busy = False
        for w in widgets:
            w.set_sensitive(True)
        if done_cb:
            done_cb(ok, err)
        return False

    # ----- 初始化 -----
    def on_init_clicked(self, button):
        if not self._confirm("初始化 OTA",
                            "将备份当前内核并创建初始 @V0 快照, 完成后自动重启。\n\n继续?"):
            return
        self.logger.info("开始 OTA 初始化...")

        def work():
            out, rc = run_ota_stream("init", timeout=120)
            self.logger.info(f"init 返回码={rc}")
            if rc != 0:
                self._task_err = f"初始化失败:\n{out}"
        self._run_task(work, lambda ok, e: self._init_done(ok, e), [self.btn_init])

    def _init_done(self, ok, err):
        if err:
            self.show_error(err)
            self.refresh_state()
            return
        self.logger.info("OTA 初始化成功, 设备将重启")
        self.statusbar.set_markup('<span foreground="green">✅ 初始化完成, 设备将重启</span>')
        self.show_info("OTA 初始化成功!\n设备将自动重启进入子卷模式, 重启后重新打开本工具即可使用升级/回退。")
        GLib.timeout_add_seconds(3, self.refresh_state)

    # ----- 备份 -----
    def on_backup_clicked(self, button):
        if not self._confirm("创建备份",
                            "将备份内核并把当前 @Vn 快照为 @backup (覆盖旧备份)。\n\n继续?"):
            return
        self.logger.info("开始创建备份...")

        def work():
            out, rc = run_ota_stream("backup", timeout=180)
            self.logger.info(f"backup 返回码={rc}")
            if rc != 0:
                self._task_err = f"备份失败:\n{out}"
        self._run_task(work, lambda ok, e: self._backup_done(ok, e), [self.btn_backup])

    def _backup_done(self, ok, err):
        self.refresh_state()
        if err:
            self.show_error(err)
        else:
            self.statusbar.set_markup('<span foreground="green">✅ 备份创建成功</span>')
            self.logger.info("备份创建成功")

    # ----- 升级 -----
    def on_upgrade_clicked(self, button):
        script = self.ent_script.get_text().strip()
        newvol = self.ent_newvol.get_text().strip()
        kernel = self.ent_kernel.get_text().strip()
        if not newvol:
            self.show_error("请填写新版本名 (如 @V2)")
            return
        if not newvol.startswith("@"):
            newvol = "@" + newvol
            self.ent_newvol.set_text(newvol)
        if not re.match(r'^@[a-zA-Z0-9._-]+$', newvol):
            self.show_error("新版本名非法, 需以 @ 开头, 仅含字母/数字/._-")
            return
        if script and not os.path.isfile(script):
            self.show_error(f"升级脚本不存在: {script}")
            return
        if newvol == "@backup" or newvol.startswith("@backup"):
            self.show_error("不能用 @backup 作为新版本名")
            return
        if kernel and not os.path.isfile(kernel):
            self.show_error(f"内核 EFI 文件不存在: {kernel}")
            return

        # 内核摘要: 选了文件取计算值, 没选且板上已就绪取板上值
        if kernel:
            try:
                _ksize = os.path.getsize(kernel)
                _kmd5 = self._md5_of_file(kernel)
            except OSError as e:
                self.show_error(f"读取内核文件失败: {e}")
                return
            k_summary = f"<b>升级内核:</b> 是 ({os.path.basename(kernel)}, {_ksize}B, md5:{_kmd5[:12]})\n"
        elif kernel_upgrade_ready():
            k_summary = f"<b>升级内核:</b> 是 (/opt/kernel_upgrade 已就绪, md5:{read_md5_value(KERNEL_UPGRADE_MD5)[:12]})\n"
        else:
            k_summary = "<b>升级内核:</b> 否 (沿用当前内核)\n"

        warn = (
            "<b>系统升级确认</b>\n\n"
            f"新版本子卷: <b>{newvol}</b>\n"
            f"升级脚本: <tt>{script or UPGRADE_SCRIPT+' (板上现有)'}</tt>\n"
            f"{k_summary}"
            f"自动备份: <b>{'是' if self.chk_autobackup.get_active() else '否'}</b>\n\n"
            "<span foreground='#c62828'>升级完成后设备将自动重启!</span>\n"
            "如果升级脚本失败, 系统会自动回滚到 @backup。\n\n继续?"
        )
        if not self._confirm_rich("系统升级", warn):
            return

        autob = self.chk_autobackup.get_active()
        self.logger.info(f"开始系统升级 -> {newvol}, 脚本={script}, "
                         f"内核={'是' if kernel else '否'}, 自动备份={autob}")

        def work():
            # 0) 若选了内核 EFI, 先计算 MD5 写入 /opt/kernel_upgrade/
            #    (用户可能直接点[执行升级]而没先点[生成 MD5], 这里兜底)
            if kernel:
                self.logger.info("准备内核升级文件...")
                self._install_kernel_files(kernel, self._md5_of_file(kernel))
                if getattr(self, "_task_err", None):
                    return  # _install_kernel_files 已设 _task_err

            # 1) 若用户选了新脚本, 替换 /opt/system_upgrade.sh
            #    写 /opt 需要 root, 统一走 ota_kernel_install install-script (sudo 白名单)
            if script and script != UPGRADE_SCRIPT:
                self.logger.info(f"替换升级脚本 {script} -> {UPGRADE_SCRIPT}")
                out, rc = run_cmd(
                    elevation_cmd(["/usr/sbin/ota_kernel_install", "install-script", script]),
                    timeout=30)
                self.logger.info(f"install-script rc={rc}\n{out}")
                if rc != 0:
                    self._task_err = f"升级脚本替换失败 (rc={rc}):\n{out}"
                    return

            # 2) 自动备份 (流式输出: 备份耗时几十秒, 逐行打印避免日志空白)
            if autob:
                self.logger.info("升级前自动创建备份...")
                out, rc = run_ota_stream("backup", timeout=180)
                if rc != 0:
                    self._task_err = f"升级前备份失败, 已中止升级:\n{out}"
                    return

            # 3) 执行升级 (流式输出!) — ota_run upgrade 会运行 system_upgrade.sh
            #    (apt update/upgrade), 耗时数分钟, 并以 reboot 结尾把设备重启。
            #    必须逐行流式打印, 否则 subprocess 缓冲会让日志窗口长时间空白,
            #    看起来像卡死 (实际正在升级)。
            out, rc = run_ota_stream("upgrade", newvol, timeout=1800)
            # 成功路径 ota_run 会 reboot, 子进程被信号终止 -> rc 可能为负;
            # 用进度标记判断: 跑到 STEP 4+ 说明升级脚本已成功, 不算失败。
            if upgrade_reached_success(out):
                self.logger.info(f"upgrade 升级流程已完成 (rc={rc}), 设备将重启")
                return
            if rc != 0:
                self._task_err = f"升级失败 (rc={rc}):\n{out}"
        self._run_task(work, lambda ok, e: self._upgrade_done(ok, e),
                       [self.btn_upgrade, self.btn_backup, self.btn_init])

    def _upgrade_done(self, ok, err):
        if err:
            # 升级失败时 ota_run 通常会自己 reboot 回滚, 但若没 reboot 则刷新状态
            self.show_error(err)
            self.statusbar.set_markup('<span foreground="#c62828">❌ 升级失败</span>')
            GLib.timeout_add_seconds(5, self.refresh_state)
        else:
            self.statusbar.set_markup('<span foreground="green">✅ 升级成功, 设备将重启</span>')
            self.show_info("系统升级成功!\n设备将自动重启进入新版本。重启后重新打开本工具验证。")

    # ----- 回退 -----
    def on_rollback_clicked(self, button):
        target = self.get_selected_subvol()
        if not target:
            self.show_error("请先在列表中选择要回退的版本")
            return
        if target == "@backup":
            self.show_error("不能回退到 @backup, 请使用 [还原备份] 按钮")
            return
        # 只认实际运行子卷 (mount 的 subvol=), 不读子卷内冻结的 current_vol
        cur_running = (self.current_running or "").lstrip("/")
        if target == cur_running:
            self.show_error(f"{target} 正是当前运行版本, 无需回退")
            return
        warn = (
            "<b>版本回退确认</b>\n\n"
            f"目标版本: <b>{target}</b>\n"
            "将恢复该版本的内核并把启动配置切换到它。\n\n"
            "<span foreground='#c62828'>回退完成后设备将自动重启!</span>\n\n继续?"
        )
        if not self._confirm_rich("版本回退", warn):
            return
        self.logger.info(f"开始回退到 {target}")

        def work():
            out, rc = run_ota_stream("rollback", target, timeout=300)
            self.logger.info(f"rollback 返回码={rc}")
            if rc != 0:
                self._task_err = f"回退失败:\n{out}"
        self._run_task(work, lambda ok, e: self._rollback_done(ok, e),
                       [self.btn_rollback])

    def _rollback_done(self, ok, err):
        if err:
            self.show_error(err)
            self.statusbar.set_markup('<span foreground="#c62828">❌ 回退失败</span>')
            self.refresh_state()
        else:
            self.statusbar.set_markup('<span foreground="green">✅ 回退成功, 设备将重启</span>')
            self.show_info("版本回退成功!\n设备将自动重启进入所选版本。")

    # ----- 删除 -----
    def on_delete_clicked(self, button):
        target = self.get_selected_subvol()
        if not target:
            self.show_error("请先在列表中选择要删除的版本")
            return
        if target == "@backup":
            self.show_error("不能删除 @backup 子卷")
            return
        # 只认实际运行子卷 (mount 的 subvol=), 不读子卷内冻结的 current_vol
        if target == (self.current_running or "").lstrip("/"):
            self.show_error("不能删除当前运行版本")
            return
        if not self._confirm_action(f"删除子卷 {target}",
                                   f"确定删除子卷 {target} ?\n\n此操作不可恢复!"):
            return
        self.logger.info(f"开始删除子卷 {target}")

        def work():
            out, rc = run_ota_stream("delete", target, timeout=120)
            self.logger.info(f"delete 返回码={rc}")
            if rc != 0:
                self._task_err = f"删除失败:\n{out}"
        self._run_task(work, lambda ok, e: self._delete_done(ok, e),
                       [self.btn_delete])

    def _delete_done(self, ok, err):
        self.refresh_state()
        if err:
            self.show_error(err)
        else:
            self.statusbar.set_markup('<span foreground="green">✅ 子卷已删除</span>')
            self.logger.info("子卷删除成功")

    # ----- 还原 -----
    def on_restore_clicked(self, button):
        warn = (
            "<b>从备份还原</b>\n\n"
            "将从 @backup 子卷把系统还原为当前 @Vn (会先删除当前 @Vn 再从备份重建),\n"
            "并恢复备份的内核。\n\n"
            "<span foreground='#c62828'>还原完成后设备将自动重启!</span>\n\n继续?"
        )
        if not self._confirm_rich("从备份还原", warn):
            return
        self.logger.info("开始从 @backup 还原")

        def work():
            out, rc = run_ota_stream("restore", timeout=300)
            self.logger.info(f"restore 返回码={rc}")
            if rc != 0:
                self._task_err = f"还原失败:\n{out}"
        self._run_task(work, lambda ok, e: self._restore_done(ok, e),
                       [self.btn_restore])

    def _restore_done(self, ok, err):
        if err:
            self.show_error(err)
            self.statusbar.set_markup('<span foreground="#c62828">❌ 还原失败</span>')
            self.refresh_state()
        else:
            self.statusbar.set_markup('<span foreground="green">✅ 还原成功, 设备将重启</span>')
            self.show_info("备份还原成功!\n设备将自动重启进入还原后的系统。")

    # ---------- 日志保存 ----------
    def on_save_log(self, button):
        dlg = Gtk.FileChooserDialog(
            title="保存日志", parent=self, action=Gtk.FileChooserAction.SAVE)
        dlg.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                        Gtk.STOCK_SAVE, Gtk.ResponseType.OK)
        dlg.set_current_name(f"quectel_ota_{datetime.datetime.now():%Y%m%d_%H%M%S}.log")
        if dlg.run() == Gtk.ResponseType.OK:
            path = dlg.get_path()
            buf = self.log_tv.get_buffer()
            start, end = buf.get_bounds()
            text = buf.get_text(start, end, True)
            with open(path, 'w', encoding='utf-8') as f:
                f.write(text)
            self.logger.info(f"日志已保存: {path}")
        dlg.destroy()

    # ---------- 对话框 ----------
    def _confirm(self, title, msg):
        dlg = Gtk.MessageDialog(transient_for=self, modal=True,
                                message_type=Gtk.MessageType.QUESTION,
                                buttons=Gtk.ButtonsType.YES_NO, text=title)
        dlg.format_secondary_text(msg)
        r = dlg.run()
        dlg.destroy()
        return r == Gtk.ResponseType.YES

    def _confirm_rich(self, title, markup):
        dlg = Gtk.MessageDialog(transient_for=self, modal=True,
                                message_type=Gtk.MessageType.WARNING,
                                buttons=Gtk.ButtonsType.YES_NO, text=title)
        dlg.get_message_area().pack_start(self._markup_label(markup), False, False, 0)
        dlg.show_all()
        r = dlg.run()
        dlg.destroy()
        return r == Gtk.ResponseType.YES

    def _confirm_action(self, title, msg):
        dlg = Gtk.MessageDialog(transient_for=self, modal=True,
                                message_type=Gtk.MessageType.WARNING,
                                buttons=Gtk.ButtonsType.OK_CANCEL, text=title)
        dlg.format_secondary_text(msg)
        r = dlg.run()
        dlg.destroy()
        return r == Gtk.ResponseType.OK

    def _markup_label(self, markup):
        lbl = Gtk.Label(xalign=0, wrap=True)
        lbl.set_markup(markup)
        lbl.set_max_width_chars(56)
        return lbl

    def show_error(self, msg):
        d = Gtk.MessageDialog(transient_for=self, modal=True,
                              message_type=Gtk.MessageType.ERROR,
                              buttons=Gtk.ButtonsType.OK, text="错误")
        d.format_secondary_text(msg)
        self.logger.error(msg)
        d.run()
        d.destroy()

    def show_info(self, msg):
        d = Gtk.MessageDialog(transient_for=self, modal=True,
                              message_type=Gtk.MessageType.INFO,
                              buttons=Gtk.ButtonsType.OK, text="提示")
        d.format_secondary_text(msg)
        d.run()
        d.destroy()


def main():
    if not os.path.exists(OTA_RUN):
        print(f"ERROR: {OTA_RUN} 不存在, 请确认 quectel-ota 已安装")
        return
    win = TimeCapsuleWindow()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()


if __name__ == "__main__":
    main()
