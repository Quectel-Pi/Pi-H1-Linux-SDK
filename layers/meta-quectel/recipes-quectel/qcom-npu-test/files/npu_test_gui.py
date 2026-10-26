#!/usr/bin/env python3
"""
Quectel PI (QCS6490) Qualcomm NPU 图形化测试工具

高通 QCS6490 的 NPU 即 Hexagon DSP/HTP (Hexagon Tensor Processor),
通过 Qualcomm SNPE / QNN SDK 调用。本工具封装板上已有的命令行工具:

  - snpe-platform-validator   验证 DSP/GPU runtime 可用性
  - qnn-platform-validator    验证 QNN backend (dsp/gpu) 可用性
  - snpe-net-run              加载 .dlc 容器做单次推理 (CPU/GPU/DSP)
  - snpe-throughput-net-run   并发吞吐量压测, 输出 FPS/延迟

GUI: GTK3 + cairo (不依赖 OpenCV), 与参考的 RK3576 rknpu-test 风格一致。
"""

import gi
gi.require_version('Gtk', '3.0')
gi.require_version('Pango', '1.0')
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import Gtk, GdkPixbuf, Gdk, GLib, Pango
import cairo

import os
import re
import sys
import glob
import mmap
import shutil
import signal
import tempfile
import subprocess
import threading
import datetime
import logging
import time
import json
import base64
import urllib.request
import urllib.error

# ========== 配置 ==========
# 板上 SNPE/QNN 工具路径 (由 qcom-snpe-sdk / qcom-qnn-sdk 安装)
SNPE_PF_VALIDATOR   = "/usr/bin/snpe-platform-validator"
QNN_PF_VALIDATOR    = "/usr/bin/qnn-platform-validator"
SNPE_NET_RUN        = "/usr/bin/snpe-net-run"
SNPE_THROUGHPUT_RUN = "/usr/bin/snpe-throughput-net-run"
SNPE_DIAGVIEW      = "/usr/bin/snpe-diagview"
NPU_DAEMON         = "/usr/bin/npu-daemon"
QNN_NET_RUN         = "/usr/bin/qnn-net-run"

# 模型搜索路径:
#   /usr/share/qcom-npu-test/models  本包自带目录 (用户把 .dlc 放这里)
#   /usr/share/model /usr/share/ml-sdk/models  系统/SDK 模型目录
#   /data/local/tmp/models           adb push 临时模型
MODEL_DIRS = ["/usr/share/qcom-npu-test/models",
              "/usr/share/model", "/usr/share/ml-sdk/models",
              "/data/local/tmp/models"]
# 测试图片搜索路径 (本包自带 bus.jpg)
IMAGE_DIRS = ["/usr/share/qcom-npu-test/images",
              "/usr/share/model", "/usr/share/ml-sdk/models"]
# COCO 标签 (YOLO 用)
COCO_LABELS = "/usr/share/qcom-npu-test/labels/coco_80_labels_list.txt"

LOG_DIR = "/tmp/qcom_npu_test_logs"


# ========== 多模态 (视觉模型) ==========
# 视觉通路: 摄像头帧 -> JPEG -> base64 data URL -> /v1/chat/completions。
# 服务端只接受内联 data URL(远程 URL 被明确禁用), 单次一张图。
#
# 模型名/视觉输入尺寸/生成长度上限/解码速度 全部不在代码里写死:
#   1. 启动时读服务端自己发布的 /usr/lib/qwen35/runtime.json (model_id, 网格,
#      token 上限, 超时) —— 这是配置权威;
#   2. 每次回答后从响应体的 qwen35_metrics 里取实测值 (image.decoder_size_wh,
#      decode_tokens_per_second, stage_wall_seconds, vision_run_seconds,
#      context_capacity_tokens) —— 这是运行权威;
#   3. 学到的值缓存到 ~/.cache 下, 下次启动就不用再猜 (旧版本的"6.9 tok/s"
#      "1024x576" 这种常量就是这么来的, 一律改为实测)。
VLM_ENDPOINT      = "http://127.0.0.1:8000/v1/chat/completions"
VLM_MODELS        = "http://127.0.0.1:8000/v1/models"
VLM_BASE          = "http://127.0.0.1:8000"
VLM_RUNTIME_JSON  = "/usr/lib/qwen35/runtime.json"
VLM_SERVICE       = "qwen35"          # systemd 单元名, 不是模型名
VLM_TIMEOUT       = 300
VLM_PROMPT        = "请用中文描述你看到的画面内容。"
VLM_MAX_TOKENS    = 128               # 首次使用的默认值, 实际上限从 runtime.json 读
VLM_JPEG_QUALITY  = "90"
VLM_PATCH_SIZE    = 16                # 仅当 runtime.json 不可读时用于推算网格

_SIZE_RE = re.compile(r"^\d+(?:\.\d+)?[bB]$")
_CTX_RE = re.compile(r"^(\d+)([kKmM])$")


def vlm_display_name(model_id):
    if not model_id:
        return "未探测到模型"
    raw = str(model_id).strip()
    toks = [t for t in re.split(r"[-_/\s]+", raw) if t]
    if not toks:
        return "未探测到模型"

    family = toks[0]
    size = ctx = None
    for t in toks[1:]:
        if size is None and _SIZE_RE.match(t):
            size = t
            continue
        m = _CTX_RE.match(t)
        if m and ctx is None:
            ctx = (m.group(1), m.group(2).upper())
    if size is None and ctx is None:
        return raw

    name = family[:1].upper() + family[1:]
    if size:
        name += "-" + size[:-1] + size[-1].upper()
    return f"{name} · {ctx[0]}{ctx[1]} 上下文" if ctx else name
VLM_PROFILE_CACHE = os.path.join(os.path.expanduser("~"), ".cache",
                                 "qcom-npu-test", "vlm_profile.json")
# CamxBridge 是按需建流的: 客户端一连上来先吐几帧全黑的初始 buffer,
# 真实画面过一会才到. 预热期不允许点识别, 否则会拿黑图去问模型.
MM_WARMUP_FRAMES = 15
# 再暗就不发请求了: 全黑帧模型只能说"图是空的", 白烧 20s NPU
MM_MIN_LUMA = 8.0


class VlmProfile(object):
    """描述"这台机器上这个服务实际是什么样"——全部字段来自读取或实测.

    runtime_cfg  : /usr/lib/qwen35/runtime.json 的内容 (可能为空)
    observed     : 从真实响应学到的值, 缓存在磁盘上
    """

    # 只有一条真实数据都没有时才用的初始估计(秒). 一旦跑过一次就会被实测覆盖,
    # 所以这里的数字不会成为谎话, 只是第一眼的保守占位.
    FIRST_RUN_STAGES = {"frontend": 1.5, "vision": 1.3,
                        "npu_prefill": 8.0, "decoder": 12.0}

    def __init__(self):
        self.runtime_cfg = None
        self.observed = {}
        self.model_id = None
        self.max_tokens_cap = VLM_MAX_TOKENS
        self.decoder_wh = None          # 服务端视觉输入尺寸 (w, h)
        self.image_tokens = None
        self.ctx_capacity = None
        self.stage_budget = {}
        self.decode_tps = None
        self.load_cache()
        self.load_runtime()

    # ---------- 启动期: 读服务端发布出来的配置 ----------
    def load_runtime(self):
        try:
            with open(VLM_RUNTIME_JSON, "r") as f:
                cfg = json.load(f)
        except Exception:
            return False
        self.runtime_cfg = cfg
        if cfg.get("model_id"):
            self.model_id = cfg["model_id"]
        lim = cfg.get("limits") or {}
        for key in ("max_tokens", "default_max_tokens"):
            if isinstance(lim.get(key), int) and lim[key] > 0:
                self.max_tokens_cap = lim[key]
                break
        rt = cfg.get("runtime") or {}
        if rt.get("stage_timeout_seconds"):
            global VLM_TIMEOUT
            VLM_TIMEOUT = int(rt["stage_timeout_seconds"])
        # shared_image_grid_thw = [t, h_patches, w_patches]; 乘 patch 尺寸即为
        # 服务端真正喂给视觉塔的像素尺寸(不需要代码里写 1024x576).
        grid = rt.get("shared_image_grid_thw")
        if isinstance(grid, (list, tuple)) and len(grid) == 3:
            try:
                h = int(grid[1]) * VLM_PATCH_SIZE
                w = int(grid[2]) * VLM_PATCH_SIZE
                if w > 0 and h > 0:
                    self.decoder_wh = (w, h)
                    # Qwen 视觉塔按 2x2 合并 patch, 所以 token 数是 patch 数的 1/4
                    self.image_tokens = int(grid[1]) * int(grid[2]) // 4
            except (TypeError, ValueError):
                pass
        return True

    # ---------- 运行期: 用实测值覆盖估计 ----------
    def learn(self, meta):
        if not isinstance(meta, dict):
            return
        if meta.get("model"):
            self.model_id = meta["model"]
        img = meta.get("image") or {}
        dw = img.get("decoder_size_wh")
        if isinstance(dw, (list, tuple)) and len(dw) == 2:
            try:
                self.decoder_wh = (int(dw[0]), int(dw[1]))
            except (TypeError, ValueError):
                pass
        if meta.get("context_capacity_tokens"):
            self.ctx_capacity = int(meta["context_capacity_tokens"])
        tps = meta.get("decode_tps")
        if isinstance(tps, (int, float)) and tps > 0.5:
            # 指数滑动平均: 温度/负载变化下跟随, 又不会被单次抖动带飞
            self.decode_tps = tps if self.decode_tps is None \
                else self.decode_tps * 0.7 + tps * 0.3
        st = meta.get("stages") or {}
        cached = meta.get("prefill_cached_tokens") or 0
        for src, dst in (("frontend", "frontend"), ("npu_prefill", "npu_prefill"),
                         ("decoder", "decoder")):
            v = st.get(src)
            if not isinstance(v, (int, float)) or v < 0:
                continue
            # 预填充命中前缀缓存时几乎不耗时, 拿它去学会把冷启动的估计值压成 0.
            # 只学真正算过的那些, 缓存命中的留给展示层单独标注.
            if dst == "npu_prefill" and cached:
                continue
            prev = self.stage_budget.get(dst)
            self.stage_budget[dst] = v if prev is None else prev * 0.7 + v * 0.3
        if isinstance(meta.get("vision_s"), (int, float)) and meta["vision_s"] > 0:
            prev = self.stage_budget.get("vision")
            v = meta["vision_s"]
            self.stage_budget["vision"] = v if prev is None else prev * 0.7 + v * 0.3
        self.save_cache()

    # ---------- 磁盘缓存 ----------
    def load_cache(self):
        try:
            with open(VLM_PROFILE_CACHE, "r") as f:
                d = json.load(f)
        except Exception:
            return
        self.observed = d if isinstance(d, dict) else {}
        if self.observed.get("decode_tps"):
            self.decode_tps = float(self.observed["decode_tps"])
        sb = self.observed.get("stage_budget")
        if isinstance(sb, dict):
            self.stage_budget = {k: float(v) for k, v in sb.items()
                                 if isinstance(v, (int, float))}
        if self.observed.get("decoder_wh"):
            try:
                self.decoder_wh = tuple(int(x) for x in self.observed["decoder_wh"])
            except (TypeError, ValueError):
                pass

    def save_cache(self):
        try:
            d = os.path.dirname(VLM_PROFILE_CACHE)
            if d and not os.path.isdir(d):
                os.makedirs(d, exist_ok=True)
            tmp = VLM_PROFILE_CACHE + ".tmp"
            with open(tmp, "w") as f:
                json.dump({"decode_tps": self.decode_tps,
                           "stage_budget": self.stage_budget,
                           "decoder_wh": list(self.decoder_wh) if self.decoder_wh else None,
                           "model_id": self.model_id}, f)
            os.replace(tmp, VLM_PROFILE_CACHE)
        except Exception:
            pass        # 缓存写不了不影响功能, 只是下次还得重新学

    # ---------- 对外展示 ----------
    @property
    def model_display(self):
        return vlm_display_name(self.model_id)

    @property
    def vision_size_text(self):
        wh = self.decoder_wh
        return f"视觉输入 {wh[0]}x{wh[1]}" if wh else "视觉输入待探测"

    @property
    def tps(self):
        return self.decode_tps or 6.5          # 仅首帧估计值, 很快被实测覆盖

    def stage_plan(self):
        """把学到的阶段耗时转成动画用的时间轴 [(开始秒, 名称, 说明)].

        完全没有历史时返回 FIRST_RUN_STAGES 推出的保守顺序, 跑过一次后就
        全部改成实测均值, 所以动画节奏和真实阶段是对得上的。
        """
        b = dict(self.FIRST_RUN_STAGES)
        for k, v in self.stage_budget.items():
            if isinstance(v, (int, float)) and v > 0:
                b[k] = float(v)
        size = (f"letterbox 到 {self.decoder_wh[0]}x{self.decoder_wh[1]}"
                if self.decoder_wh else "letterbox 到服务端输入尺寸")
        vt = "视觉塔三阶段"
        if self.image_tokens:
            vt += f", {self.image_tokens} 图像 token"
        rows, t = [], 0.0
        rows.append((t, "\u2460 前端预处理", f"解码 JPEG + {size} + 分词"))
        t += b["frontend"]
        rows.append((t, "\u2461 视觉编码", vt))
        t += b["vision"]
        rows.append((t, "\u2462 预填充", "文本 + 图像 embedding 喂入 HTP"))
        t += b["npu_prefill"]
        rows.append((t, "\u2463 解码生成", f"逐 token 输出, 实测约 {self.tps:.1f} tok/s"))
        return rows


VLM_PROFILE = VlmProfile()

# 回答区的醒目标式. 用 CSS 而不是 override_font, 因为字号/背景/边框一次配完,
# 且能跟随主题色. px 在 GTK3 CSS 里是绝对单位, 不受桌面字号设置影响.
VLM_CSS = b"""
#vlm-answer-frame {
    background-color: alpha(@theme_selected_bg_color, 0.07);
    border: 2px solid alpha(@theme_selected_bg_color, 0.45);
    border-radius: 10px;
}
#vlm-answer-text, #vlm-answer-text text {
    font-size: 19px;
    color: @theme_fg_color;
}
#vlm-stage-text, #vlm-stage-text text {
    font-size: 13px;
    color: alpha(@theme_fg_color, 0.85);
}
#vlm-busy-stage {
    font-size: 15px;
    font-weight: bold;
}
#vlm-recognize-btn {
    font-size: 17px;
    font-weight: bold;
    padding: 12px 30px;
    border-radius: 8px;
}
#vlm-recognize-btn:disabled {
    font-size: 17px;
    padding: 12px 30px;
}
#vlm-hint {
    font-size: 12px;
    color: alpha(@theme_fg_color, 0.6);
}
.vlm-small-btn {
    font-size: 12px;
    padding: 2px 6px;
    min-height: 0;
}
"""


def install_vlm_css():
    """把回答区样式挂到默认屏幕. 重复调用无害."""
    try:
        prov = Gtk.CssProvider()
        try:
            prov.load_from_data(VLM_CSS)          # GTK >= 3.20
        except TypeError:
            prov.load_from_data(VLM_CSS.decode(), len(VLM_CSS))
        Gtk.StyleContext.add_provider_for_screen(
            Gdk.Screen.get_default(), prov,
            Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        return True
    except Exception:
        return False    # 样式失效也还有字号保底, 不影响功能

# 摄像头源: 与"摄像头实时"选项卡完全同一个 (CamxBridge 虚拟摄像头)
CAM_DEVICE = "/dev/video10"
CAM_W, CAM_H = 1280, 720


# ========== 工具函数 ==========
def camera_pipeline_cmd(w=CAM_W, h=CAM_H, device=CAM_DEVICE):
    """gst 取流命令: 裸 RGB 写到 stdout, 由调用方 os.read 凑成一帧.

    sync=false 让管道按摄像头自己的节奏出帧, 不按墙上时间重采样; 拿到的就是
    摄像头原始帧, 不会因为下游慢而插值重复帧.
    """
    return ["gst-launch-1.0", "-q", "v4l2src", f"device={device}", "!",
            "videoconvert", "!",
            f"video/x-raw,format=RGB,width={w},height={h}",
            "!", "fdsink", "fd=1", "sync=false"]


def find_models():
    """查找 .dlc / .so(QNN模型) / .onnx / .tflite 模型文件

    .dlc  -> SNPE snpe-net-run 容器
    .onnx -> QNN qnn-net-run 模型 (外部权重 model.data 需同目录)
    .tflite-> SNPE tflite 容器
    .so   -> QNN context-binary 模型
    """
    models = []
    for d in MODEL_DIRS:
        if not os.path.isdir(d):
            continue
        for ext in ("*.dlc", "*.tflite"):
            models.extend(glob.glob(os.path.join(d, "**", ext), recursive=True))
        # QNN ONNX 模型: model.onnx + model.data 在子目录里
        for f in glob.glob(os.path.join(d, "**", "*.onnx"), recursive=True):
            models.append(f)
        # QNN 模型 .so 太多(libQnn*CalculatorStub 等), 只取名字含 model 的
        for f in glob.glob(os.path.join(d, "**", "*.so"), recursive=True):
            base = os.path.basename(f).lower()
            if "model" in base and "stub" not in base and "calculator" not in base:
                models.append(f)
    return sorted(set(models))


def model_backend(model_path):
    """根据模型扩展名判断后端工具

    .dlc/.tflite -> snpe (snpe-net-run --container)
                    也能用 qnn-net-run --dlc_path, 但 SNPE 是 DLC 首选
    .so          -> qnn  (qnn-net-run --model, QNN context binary)
    .onnx        -> onnx_reference (AI Hub 源模型, 需在 PC 上用
                    qairt-converter/qnn-onnx-converter 转成 .so 后才能跑)
    """
    ext = os.path.splitext(model_path)[1].lower()
    if ext == ".onnx":
        return "onnx_reference"
    if ext in (".dlc", ".tflite"):
        return "snpe"
    if ext == ".so":
        return "qnn"
    return "snpe"


def find_images():
    imgs = []
    for d in IMAGE_DIRS:
        if not os.path.isdir(d):
            continue
        for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp"):
            imgs.extend(glob.glob(os.path.join(d, ext)))
            imgs.extend(glob.glob(os.path.join(d, "**", ext), recursive=True))
    # 用户上传目录
    upload_dir = "/tmp/npu_uploads"
    if os.path.isdir(upload_dir):
        for ext in ("*.jpg", "*.jpeg", "*.png", "*.bmp"):
            imgs.extend(glob.glob(os.path.join(upload_dir, ext)))
    return sorted(set(imgs))


def run_cmd(cmd, timeout=120, cwd=None, env=None):
    """运行命令, 返回 (stdout+stderr, returncode)"""
    log = logging.getLogger("npu_test")
    log.debug(f"CMD: {' '.join(cmd)}")
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                            timeout=timeout, cwd=cwd, env=env)
        out = (r.stdout or "") + (r.stderr or "")
        if r.returncode != 0:
            log.warning(f"命令返回码非0: {r.returncode}")
        return out, r.returncode
    except subprocess.TimeoutExpired:
        log.error(f"命令超时({timeout}s): {' '.join(cmd)}")
        return f"ERROR: 命令超时({timeout}s)", -1
    except FileNotFoundError as e:
        log.error(f"程序不存在: {e}")
        return f"ERROR: 程序不存在: {e}", -1
    except Exception as e:
        log.error(f"命令异常: {e}")
        return f"ERROR: {e}", -1


# ========== 解析函数 ==========
def parse_snpe_pf(text):
    """解析 snpe-platform-validator 输出"""
    info = {}
    # Unit Test on the runtime DSP: Passed.
    m = re.search(r'Unit Test on the runtime (\w+):\s*(\w+)', text)
    if m:
        info.setdefault('runtimes', {})[m.group(1).lower()] = m.group(2)
    # SNPE is supported for runtime DSP on the device.
    m = re.search(r'SNPE is (supported|not supported) for runtime (\w+)', text)
    if m:
        info.setdefault('runtimes', {})[m.group(2).lower()] = 'Passed' if 'supported' in m.group(1) else 'Failed'
    # Prerequisites: Present / Absent
    for m in re.finditer(r'Runtime (\w+) Prerequisites:\s*(\w+)', text):
        info.setdefault('prereq', {})[m.group(1).lower()] = m.group(2)
    # GPU 缺 OpenCL
    if 'Could not find libOpenCL' in text:
        info.setdefault('runtimes', {})['gpu'] = 'Absent (no OpenCL)'
    return info


def parse_qnn_pf(text):
    """解析 qnn-platform-validator 输出"""
    info = {}
    m = re.search(r'Unit Test on the backend (\w+):\s*(\w+)', text)
    if m:
        info.setdefault('backends', {})[m.group(1).lower()] = m.group(2)
    m = re.search(r'QNN is (supported|not supported) for backend (\w+)', text)
    if m:
        info.setdefault('backends', {})[m.group(2).lower()] = 'Passed' if 'supported' in m.group(1) else 'Failed'
    for m in re.finditer(r'Backend (\w+) Prerequisites:\s*(\w+)', text):
        info.setdefault('prereq', {})[m.group(1).lower()] = m.group(2)
    # Results Summary block
    m = re.search(r'Backend\s*=\s*(\w+)\s*\{(.*?)\}', text, re.DOTALL)
    if m:
        info['summary_backend'] = m.group(1)
        for line in m.group(2).splitlines():
            if ':' in line:
                k, v = line.strip().split(':', 1)
                info.setdefault('summary', {})[k.strip()] = v.strip()
    return info


def parse_inference(text):
    """解析 snpe-net-run 单次推理输出, 提取耗时"""
    info = {}
    # SNPE build version
    m = re.search(r'snpe-net-run build version:\s*([\w.]+)', text)
    if m:
        info['build_ver'] = m.group(1)
    # 模型信息
    m = re.search(r'Model name\s*:\s*(\S+)', text)
    if m:
        info['model_name'] = m.group(1)
    # 各层耗时统计
    times = re.findall(r'(\d+\.?\d*)\s*ms\s+', text)
    if times:
        t = [float(x) for x in times]
        info['layer_times'] = t
        info['total_ms'] = sum(t)
        info['max_ms'] = max(t)
        info['avg_layer_ms'] = sum(t) / len(t)
    # 执行成功标志
    if 'Successfully executed' in text or 'Success' in text:
        info['status'] = 'Success'
    elif 'ERROR' in text or 'Error' in text:
        info['status'] = 'Failed'
    # 推理输出结果 (Top5 等) - SNPE 默认把每层输出写到 output_dir, 文本里不一定有
    # 找类似 "Result" / "output" 的统计
    m = re.search(r'Average time[:\s]*([\d.]+)\s*ms', text, re.IGNORECASE)
    if m:
        info['avg_ms'] = float(m.group(1))
        info['avg_fps'] = 1000.0 / float(m.group(1)) if float(m.group(1)) > 0 else 0
    m = re.search(r'FPS[:\s]*([\d.]+)', text)
    if m:
        info['fps'] = float(m.group(1))
    return info


def parse_throughput(text):
    """解析 snpe-throughput-net-run 输出"""
    info = {}
    m = re.search(r'snpe-throughput-net-run build version:\s*([\w.]+)', text)
    if m:
        info['build_ver'] = m.group(1)
    # 容器信息
    m = re.search(r'Network:\s*(\S+)', text)
    if m:
        info['network'] = m.group(1)
    m = re.search(r'Processing graph:\s*(\S+)', text)
    if m:
        info['network'] = m.group(1)

    # 实际输出格式 (SNPE v2.x):
    #   Total throughput(inferences per second): 69.737939 infs/sec
    #   Total throughput(latency per inference): 0.014339 sec/inf
    m = re.search(r'[Tt]otal\s+throughput\s*\(inferences per second\)\s*:\s*([\d.]+)', text)
    if m:
        info['avg_fps'] = float(m.group(1))
    m = re.search(r'[Tt]otal\s+throughput\s*\(latency per inference\)\s*:\s*([\d.]+)', text)
    if m:
        info['avg_ms'] = float(m.group(1)) * 1000.0  # sec -> ms

    # 兼容旧格式 / 其他版本
    if 'avg_fps' not in info:
        m = re.search(r'Average\s+FPS\s*[:=]?\s*([\d.]+)', text, re.IGNORECASE)
        if m:
            info['avg_fps'] = float(m.group(1))
    if 'avg_ms' not in info:
        m = re.search(r'Average\s+[Ll]atency\s*[:=]?\s*([\d.]+)\s*ms?', text)
        if m:
            info['avg_ms'] = float(m.group(1))
    m = re.search(r'Total\s+(?:Inferences|Iterations)\s*[:=]?\s*([\d.]+)', text, re.IGNORECASE)
    if m:
        info['total_inferences'] = int(float(m.group(1)))
    m = re.search(r'[Tt]otal\s+execution\s+time\s*[:=]?\s*([\d.]+)\s*s?', text)
    if m:
        info['total_time_s'] = float(m.group(1))
    # 逐时段 FPS 记录 (如果有)
    fps_vals = [float(x) for x in re.findall(r'FPS[:\s]+([\d.]+)', text)]
    if fps_vals:
        info['fps_samples'] = fps_vals
        if 'avg_fps' not in info:
            info['avg_fps'] = sum(fps_vals) / len(fps_vals)
    return info


class NpuDaemon:
    """常驻 SNPE 会话: 容器只加载一次, 之后每次点击只付推理时间.

    snpe-net-run 每点一次都要重走 进程启动 + 13.6MB DLC 解析 + DSP 会话
    建立, 实测 ~280ms, 而推理本身只要 10~20ms. 用一个常驻进程收 raw 喂
    结果, 点击延迟从 ~310ms 降到 ~30ms. 输出与 snpe-net-run 逐字节一致
    (boxes/scores/class_idx 三路 raw 已 md5 校验).
    """

    def __init__(self, model, perf="burst", runtime="dsp"):
        self.dir = tempfile.mkdtemp(prefix="npu_daemon_", dir="/tmp")
        # 输入走 /dev/shm: 管道每帧要多付 ~25ms 的 write+copy
        self.shm_path = f"/dev/shm/npu_in_{os.getpid()}_{id(self)}.raw"
        self.mm = None
        self.runtime = runtime
        args = [NPU_DAEMON, model, self.dir, perf,
                self.shm_path if os.path.isdir("/dev/shm") else "", runtime]
        # bufsize=0: 门铃/应答都只几个字节, 不要 Python 侧再缓存
        self.p = subprocess.Popen(args,
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, bufsize=0)
        # SNPE 初始化时会先往 stdout 打一行 rpcmem 提示, 跳过它等 READY
        line = b""
        for _ in range(20):
            line = self.p.stdout.readline()
            if line.startswith(b"READY"):
                break
            if not line:                      # 进程已退出
                break
        if not line.startswith(b"READY"):
            self.close()
            raise RuntimeError(line.decode(errors="replace").strip()
                               or "npu-daemon 未就绪")
        parts = line.split()
        self.input_bytes = int(parts[2])
        self.use_shm = len(parts) > 3 and parts[3] == b"1"
        if self.use_shm:
            with open(self.shm_path, "wb") as f:
                f.truncate(self.input_bytes)
            self._shm_file = open(self.shm_path, "r+b")
            self.mm = mmap.mmap(self._shm_file.fileno(), self.input_bytes)
        self.lock = threading.Lock()

    def infer(self, raw_bytes):
        """喂一帧 float32 raw, 返回本轮 execute() 毫秒数."""
        with self.lock:
            if self.mm is not None:
                self.mm[:] = raw_bytes
                self.p.stdin.write(b"\x01")     # 门铃
            else:
                self.p.stdin.write(raw_bytes)
            line = self.p.stdout.readline().decode(errors="replace").strip()
        if not line.startswith("OK "):
            raise RuntimeError(line or "npu-daemon 无响应")
        self.last_ms = float(line.split()[1])
        return self.last_ms

    def close(self):
        try:
            self.p.stdin.close()
        except Exception:
            pass
        try:
            self.p.kill()
            self.p.wait(timeout=5)
        except Exception:
            pass
        try:
            if self.mm is not None:
                self.mm.close()
            os.unlink(self.shm_path)
        except Exception:
            pass
        shutil.rmtree(self.dir, ignore_errors=True)


_DAEMONS = {}
_DAEMON_LOCK = threading.Lock()


def get_daemon(model, runtime="dsp"):
    """按 (模型, 后端) 缓存常驻会话; 起不来就返回 None, 调用方回退 snpe-net-run."""
    if not os.path.exists(NPU_DAEMON):
        return None
    key = (model, runtime)
    with _DAEMON_LOCK:
        d = _DAEMONS.get(key)
        if d is not None and d.p.poll() is None:
            return d
        try:
            d = NpuDaemon(model, runtime=runtime)
        except Exception as e:
            logging.getLogger("npu_test").warning(
                f"npu-daemon({runtime}) 启动失败, 回退 snpe-net-run: {e}")
            _DAEMONS[key] = None
            return None
        logging.getLogger("npu_test").info(f"npu-daemon 就绪: {model} [{runtime}]")
        _DAEMONS[key] = d
        return d


def daemon_run(model, runtime, raw_bytes):
    """用常驻会话跑一帧 raw, 返回 (daemon, ms); 没会话就返回 None 让调用方回退 CLI."""
    d = get_daemon(model, runtime)
    if d is None or len(raw_bytes) != d.input_bytes:
        return None
    try:
        return d, d.infer(raw_bytes)
    except Exception as e:
        logging.getLogger("npu_test").warning(
            f"npu-daemon({runtime}) 推理失败, 回退 CLI: {e}")
        return None


def throughput_via_daemon(model, runtime, raw_bytes, seconds):
    """常驻会话压测: 固定时长内连续喂帧, 返回 snpe-throughput-net-run 同款文本.

    比 snpe-throughput-net-run 更贴实际: 那个每次都要重建会话, 报的是它自己
    的稳态; 这里报的是常驻会话真实吞吐.
    """
    r = daemon_run(model, runtime, raw_bytes)
    if r is None:
        return None
    d, _ = r
    n, lat = 0, []
    t0 = time.monotonic()
    while time.monotonic() - t0 < seconds:
        lat.append(d.infer(raw_bytes))
        n += 1
    wall = time.monotonic() - t0
    if not n:
        return None
    avg = sum(lat) / n
    return (f"npu-daemon[{d.runtime}] 常驻会话压测\n"
            f"Total throughput(inferences per second): {n / wall:.3f} infs/sec\n"
            f"Total throughput(latency per inference): {wall / n:.6f} sec/inf\n"
            f"Total Inferences: {n}\n"
            f"Total execution time: {wall:.3f} s\n"
            f"单帧 execute 均值 {avg:.2f} ms / 最小 {min(lat):.2f} / 最大 {max(lat):.2f}")


def yolo_detect_pixbuf(model, pixbuf, runtime="dsp", input_size=640):
    """摄像头一帧: letterbox -> 常驻会话 -> 解码. 返回 (dets, infer_ms)."""
    raw, (top, left, scale), (w, h) = yolo_letterbox(pixbuf, input_size)
    r = daemon_run(model, runtime, raw)
    if r is None:
        return [], 0.0
    d, ms = r
    dets = _decode_yolo_raws(d.dir, top, left, scale, w, h,
                             conf=0.5, iou=0.45, input_size=input_size)
    return dets, ms


# 各 runtime 最近一次实测推理延迟(ms). 用来算加速比, 也用来在没有对比数据时
# 老实显示"—", 绝不猜数字.
_RUNTIME_MS = {}


def fmt_timing(lat, call_ms, runtime=""):
    """把一次运行的耗时拼成面板文字.

    三档 runtime 用同一个口径: 一次推理花多久. DSP 是常驻会话的 execute(),
    CPU/GPU 是 SNPEDiag 的 Total Inference Time (没有 accelerator 计数器).
    """
    lat = lat or {}
    npu = lat.get('avg_ms', 0) or 0
    tot = lat.get('total_ms', 0) or 0
    clk = lat.get('clock_mhz', 0) or 0
    ms = tot or npu
    if ms > 0 and runtime:
        _RUNTIME_MS[runtime.split()[0]] = ms
    out = []
    if ms > 0:
        out.append(f"推理延迟: {ms:.2f} ms   [{runtime}]\n")
        if clk > 0:
            out.append(f"DSP 计算: {npu:.2f} ms  (HTP {clk:.0f} MHz)\n")
    if call_ms > 0:
        out.append(f"端到端点击: {call_ms:.0f} ms  (letterbox + 推理 + 后处理)\n")
    # 只在真的量过别的档位时才报倍数
    if runtime.startswith("DSP") and ms > 0:
        ref = [(k, v) for k, v in _RUNTIME_MS.items()
               if k in ("GPU", "CPU") and v > 0]
        if ref:
            out.append("加速比: " + ", ".join(
                f"比 {k} 快 {v / ms:.1f}×" for k, v in ref) + "\n")
    return "".join(out)


def compare_text(rows):
    """三档对比表的文字. rows = [(runtime, 推理ms, 端到端ms, 目标数), ...]"""
    ok = [r for r in rows if r[1] > 0]
    base = min(r[1] for r in ok) if ok else 0
    out = [f"{'='*55}\n  ⚡ 同模型同图 · 三档 Runtime 实测对比\n{'='*55}\n\n"
           f"  {'Runtime':<16}{'推理延迟':>12}{'端到端':>12}{'相对':>9}\n"
           f"  {'-'*49}\n"]
    for name, ms, wall, nd in rows:
        if ms <= 0:
            out.append(f"  {name:<16}{'失败':>10}  (看日志)\n")
            continue
        out.append(f"  {name:<16}{ms:>9.2f}ms{wall:>9.0f}ms"
                   f"{ms/base:>8.1f}×  ({nd} 目标)"
                   + ("  ⚠ 输出异常" if nd == 0 else "") + "\n")
    if len(ok) == 3:
        out.append(f"\n  HTP/NPU 相对 CPU 加速 {rows[0][1]/base:.1f}×, "
                   f"相对 GPU 加速 {rows[1][1]/base:.1f}×\n")
    if any(r[3] == 0 for r in ok):
        # 量化 DLC 在 CPU runtime 上算得出结果但分数全塌, 耗时也只能当参考.
        out.append("  ⚠ 0 目标那档: 量化 DLC 的 CPU runtime 输出分数塌陷, "
                   "耗时可信, 结果不可信\n")
    out.append("  推理延迟 = 一次推理本身; 端到端 = 点一下要等多久\n")
    out.append("  三档都用常驻会话, 新建会话的开销不在计时里 "
               "(GPU 光建会话就要 1.35s)\n")
    return "".join(out)


def measure_npu_latency(model, out_dir):
    """从本次推理的 SNPEDiag.log 里取 NPU 纯推理时间 (snpe-diagview).

    snpe-net-run 每次调用都要重跑 HTP graph prepare (容器加载 / DSP 初始化 /
    图定序), 这部分固定开销远大于推理本身, 所以不能用它包住的 wall time
    当"推理延迟". SNPEDiag.log 里本来就有 Total Inference Time, 直接读,
    不用像旧版那样另起一个 snpe-throughput-net-run 进程再跑 1 秒循环
    (那一步固定多花 ~1.2s, 是点击后卡顿的第二大来源).

    Returns:
        dict: {'avg_ms': float, 'avg_fps': float, 'raw': str}
              非 snpe 后端 / 无日志 / 工具缺失时返回空 dict.
    """
    if model_backend(model) != "snpe":
        return {}
    # 常驻会话跑的这一帧: 没有 SNPEDiag.log, 时间就是 daemon 报的 execute().
    # execute() 是同步调用, 返回即算完, 所以它既是推理时间也是总时间.
    for d in _DAEMONS.values():
        if d is not None and getattr(d, "dir", None) == out_dir \
                and getattr(d, "last_ms", 0):
            return {'avg_ms': d.last_ms,
                    'total_ms': d.last_ms,
                    'clock_mhz': 0.0,
                    'avg_fps': 1000.0 / d.last_ms,
                    'raw': 'npu-daemon'}
    if not os.path.exists(SNPE_DIAGVIEW):
        return {}
    logp = os.path.join(out_dir, "SNPEDiag.log")
    if not os.path.exists(logp):
        logging.getLogger("npu_test").info("无 SNPEDiag.log, 跳过延迟解析")
        return {}
    out, rc = run_cmd([SNPE_DIAGVIEW, f"--input_log={logp}"], timeout=30)
    def us(pat):
        m = re.search(pat, out)
        return float(m.group(1)) / 1000.0 if m else 0.0
    # Snpe Accelerator Time = DSP 上真正跑网络的时间; Total Inference Time 还含
    # 4.9MB float32 输入的 fastrpc 搬运 / 输出张量拷贝, 单帧 cold run 上差 ~13ms.
    acc = us(r'Snpe Accelerator Time:\s*([\d.]+)\s*us')
    tot = us(r'Total Inference Time:\s*([\d.]+)\s*us')
    if not tot:
        return {}
    m = re.search(r'Subnet 0 \(DSP\)\s*:\s*(\d+)\s*cycles', out)
    # 同一张图的 DSP cycles 恒定 (~14.1M), 用它对时间反推当前 DSP 频率:
    # 单帧冷跑 ~0.9GHz, 持续跑满 ~1.4GHz, 这才是 28ms vs 10ms 的实际来源.
    clock = int(m.group(1)) / acc / 1000.0 if (m and acc) else 0.0
    return {'avg_ms': acc or tot,
            'total_ms': tot,
            'clock_mhz': clock,
            'avg_fps': 1000.0 / tot if tot > 0 else 0,
            'raw': out}


def parse_yolo_output(text):
    """解析 SNPE yolov5 demo 输出 (沿用 RK3576 rknn demo 的输出格式假设)"""
    info = {}
    m = re.search(r'build version:\s*([\w.]+)', text)
    if m:
        info['build_ver'] = m.group(1)
    m = re.search(r'model\s+input\s+(?:height|H)\s*=?\s*(\d+).*?(?:width|W)\s*=?\s*(\d+).*?(?:channel|C)\s*=?\s*(\d+)',
                  text, re.IGNORECASE | re.DOTALL)
    if m:
        info['model_h'] = int(m.group(1))
        info['model_w'] = int(m.group(2))
        info['model_c'] = int(m.group(3))
    m = re.search(r'once\s+run\s+use\s+([\d.]+)ms', text, re.IGNORECASE)
    if m:
        info['infer_ms'] = float(m.group(1))
    m = re.search(r'average\s+run\s+([\d.]+)ms', text, re.IGNORECASE)
    if m:
        info['avg_ms'] = float(m.group(1))
        info['avg_fps'] = 1000.0 / float(m.group(1)) if float(m.group(1)) > 0 else 0
    # 检测结果: name @ (l t r b) conf
    detections = []
    for m in re.finditer(r'(\S+)\s+@\s*\((\d+)\s+(\d+)\s+(\d+)\s+(\d+)\)\s+([\d.]+)', text):
        name = m.group(1).strip()
        d = {'name': name,
             'left': int(m.group(2)), 'top': int(m.group(3)),
             'right': int(m.group(4)), 'bottom': int(m.group(5)),
             'conf': float(m.group(6))}
        # 用 COCO labels 把索引映射成名字 (如果 name 是数字)
        try:
            idx = int(name)
            if os.path.exists(COCO_LABELS):
                with open(COCO_LABELS) as f:
                    labels = [l.strip() for l in f if l.strip()]
                if 0 <= idx < len(labels):
                    d['name'] = labels[idx]
        except (ValueError, OSError):
            pass
        detections.append(d)
    info['detections'] = detections
    return info


# numpy is only needed for the YOLO post-processing path. Import lazily so the
# GUI still launches when numpy is absent (pure-Yocto without python3-numpy);
# the YOLO tab will show an install hint instead of crashing.
def _np():
    try:
        import numpy
        return numpy
    except ImportError:
        return None


_LB_CACHE = {}


def _lb_canvas(size, pad_value, top, left, nh, nw):
    """复用的 float32 letterbox 画布, 省掉每帧 1.2M 像素的 astype.

    摄像头 1280x720 缩到 640x360 只占中间一条, 每帧只写这块; 几何变了才整块铺
    pad 值. 之前每帧都 canvas.astype(float32)/255 全画布 (23ms), 现在只算有效
    区域 (~7ms), 输出逐字节相同.
    """
    np = _np()
    if np is None:
        raise RuntimeError("letterbox 需要 numpy")
    key = (size, pad_value)
    geo = (top, left, nh, nw)
    hit = _LB_CACHE.get(key)
    if hit is None or hit[0] != geo:
        canvas = np.full((size, size, 3), pad_value / 255.0, dtype=np.float32)
        _LB_CACHE[key] = (geo, canvas)
    else:
        canvas = hit[1]
    return canvas


def yolo_letterbox(image_path, size=640, pad_value=114):
    """Letterbox-resize an image to (size,size) using GdkPixbuf, return float32
    NCHW bytes (3,H,W order) plus (top, left, scale) for un-letterboxing.

    Preprocessing matches the Qualcomm AI Hub YOLOv7 export convention:
    RGB, /255.0, letterbox with constant pad=114, NCHW layout.
    GdkPixbuf already ships RGB 8-bit; we scale, pad, normalize and pack.
    """
    pixbuf = (image_path if isinstance(image_path, GdkPixbuf.Pixbuf)
              else GdkPixbuf.Pixbuf.new_from_file(image_path))
    iw, ih = pixbuf.get_width(), pixbuf.get_height()
    r = size / max(iw, ih)
    nw, nh = int(round(iw * r)), int(round(ih * r))
    scaled = pixbuf.scale_simple(nw, nh, GdkPixbuf.InterpType.BILINEAR)
    # 同样式的结果, 但走 numpy 整块拷贝: 原来的逐像素 Python 循环在
    # 640x640 上实测 ~160ms, 这里是几毫秒, 输出逐字节相同.
    np = _np()
    if np is None:
        raise RuntimeError("letterbox 需要 numpy")
    channels = 3
    top = (size - nh) // 2
    left = (size - nw) // 2
    rowstride = scaled.get_rowstride()
    snppb = scaled.get_n_channels()
    src = np.frombuffer(scaled.get_pixels(), dtype=np.uint8,
                        count=nh * rowstride)
    src = src.reshape(nh, rowstride)[:, :nw * snppb].reshape(nh, nw, snppb)
    canvas = _lb_canvas(size, pad_value, top, left, nh, nw)
    # 只把有效区域搬进画布并归一化, pad 区域留上帧/上次铺好的值
    np.divide(src[:, :, :channels], 255.0, out=canvas[top:top + nh, left:left + nw],
              casting="unsafe")
    # to float32 [0,1], NHWC
    # SNPE .dlc containers expect NHWC (channels-last) layout — the Qualcomm
    # AI Hub YOLOv7 export uses [1, 640, 640, 3].  Feeding NCHW scrambles
    # pixel data and produces hundreds of garbage grid-pattern detections
    # that look like "mirror duplicates" on the preview.
    arr = canvas.reshape(1, size, size, channels)                 # N,H,C,W
    return arr.tobytes(), (top, left, r), (iw, ih)


def run_yolo(model, image, runtime_flag="--use_dsp", graph_name="yolov7",
             input_size=640):
    """Run YOLO inference and decode detections.

    Returns (raw_text, out_path, detections) where detections is a list of
    dicts {name, left, top, right, bottom, conf} in original-image coords.

    For SNPE .dlc the full pipeline runs here:
      preprocess (letterbox ->raw) -> snpe-net-run --set_output_tensors ->
      load Result/*.raw -> conf filter -> NMS -> un-letterbox.
    For QNN .so we only run inference; detection decode is the same and is
    applied if the matching .raw files are produced.
    """
    log = logging.getLogger("npu_test")
    backend = model_backend(model)
    out_dir = tempfile.mkdtemp(prefix="npu_yolo_", dir="/tmp")

    if backend == "onnx_reference":
        return ("ERROR: .onnx 源模型无法在板上直接推理, "
                "需先用 qairt-converter 转成 .so\n"
                "请选 yolov7.dlc (SNPE 容器) 进行检测。", None, [], None)

    # ---- preprocess to raw (SNPE expects a float32 NCHW .raw input) ----
    np = _np()
    if np is None:
        return ("ERROR: YOLO 后处理需要 numpy, 但设备上未安装。\n"
                "请安装: apt-get install python3-numpy "
                "(Debian) 或在 Yocto 里加 python3-numpy", None, [], None)

    try:
        t_lb = time.monotonic()
        raw_bytes, (top, left, scale), (orig_w, orig_h) = \
            yolo_letterbox(image, input_size)
        lb_ms = (time.monotonic() - t_lb) * 1000.0
    except Exception as e:
        log.error(f"YOLO 预处理失败: {e}")
        return f"ERROR: 预处理失败: {e}", None, [], None

    log.info(f"YOLO 推理: {os.path.basename(model)} x {os.path.basename(image)} "
             f"backend={backend}")

    # 三档各自一个常驻会话, 端到端才可比: GPU 新建会话要 1.35s 编 OpenCL
    # kernel, DSP 也要读 DLC + 建 HTP 会话, 摊销掉这些才是纯推理差距.
    daemon = None
    if backend == "snpe":
        rt = ("gpu" if "gpu" in runtime_flag
              else "dsp" if "dsp" in runtime_flag else "cpu")
        daemon = get_daemon(model, rt)
    if daemon is not None and len(raw_bytes) == daemon.input_bytes:
        # 常驻会话: 省掉 进程启动 + 13.6MB DLC 解析 + 会话建立
        try:
            t_run = time.monotonic()
            infer_ms = daemon.infer(raw_bytes)
            run_ms = (time.monotonic() - t_run) * 1000.0
        except Exception as e:
            log.warning(f"npu-daemon 推理失败, 本帧回退 snpe-net-run: {e}")
            daemon = None
            run_ms = 0.0
        else:
            shutil.rmtree(out_dir, ignore_errors=True)   # 本帧没用到本地 tmp
            out_dir = daemon.dir
            out = (f"npu-daemon[{daemon.runtime}]: execute {infer_ms:.2f} ms "
                   f"(常驻会话, 无进程启动/图初始化开销)")

    if daemon is None:
        raw_path = os.path.join(out_dir, "image.raw")
        with open(raw_path, "wb") as f:
            f.write(raw_bytes)
        input_list = os.path.join(out_dir, "input_list.txt")
        with open(input_list, "w") as f:
            f.write(raw_path + "\n")

        if backend == "snpe":
            cmd = [SNPE_NET_RUN, f"--container={model}",
                   f"--input_list={input_list}", f"--output_dir={out_dir}",
                   f"--set_output_tensors={graph_name} boxes,scores,class_idx"]
            cmd.extend(runtime_flag.split())    # CPU 档 runtime_flag 为空
        else:  # qnn .so
            cmd = [QNN_NET_RUN, f"--model={model}",
                   f"--input_list={input_list}", f"--output_dir={out_dir}"]
            cmd.extend(runtime_flag.split())

        log.debug(f"CMD: {' '.join(cmd)}")
        try:
            t_run = time.monotonic()
            r = subprocess.run(cmd, capture_output=True, text=True,
                               timeout=120, cwd=out_dir)
            run_ms = (time.monotonic() - t_run) * 1000.0
            out = (r.stdout or "") + (r.stderr or "")
            if r.returncode != 0:
                log.warning(f"YOLO 返回码: {r.returncode}")
        except subprocess.TimeoutExpired:
            log.error("YOLO 推理超时(120s)")
            return "ERROR: 推理超时(120s)", None, [], None
        except Exception as e:
            log.error(f"YOLO 异常: {e}")
            return f"ERROR: {e}", None, [], None

    # ---- find output .raw (SNPE writes to Result_<n>/) ----
    # conf=0.5 + iou=0.45: SNPE quantized yolov7 emits many low-quality anchors;
    # a higher confidence cut + tighter NMS is more effective than aggressive
    # NMS alone.  iou lowered from 0.55 → 0.45 to suppress quantization-induced
    # duplicate boxes whose overlap falls in the 0.4-0.55 range.
    t_dec = time.monotonic()
    dets = _decode_yolo_raws(out_dir, top, left, scale,
                             orig_w, orig_h, conf=0.5, iou=0.45,
                             input_size=input_size)
    dec_ms = (time.monotonic() - t_dec) * 1000.0
    # 分项耗时进日志, 卡的时候看这一行就知道该优化哪一步
    log.info(f"YOLO 耗时: letterbox={lb_ms:.0f}ms infer={run_ms:.0f}ms "
             f"decode={dec_ms:.0f}ms ({'daemon' if daemon else 'cli'})")
    return out, None, dets, out_dir


def _decode_yolo_raws(out_dir, top, left, scale, orig_w, orig_h,
                      conf=0.25, iou=0.45, input_size=640):
    """Locate boxes/scores/class_idx .raw under out_dir, decode to detections
    in original-image coordinates. Returns a list of detection dicts."""
    np = _np()
    if np is None:
        return []
    # SNPE writes Result_<n>/boxes.raw [25200,4] f32,
    #                   Result_<n>/scores.raw [25200] f32,
    #                   Result_<n>/class_idx.raw [25200] f32 (stored as float)
    boxes = scores = class_idx = None
    for sub in sorted(glob.glob(os.path.join(out_dir, "**"), recursive=True)):
        if not os.path.isdir(sub):
            continue
        b = os.path.join(sub, "boxes.raw")
        s = os.path.join(sub, "scores.raw")
        c = os.path.join(sub, "class_idx.raw")
        if os.path.exists(b) and os.path.exists(s) and os.path.exists(c):
            boxes = np.fromfile(b, dtype=np.float32).reshape(-1, 4)
            scores = np.fromfile(s, dtype=np.float32)
            class_idx = np.fromfile(c, dtype=np.float32).astype(np.int64)
            break
    if boxes is None:
        # also check flat out_dir
        b = os.path.join(out_dir, "boxes.raw")
        s = os.path.join(out_dir, "scores.raw")
        c = os.path.join(out_dir, "class_idx.raw")
        if os.path.exists(b) and os.path.exists(s) and os.path.exists(c):
            boxes = np.fromfile(b, dtype=np.float32).reshape(-1, 4)
            scores = np.fromfile(s, dtype=np.float32)
            class_idx = np.fromfile(c, dtype=np.float32).astype(np.int64)
    if boxes is None:
        return []

    # confidence filter
    mask = scores > conf
    if not bool(mask.any()):
        return []
    boxes = boxes[mask]
    scores = scores[mask]
    class_idx = class_idx[mask]

    # the model emits xyxy in input-pixel space (0..input_size). Ensure monotonic.
    x1 = np.minimum(boxes[:, 0], boxes[:, 2])
    y1 = np.minimum(boxes[:, 1], boxes[:, 3])
    x2 = np.maximum(boxes[:, 0], boxes[:, 2])
    y2 = np.maximum(boxes[:, 1], boxes[:, 3])

    # Drop "ghost" boxes that fall in the letterbox padding region.
    # The quantized yolov7 head emits spurious anchors in the 114-gray
    # padding, often as y-mirror copies of real detections.  The old
    # filter only checked the top-left corner and skipped filtering
    # entirely when all boxes appeared "inside" — mirror ghosts whose
    # top-left leaked into the valid area slipped through.
    #
    # Fix: use box CENTROID.  A mirror ghost about the canvas center
    # places its centroid in the padding, so centroid-based filtering
    # catches it reliably.  Always apply the filter (no conditional).
    nw = int(round(orig_w * scale))
    nh = int(round(orig_h * scale))
    cx = (x1 + x2) / 2.0
    cy = (y1 + y2) / 2.0
    in_image = ((cx >= left) & (cx <= left + nw) &
                (cy >= top)  & (cy <= top + nh))
    if bool(in_image.any()):
        boxes = boxes[in_image]
        scores = scores[in_image]
        class_idx = class_idx[in_image]
        x1 = x1[in_image]
        y1 = y1[in_image]
        x2 = x2[in_image]
        y2 = y2[in_image]
    else:
        return []

    # per-class NMS: suppress only same-class overlaps (a common effect of
    # quantized YOLO heads is one object spawning multi-scale boxes; grouping
    # by class lets a person box and a bus box overlap without killing each other)
    keep_all = []
    unique_classes = np.unique(class_idx)
    for cls in unique_classes:
        cmask = class_idx == cls
        if not bool(cmask.any()):
            continue
        k = _nms(x1[cmask], y1[cmask], x2[cmask], y2[cmask],
                 scores[cmask], iou)
        # map local indices back to global
        global_idx = np.where(cmask)[0]
        for ki in k:
            keep_all.append(int(global_idx[ki]))
    keep = keep_all

    # Mirror-pair suppression: quantized yolov7 on SNPE/HTP frequently
    # produces detections that are approximate mirror images about the
    # canvas center (S/2, S/2).  These survive both the centroid ghost
    # filter (both copies can have centroids inside the valid region)
    # and per-class NMS (mirror copies don't spatially overlap, IoU ≈ 0).
    # Two conditions must hold for a pair to be considered a mirror:
    #   1) Centroid symmetry: cx_A + cx_B ≈ S and cy_A + cy_B ≈ S
    #   2) Mirror IoU: IoU(box_A, mirror(box_B)) > threshold
    # Condition (1) prevents false positives on distinct same-class objects
    # that happen to partially overlap with each other's mirror image.
    S = float(input_size)
    mirror_iou_thresh = 0.2
    centroid_tol = S * 0.15  # 96 px for 640 input
    suppressed = set()
    for i in range(len(keep)):
        if i in suppressed:
            continue
        for j in range(i + 1, len(keep)):
            if j in suppressed:
                continue
            if class_idx[keep[i]] != class_idx[keep[j]]:
                continue
            # condition 1: centroid symmetry about canvas center
            cxi = (x1[keep[i]] + x2[keep[i]]) / 2.0
            cyi = (y1[keep[i]] + y2[keep[i]]) / 2.0
            cxj = (x1[keep[j]] + x2[keep[j]]) / 2.0
            cyj = (y1[keep[j]] + y2[keep[j]]) / 2.0
            if (abs(float(cxi + cxj) - S) > centroid_tol or
                    abs(float(cyi + cyj) - S) > centroid_tol):
                continue
            # condition 2: mirror of box j about canvas center
            mx1 = S - x2[keep[j]]
            my1 = S - y2[keep[j]]
            mx2 = S - x1[keep[j]]
            my2 = S - y1[keep[j]]
            # IoU between box i and mirror of box j
            ix1 = max(float(x1[keep[i]]), mx1)
            iy1 = max(float(y1[keep[i]]), my1)
            ix2 = min(float(x2[keep[i]]), mx2)
            iy2 = min(float(y2[keep[i]]), my2)
            iw = max(0.0, ix2 - ix1)
            ih = max(0.0, iy2 - iy1)
            inter = iw * ih
            area_i = float((x2[keep[i]] - x1[keep[i]]) *
                           (y2[keep[i]] - y1[keep[i]]))
            area_m = (mx2 - mx1) * (my2 - my1)
            m_iou = inter / (area_i + area_m - inter + 1e-9)
            if m_iou > mirror_iou_thresh:
                # suppress the weaker one
                if scores[keep[i]] >= scores[keep[j]]:
                    suppressed.add(j)
                else:
                    suppressed.add(i)
                    break  # i suppressed, stop inner loop
    if suppressed:
        keep = [k for idx, k in enumerate(keep) if idx not in suppressed]

    dets = []
    labels = _load_coco_labels()
    for k in keep:
        bx1 = float((x1[k] - left) / scale)
        by1 = float((y1[k] - top) / scale)
        bx2 = float((x2[k] - left) / scale)
        by2 = float((y2[k] - top) / scale)
        bx1 = max(0, min(orig_w - 1, bx1))
        bx2 = max(0, min(orig_w - 1, bx2))
        by1 = max(0, min(orig_h - 1, by1))
        by2 = max(0, min(orig_h - 1, by2))
        c = int(class_idx[k])
        name = labels[c] if 0 <= c < len(labels) else f"cls{c}"
        dets.append({'name': name, 'left': int(bx1), 'top': int(by1),
                     'right': int(bx2), 'bottom': int(by2),
                     'conf': float(scores[k])})
    return dets


def _nms(x1, y1, x2, y2, scores, iou_thresh):
    """Pure-numpy NMS. Inputs are 1-D arrays of equal length; returns keep idx."""
    np = _np()
    areas = (x2 - x1) * (y2 - y1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(int(i))
        if order.size == 1:
            break
        rest = order[1:]
        xx1 = np.maximum(x1[i], x1[rest])
        yy1 = np.maximum(y1[i], y1[rest])
        xx2 = np.minimum(x2[i], x2[rest])
        yy2 = np.minimum(y2[i], y2[rest])
        w = np.maximum(0.0, xx2 - xx1)
        h = np.maximum(0.0, yy2 - yy1)
        inter = w * h
        ovr = inter / (areas[i] + areas[rest] - inter + 1e-9)
        inds = np.where(ovr <= iou_thresh)[0]
        order = rest[inds]
    return keep


def _load_coco_labels():
    labels = []
    try:
        if os.path.exists(COCO_LABELS):
            with open(COCO_LABELS) as f:
                labels = [l.strip() for l in f if l.strip()]
    except OSError:
        pass
    return labels


# ========== 日志系统 ==========
class GTKLogHandler(logging.Handler):
    """把日志写入 GTK TextView"""
    def __init__(self, textview):
        super().__init__()
        self.textview = textview

    def emit(self, record):
        msg = self.format(record)
        GLib.idle_add(self._append, msg)

    def _append(self, msg):
        buf = self.textview.get_buffer()
        end = buf.get_end_iter()
        buf.insert(end, msg + "\n")
        mark = buf.create_mark(None, buf.get_end_iter(), False)
        self.textview.scroll_to_mark(mark, 0.0, False, 0.0, 1.0)


def setup_logger(textview):
    os.makedirs(LOG_DIR, exist_ok=True)
    logger = logging.getLogger("npu_test")
    logger.setLevel(logging.DEBUG)
    # 清掉旧 handler (重开窗口时)
    logger.handlers.clear()

    gtk_handler = GTKLogHandler(textview)
    gtk_handler.setLevel(logging.INFO)
    gtk_handler.setFormatter(logging.Formatter(
        "%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S"))
    logger.addHandler(gtk_handler)

    log_file = os.path.join(LOG_DIR, f"qcom_npu_test_{datetime.datetime.now():%Y%m%d_%H%M%S}.log")
    fh = logging.FileHandler(log_file, encoding='utf-8')
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(logging.Formatter(
        "%(asctime)s [%(levelname)s] %(message)s"))
    logger.addHandler(fh)

    logger.log_file = log_file
    return logger


# ========== Qwen3.5 视觉请求 ==========
def vlm_probe(timeout=3.0):
    """探测 Qwen3.5 服务. 返回模型 id, 不可用返回 None."""
    try:
        with urllib.request.urlopen(VLM_MODELS, timeout=timeout) as r:
            data = json.loads(r.read().decode("utf-8", "replace"))
        items = data.get("data") or []
        return (items[0].get("id") if items else "ready")
    except Exception:
        return None


def vlm_ask_image(jpeg, prompt, max_tokens=VLM_MAX_TOKENS,
                  timeout=VLM_TIMEOUT):
    """把一张 JPEG 发给 Qwen3.5 视觉模型, 返回 (回答文本, 指标字典).

    服务端契约 (见 qwen35 的 request_frontend / qwen_server):
      - content 数组里放 {"type":"image_url","image_url":{"url":"data:..."}}
        和 {"type":"text","text":...}, 图片只允许 data URL, 单次只允许一张;
      - 尺寸由服务端按自己的视觉输入尺寸 letterbox, 所以这里传摄像头原生帧即可;
      - 回答在 choices[0].message.content, 耗时/速度在 qwen35_metrics 里。
    失败时抛异常, 由调用方分类展示。
    """
    payload = {
        "messages": [{"role": "user", "content": [
            {"type": "image_url",
             "image_url": {"url": "data:image/jpeg;base64,"
                                  + base64.b64encode(jpeg).decode("ascii")}},
            {"type": "text", "text": prompt},
        ]}],
        "max_tokens": int(max_tokens),
    }
    req = urllib.request.Request(
        VLM_ENDPOINT, data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = json.loads(resp.read().decode("utf-8", "replace"))

    choice = (data.get("choices") or [{}])[0]
    msg = choice.get("message") or {}
    text = (msg.get("content") or "").strip()
    if not text:
        # 模型可能只输出了推理段, 兜底拿它, 总比空白好
        text = (msg.get("reasoning_content") or "").strip() or "(模型没有输出内容)"

    usage = data.get("usage") or {}
    m = data.get("qwen35_metrics") or {}
    stages = m.get("stage_wall_seconds") or {}
    meta = {
        "prompt_tokens": usage.get("prompt_tokens", m.get("prompt_tokens")),
        "completion_tokens": usage.get("completion_tokens",
                                     m.get("completion_tokens")),
        "decode_tps": m.get("decode_tokens_per_second"),
        "mode": m.get("mode"),
        "finish_reason": choice.get("finish_reason"),
        "model": data.get("model"),
        # 服务端会把真正的耗时拆成阶段报回来, 用来把"中间过程"展示成时间轴
        "stages": stages if isinstance(stages, dict) else {},
        "vision_s": m.get("vision_run_seconds"),
        "prefill_s": m.get("npu_prefill_compute_seconds"),
        "prefill_tps": m.get("npu_prefill_compute_tokens_per_second"),
        "first_token_s": m.get("decoder_first_token_seconds"),
        "image_order": m.get("image_content_order"),
        "image": m.get("image") or {},
        "context_capacity_tokens": m.get("context_capacity_tokens"),
        "prefill_cached_tokens": m.get("prefill_cached_tokens"),
        "request_seconds": m.get("request_seconds"),
    }
    VLM_PROFILE.learn(meta)
    return text, meta


def mm_frame_luma(raw, step=997):
    """粗估一帧 RGB 的平均亮度 (0-255). 隔字节抽样, 不用 PIL/OpenCV.

    CamxBridge 刚建流时前几帧是全黑的, 而全黑帧送进去模型只会回一句
    "这张图片是空的/很模糊", 属于拿不到信息的调用. 这里就提前拦下来。
    """
    if not raw:
        return 0.0
    total = n = 0
    for i in range(0, len(raw), step):
        total += raw[i]
        n += 1
    return total / n if n else 0.0


def vlm_meta_line(meta, elapsed=None, jpeg_bytes=None):
    """把请求指标拼成一行给人看的摘要."""
    parts = []
    if elapsed is not None:
        parts.append(f"端到端 {elapsed:.1f}s")
    if jpeg_bytes:
        parts.append(f"图像 {jpeg_bytes / 1024:.0f} KB")
    if meta.get("prompt_tokens"):
        parts.append(f"prompt {meta['prompt_tokens']} tokens")
    if meta.get("completion_tokens"):
        parts.append(f"生成 {meta['completion_tokens']} tokens")
    if meta.get("decode_tps"):
        parts.append(f"解码 {meta['decode_tps']:.1f} tok/s")
    if meta.get("finish_reason"):
        parts.append(f"finish={meta['finish_reason']}")
    if meta.get("prefill_cached_tokens"):
        parts.append(f"缓存命中 {meta['prefill_cached_tokens']} tok")
    return " · ".join(parts)


def vlm_stage_rows(meta):
    """把服务端报的阶段耗时整理成 [(名称, 秒数, 补充说明)] , 供时间轴展示."""
    st = meta.get("stages") or {}
    rows = []
    if st.get("frontend"):
        wh = VLM_PROFILE.decoder_wh
        size = f"{wh[0]}x{wh[1]}" if wh else "服务端视觉输入尺寸"
        rows.append(("前端预处理", st["frontend"], f"解码图像 + letterbox 到 {size} + 分词"))
    if meta.get("vision_s"):
        it = VLM_PROFILE.image_tokens
        extra = "patch / body / merger 三阶段" + (f", {it} 个图像 token" if it else "")
        rows.append(("视觉编码 (HTP)", meta["vision_s"], extra))
    if st.get("npu_prefill") or meta.get("prefill_s"):
        sec = st.get("npu_prefill") or meta.get("prefill_s")
        extra = ""
        if meta.get("prompt_tokens"):
            extra = f"{meta['prompt_tokens']} tokens"
            if meta.get("prefill_tps"):
                extra += f", {meta['prefill_tps']:.0f} tok/s"
        cached = meta.get("prefill_cached_tokens") or 0
        if cached:
            # 同一张图的 prompt 前缀会缓存, 命中后预填充几乎不耗时
            extra = (extra + " · " if extra else "") + \
                    f"前缀缓存命中 {cached} tokens"
        rows.append(("预填充", sec, extra))
    if st.get("decoder"):
        extra = ""
        if meta.get("first_token_s"):
            extra = f"首 token {meta['first_token_s']:.2f}s"
        if meta.get("completion_tokens") and meta.get("decode_tps"):
            extra += (" · " if extra else "") + \
                     f"{meta['completion_tokens']} tokens @ {meta['decode_tps']:.1f} tok/s"
        rows.append(("解码生成", st["decoder"], extra))
    return rows


# ========== GUI ==========
class NPUTestWindow(Gtk.Window):
    def __init__(self):
        super().__init__(title="Qualcomm NPU 测试工具 (QCS6490)")
        self.set_default_size(1000, 740)
        self.set_position(Gtk.WindowPosition.CENTER)

        vbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        vbox.set_margin_start(10)
        vbox.set_margin_end(10)
        vbox.set_margin_top(10)
        vbox.set_margin_bottom(10)
        self.add(vbox)

        # 标题
        title = Gtk.Label()
        title.set_markup('<span size="xx-large" weight="bold">🧪 Qualcomm NPU 测试工具</span>\n'
                         '<span size="small" foreground="gray">QCS6490 · Hexagon DSP/HTP · SNPE/QNN SDK</span>')
        vbox.pack_start(title, False, False, 0)

        # 设备信息栏
        info_bar = Gtk.Frame(label="设备信息")
        info_grid = Gtk.Grid(column_spacing=12, row_spacing=2)
        info_grid.set_margin_start(6)
        info_grid.set_margin_end(6)
        info_grid.set_margin_top(4)
        info_grid.set_margin_bottom(4)
        info_bar.add(info_grid)
        self.lbl_soc = Gtk.Label(xalign=0)
        self.lbl_runtime = Gtk.Label(xalign=0)
        self.lbl_runtime.set_markup('<span foreground="gray">正在探测 NPU...</span>')
        info_grid.attach(Gtk.Label(label="SoC:", xalign=0), 0, 0, 1, 1)
        info_grid.attach(self.lbl_soc, 1, 0, 1, 1)
        info_grid.attach(Gtk.Label(label="NPU/Runtime:", xalign=0), 0, 1, 1, 1)
        info_grid.attach(self.lbl_runtime, 1, 1, 1, 1)
        vbox.pack_start(info_bar, False, False, 0)

        # Tab 容器
        self.notebook = Gtk.Notebook()
        vbox.pack_start(self.notebook, True, True, 0)
        # 切页时自动开关多模态预览: 进页就取流, 离页就释放 /dev/video10
        self.notebook.connect("switch-page", self.on_notebook_switch_page)

        # 摄像头状态: 取流线程/推理线程/绘制线程三方共享, 用条件变量传递最新帧
        self._cam_on = False
        self._cam_proc = None
        self._cam_frame = None
        self._cam_raw = None
        self._cam_seq = 0
        self._cam_lock = threading.Lock()
        self._cam_cond = threading.Condition()

        # 多模态预览状态: 只取流不推理, 单线程就能跟上摄像头
        self._mm_on = False
        self._mm_busy = False          # 正在等 Qwen3.5 回答
        self._mm_ready = False         # 已过预热帧数, 可以识别
        self._mm_proc = None
        self._mm_frame = None
        self._mm_raw = None
        self._mm_fail_streak = 0        # 连续取流失败次数, 用于判断要不要提示手动重置
        self._mm_lock = threading.Lock()

        self.build_validation_tab()
        self.build_inference_tab()
        self.build_yolo_tab()
        self.build_camera_tab()
        self.build_multimodal_tab()
        self.build_throughput_tab()

        # 开界面就把常驻会话拉起来, 不分 Tab: 第一次点任何页的按钮都只付推理时间
        self.warm_sessions()

        # ===== 日志面板 =====
        log_frame = Gtk.Frame(label="运行日志")
        log_hbox = Gtk.Box(spacing=4)
        log_hbox.set_margin_start(4)
        log_hbox.set_margin_end(4)
        log_hbox.set_margin_top(2)
        log_hbox.set_margin_bottom(2)
        log_frame.add(log_hbox)

        log_sw = Gtk.ScrolledWindow()
        log_sw.set_size_request(-1, 120)
        log_sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        self.log_textview = Gtk.TextView()
        self.log_textview.set_editable(False)
        self.log_textview.set_monospace(True)
        self.log_textview.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.log_textview.modify_font(Pango.FontDescription("monospace 9"))
        log_sw.add(self.log_textview)
        log_hbox.pack_start(log_sw, True, True, 0)

        log_btn_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        btn_clear = Gtk.Button(label="清空")
        btn_clear.set_size_request(60, -1)
        btn_clear.connect("clicked", lambda b: self.log_textview.get_buffer().set_text(""))
        log_btn_box.pack_start(btn_clear, False, False, 0)
        btn_save = Gtk.Button(label="保存")
        btn_save.set_size_request(60, -1)
        btn_save.connect("clicked", self.on_save_log)
        log_btn_box.pack_start(btn_save, False, False, 0)
        log_hbox.pack_start(log_btn_box, False, False, 0)

        vbox.pack_start(log_frame, False, False, 0)

        # 初始化日志
        self.logger = setup_logger(self.log_textview)
        self.logger.info("Qualcomm NPU 测试工具启动")
        self.logger.info(f"日志文件: {self.logger.log_file}")

        # 状态栏
        self.statusbar = Gtk.Label(xalign=0)
        self.statusbar.set_markup('<span foreground="gray">就绪</span>')
        vbox.pack_start(self.statusbar, False, False, 0)

        # 异步加载设备信息
        self.load_device_info()

    # ---------- 设备信息 ----------
    def load_device_info(self):
        def _load():
            soc = "?"
            kernel = "?"
            try:
                r = subprocess.run(["uname", "-r"], capture_output=True, text=True, timeout=5)
                kernel = r.stdout.strip()
            except:
                pass
            try:
                with open("/sys/devices/soc0/machine") as f:
                    soc = f.read().strip()
            except:
                # fallback
                try:
                    r = subprocess.run(["cat", "/proc/cpuinfo"], capture_output=True, text=True, timeout=5)
                    m = re.search(r'Hardware\s*:\s*(\S+)', r.stdout)
                    if m:
                        soc = m.group(1)
                except:
                    pass

            # 快速验证 NPU: 跑 snpe dsp platform-validator (快, 不需要模型)
            npu_status = "❓ 未知"
            try:
                if os.path.exists(SNPE_PF_VALIDATOR):
                    r = subprocess.run([SNPE_PF_VALIDATOR, "--runtime", "dsp", "--testRuntime"],
                                       capture_output=True, text=True, timeout=30)
                    out = (r.stdout or "") + (r.stderr or "")
                    if "Unit Test on the runtime DSP: Passed" in out:
                        npu_status = "✅ DSP/HTP 可用 (NPU 正常)"
                    elif "not available" in out.lower() or "Absent" in out:
                        npu_status = "❌ DSP/HTP 不可用"
                    else:
                        npu_status = "⚠️ DSP 状态未知"
                else:
                    npu_status = "❌ snpe-platform-validator 未安装"
            except subprocess.TimeoutExpired:
                npu_status = "⚠️ DSP 验证超时"
            except Exception as e:
                npu_status = f"❌ 验证失败: {e}"

            GLib.idle_add(lambda: self.lbl_soc.set_text(f"{soc}  |  内核: {kernel}"))
            GLib.idle_add(lambda: self.lbl_runtime.set_markup(
                f'<span foreground="green">{npu_status}</span>'))
            self.logger.info(f"设备: {soc}, 内核: {kernel}, NPU: {npu_status}")
        threading.Thread(target=_load, daemon=True).start()

    # ========== Tab1: 平台验证 ==========
    def build_validation_tab(self):
        tab = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        tab.set_margin_start(8)
        tab.set_margin_end(8)
        tab.set_margin_top(8)
        self.notebook.append_page(tab, Gtk.Label(label="🔍 平台验证"))

        desc = Gtk.Label(xalign=0)
        desc.set_markup('<span size="small" foreground="gray">'
                        '调用 snpe/qnn-platform-validator 验证 DSP(HTP/NPU)、GPU runtime 可用性</span>')
        tab.pack_start(desc, False, False, 0)

        # 工具选择
        sel = Gtk.Box(spacing=10)
        tab.pack_start(sel, False, False, 0)
        sel.pack_start(Gtk.Label(label="验证工具:"), False, False, 0)
        self.cmb_pf_tool = Gtk.ComboBoxText()
        self.cmb_pf_tool.append_text("SNPE platform-validator (dsp/gpu)")
        self.cmb_pf_tool.append_text("QNN platform-validator (dsp/gpu)")
        self.cmb_pf_tool.set_active(0)
        sel.pack_start(self.cmb_pf_tool, False, False, 0)

        self.chk_version = Gtk.CheckButton(label="同时查询版本信息 (--coreVersion --libVersion)")
        sel.pack_start(self.chk_version, False, False, 0)

        self.btn_validate = Gtk.Button(label="▶ 开始验证")
        self.btn_validate.get_style_context().add_class("suggested-action")
        self.btn_validate.connect("clicked", self.on_validate_clicked)
        sel.pack_end(self.btn_validate, False, False, 0)

        # 结果区
        result_frame = Gtk.Frame(label="验证结果")
        sw = Gtk.ScrolledWindow()
        self.pf_result_text = Gtk.TextView()
        self.pf_result_text.set_editable(False)
        self.pf_result_text.set_monospace(True)
        self.pf_result_text.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.pf_result_text.get_buffer().set_text(
            "点击 [开始验证] 检测 NPU (DSP/HTP) 平台可用性...\n\n"
            "SNPE 验证项:\n"
            "  --runtime dsp   Hexagon DSP/HTP (即 NPU)\n"
            "  --runtime gpu   Adreno GPU (需要 OpenCL)\n\n"
            "QNN 验证项:\n"
            "  --backend dsp   QNN DSP 后端\n"
            "  --backend gpu   QNN GPU 后端\n")
        sw.add(self.pf_result_text)
        result_frame.add(sw)
        tab.pack_start(result_frame, True, True, 0)

    def on_validate_clicked(self, button):
        tool = self.cmb_pf_tool.get_active()
        want_ver = self.chk_version.get_active()
        self.btn_validate.set_sensitive(False)
        self.btn_validate.set_label("⏳ 验证中...")

        def run():
            results = {}
            if tool == 0:
                # SNPE: 测 dsp + gpu
                for rt in ("dsp", "gpu"):
                    cmd = [SNPE_PF_VALIDATOR, "--runtime", rt, "--testRuntime"]
                    if want_ver:
                        cmd += ["--coreVersion", "--libVersion"]
                    self.logger.info(f"SNPE 验证 runtime={rt}")
                    out, rc = run_cmd(cmd, timeout=60)
                    results[rt] = (out, rc, parse_snpe_pf(out))
                GLib.idle_add(self.on_validate_done, "snpe", results)
            else:
                # QNN: 测 dsp + gpu
                for bk in ("dsp", "gpu"):
                    cmd = [QNN_PF_VALIDATOR, "--backend", bk, "--testBackend"]
                    if want_ver:
                        cmd += ["--coreVersion", "--libVersion"]
                    self.logger.info(f"QNN 验证 backend={bk}")
                    out, rc = run_cmd(cmd, timeout=60)
                    results[bk] = (out, rc, parse_qnn_pf(out))
                GLib.idle_add(self.on_validate_done, "qnn", results)
        threading.Thread(target=run, daemon=True).start()

    def on_validate_done(self, tool, results):
        self.btn_validate.set_sensitive(True)
        self.btn_validate.set_label("▶ 开始验证")
        buf = self.pf_result_text.get_buffer()
        buf.set_text("")

        def a(text):
            buf.insert(buf.get_end_iter(), text)

        a(f"{'='*55}\n  🔍 NPU 平台验证结果 ({tool.upper()})\n{'='*55}\n\n")
        all_ok = True
        for key in results:
            out, rc, info = results[key]
            a(f"{'─'*55}\n  [{key.upper()}]\n{'─'*55}\n")
            # 状态
            if tool == "snpe":
                status = info.get('runtimes', {}).get(key, 'Unknown')
            else:
                status = info.get('backends', {}).get(key, 'Unknown')
            if status in ('Passed', 'Present'):
                icon = "✅"
            elif status in ('Failed', 'Absent', 'Not Available') or 'Absent' in str(status):
                icon = "❌"
                all_ok = False
            else:
                icon = "⚠️"
            a(f"  状态: {icon} {status}\n")
            if 'prereq' in info and key in info['prereq']:
                a(f"  前置条件: {info['prereq'][key]}\n")
            if 'summary' in info:
                for k, v in info['summary'].items():
                    a(f"  {k}: {v}\n")
            a(f"  返回码: {rc}\n\n")
            a(f"  --- 原始输出 ---\n{out}\n\n")

        status_text = "✅ 全部通过" if all_ok else "⚠️ 部分不可用"
        self.statusbar.set_markup(f'<span foreground="{"green" if all_ok else "orange"}">{status_text}</span>')
        self.logger.info(f"平台验证完成: {status_text}")

    # ========== Tab2: 单次推理 ==========
    def build_inference_tab(self):
        tab = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        tab.set_margin_start(8)
        tab.set_margin_end(8)
        tab.set_margin_top(8)
        self.notebook.append_page(tab, Gtk.Label(label="🧠 单次推理"))

        desc = Gtk.Label(xalign=0)
        desc.set_markup('<span size="small" foreground="gray">'
                        '加载 .dlc(snpe-net-run)/.so(qnn-net-run) 模型做单次推理, 测试 CPU/GPU/DSP(NPU) 各 runtime 耗时. '
                        '.onnx 源模型需先离线转换为 .so</span>')
        tab.pack_start(desc, False, False, 0)

        # 选择区
        sel = Gtk.Box(spacing=10)
        tab.pack_start(sel, False, False, 0)

        # 模型
        mb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        mb.pack_start(Gtk.Label(label="模型:", xalign=0), False, False, 0)
        self.cmb_model = Gtk.ComboBoxText()
        self.models = find_models()
        self._refresh_model_combo()
        mb.pack_start(self.cmb_model, False, False, 0)
        sel.pack_start(mb, True, True, 0)

        # 图片
        ib = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        ib.pack_start(Gtk.Label(label="测试图片:", xalign=0), False, False, 0)
        img_hbox = Gtk.Box(spacing=4)
        self.cmb_image = Gtk.ComboBoxText()
        self.images = find_images()
        for img in self.images:
            self.cmb_image.append_text(os.path.basename(img))
        if self.images:
            self.cmb_image.set_active(0)
        self.cmb_image.connect("changed",
            lambda c: self.load_image_preview(self.cmb_image, self.images, self.inf_img_widget))
        img_hbox.pack_start(self.cmb_image, True, True, 0)
        btn_upload = Gtk.Button(label="📁")
        btn_upload.set_tooltip_text("上传本地图片")
        btn_upload.set_size_request(32, -1)
        btn_upload.connect("clicked",
            lambda b: self.on_upload_image(self.cmb_image, self.images, self.inf_img_widget))
        img_hbox.pack_start(btn_upload, False, False, 0)
        ib.pack_start(img_hbox, False, False, 0)
        sel.pack_start(ib, True, True, 0)

        # Runtime 选择
        rb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        rb.pack_start(Gtk.Label(label="Runtime:", xalign=0), False, False, 0)
        self.cmb_runtime = Gtk.ComboBoxText()
        self.cmb_runtime.append_text("DSP (NPU/HTP)")
        self.cmb_runtime.append_text("GPU (Adreno)")
        self.cmb_runtime.append_text("CPU")
        self.cmb_runtime.set_active(0)
        rb.pack_start(self.cmb_runtime, False, False, 0)
        sel.pack_start(rb, False, False, 0)

        # 按钮
        bbtn = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        bbtn.pack_start(Gtk.Label(label=""), False, False, 0)
        self.btn_infer = Gtk.Button(label="▶ 开始推理")
        self.btn_infer.get_style_context().add_class("suggested-action")
        self.btn_infer.connect("clicked", self.on_infer_clicked)
        bbtn.pack_start(self.btn_infer, False, False, 0)
        sel.pack_start(bbtn, False, False, 0)

        # 内容: 左图片右结果
        paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        tab.pack_start(paned, True, True, 0)

        img_frame = Gtk.Frame(label="输入图片")
        self.inf_img_widget = Gtk.Image()
        self.inf_img_widget.set_size_request(280, 280)
        img_frame.add(self.inf_img_widget)
        paned.pack1(img_frame, resize=False, shrink=False)

        result_frame = Gtk.Frame(label="推理结果")
        sw = Gtk.ScrolledWindow()
        self.inf_result_text = Gtk.TextView()
        self.inf_result_text.set_editable(False)
        self.inf_result_text.set_monospace(True)
        self.inf_result_text.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.inf_result_text.get_buffer().set_text("选择模型和图片, 点击 [开始推理]...\n\n"
            "模型类型说明:\n"
            "  .dlc  -> snpe-net-run (SNPE), 板上可直接推理 ✓\n"
            "  .so   -> qnn-net-run (QNN context binary), 可直接推理 ✓\n"
            "  .onnx -> AI Hub 源模型, 需在 PC 上用 qairt-converter\n"
            "          转成 .so 后才能用 qnn-net-run 跑\n\n"
            "自带 yolov7.dlc 开箱即用; yolov7_onnx 为源模型参考。")
        sw.add(self.inf_result_text)
        result_frame.add(sw)
        paned.pack2(result_frame, resize=True, shrink=False)

        GLib.idle_add(lambda: self.load_image_preview(self.cmb_image, self.images, self.inf_img_widget))

    def _refresh_model_combo(self):
        self.cmb_model.remove_all()
        self.models = find_models()
        for m in self.models:
            # 子目录里的模型 (如 yolov7_onnx/model.onnx) 用父目录名区分
            parent = os.path.basename(os.path.dirname(m))
            if parent and parent != os.path.basename(m):
                self.cmb_model.append_text(f"{parent}/{os.path.basename(m)}")
            else:
                self.cmb_model.append_text(os.path.basename(m))
        if self.models:
            self.cmb_model.set_active(0)

    def on_infer_clicked(self, button):
        mi = self.cmb_model.get_active()
        ii = self.cmb_image.get_active()
        if mi < 0:
            self.show_error("请选择模型\n(将 .dlc/.onnx 文件放到 /usr/share/model/ 后点刷新)")
            return
        if ii < 0:
            self.show_error("请选择测试图片")
            return
        model = self.models[mi]
        image = self.images[ii]
        rt_idx = self.cmb_runtime.get_active()
        backend = model_backend(model)
        runtime_name = ["DSP (NPU/HTP)", "GPU (Adreno)", "CPU"][rt_idx]

        if backend == "onnx_reference":
            self.show_error(
                "此 .onnx 是 Qualcomm AI Hub 源模型, 无法在板上直接推理。\n"
                "需在 PC 上用 qairt-converter (qnn-onnx-converter) 转成\n"
                "QNN context binary (.so) 后, 用 qnn-net-run --model 运行。\n\n"
                "如需在板上直接测试, 请选 yolov7.dlc (SNPE 容器)。")
            return
        if backend == "snpe":
            if not os.path.exists(SNPE_NET_RUN):
                self.show_error(f"snpe-net-run 不存在: {SNPE_NET_RUN}\n请确认 qcom-snpe-sdk 已安装")
                return
            runtime_flag = ["--use_dsp", "--use_gpu", ""][rt_idx]
        else:  # qnn (.so)
            if not os.path.exists(QNN_NET_RUN):
                self.show_error(f"qnn-net-run 不存在: {QNN_NET_RUN}\n请确认 qcom-qnn-sdk 已安装")
                return
            runtime_flag = ["--backend libQnnHtp.so",
                            "--backend libQnnGpu.so",
                            "--backend libQnnCpu.so"][rt_idx]

        self.btn_infer.set_sensitive(False)
        self.btn_infer.set_label("⏳ 推理中...")
        self.logger.info(f"推理: model={os.path.basename(model)} image={os.path.basename(image)} "
                         f"backend={backend} runtime={runtime_name}")

        def run():
            out_dir = tempfile.mkdtemp(prefix="npu_out_", dir="/tmp")
            import time as _time

            # snpe-net-run / qnn-net-run only accept raw binary input,
            # not jpg/png.  Preprocess the image the same way the YOLO
            # tab does: letterbox → float32 [0,1] NHWC → .raw file.
            np = _np()
            raw_bytes = None
            input_file = image
            if np is not None:
                try:
                    raw_bytes, _, _ = yolo_letterbox(image, 640)
                    raw_path = os.path.join(out_dir, "image.raw")
                    with open(raw_path, "wb") as f:
                        f.write(raw_bytes)
                    input_file = raw_path
                except Exception as e:
                    self.logger.warning(f"图片预处理失败, 回退原始文件: {e}")

            # 常驻会话优先: 和 YOLO 页共用同一个 npu-daemon, 点一下只付推理时间.
            # 起不来(或模型尺寸不是 640)就回退 snpe-net-run.
            if backend == "snpe" and raw_bytes:
                r = daemon_run(model, ["dsp", "gpu", "cpu"][rt_idx], raw_bytes)
                if r is not None:
                    d, ms = r
                    shutil.rmtree(out_dir, ignore_errors=True)
                    lat = {"avg_ms": ms, "total_ms": ms, "clock_mhz": 0.0}
                    out = (f"npu-daemon[{d.runtime}]: execute {ms:.2f} ms "
                           f"(常驻会话, 无进程启动/图初始化开销)")
                    GLib.idle_add(self.on_infer_done, out, 0, model, image,
                                  runtime_name, d.dir, backend, ms, lat)
                    return

            input_list = os.path.join(out_dir, "input_list.txt")
            with open(input_list, "w") as f:
                f.write(input_file + "\n")

            if backend == "snpe":
                cmd = [SNPE_NET_RUN, f"--container={model}",
                       f"--input_list={input_list}", f"--output_dir={out_dir}"]
                if runtime_flag:
                    cmd.append(runtime_flag)
            else:
                # QNN: qnn-net-run
                cmd = [QNN_NET_RUN, f"--model={model}",
                       f"--input_list={input_list}", f"--output_dir={out_dir}"]
                cmd.extend(runtime_flag.split())
            import time as _time
            t0 = _time.monotonic()
            out, rc = run_cmd(cmd, timeout=120)
            call_ms = (_time.monotonic() - t0) * 1000.0
            # 推理延迟直接从本次运行的 SNPEDiag.log 读, 不再另跑一个进程.
            lat = measure_npu_latency(model, out_dir)
            GLib.idle_add(self.on_infer_done, out, rc, model, image, runtime_name, out_dir, backend, call_ms, lat)
        threading.Thread(target=run, daemon=True).start()

    def on_infer_done(self, raw, rc, model, image, runtime, out_dir, backend="snpe", call_ms=0, lat=None):
        self.btn_infer.set_sensitive(True)
        self.btn_infer.set_label("▶ 开始推理")
        info = parse_inference(raw)
        buf = self.inf_result_text.get_buffer()
        buf.set_text("")

        def a(text):
            buf.insert(buf.get_end_iter(), text)

        a(f"{'='*55}\n  🧠 {backend.upper()} 单次推理结果\n{'='*55}\n\n")
        a(f"模型: {os.path.basename(model)}\n")
        a(f"图片: {os.path.basename(image)}\n")
        a(f"Runtime: {runtime}\n")
        a(f"返回码: {rc}\n\n")
        if 'build_ver' in info:
            a(f"SNPE 版本: {info['build_ver']}\n")
        if 'model_name' in info:
            a(f"模型名: {info['model_name']}\n")
        if 'status' in info:
            a(f"执行状态: {info['status']}\n")
        # 耗时统计: NPU 延迟取自本次运行的 SNPEDiag.log (snpe-diagview),
        # 端到端耗时是真正点一次要等的时间 (预处理 + snpe-net-run + 后处理).
        a(f"\n{'─'*55}\n  ⚡ 耗时统计\n{'─'*55}\n")
        # 拆成两行, 否则单帧冷跑的 Total 会被误当成"NPU 变慢了":
        #   NPU计算 = DSP 上跑网络的时间 (转速随 DVFS 变, 单帧 ~0.9GHz / 满载 ~1.4GHz)
        #   单次推理 = 计算 + 4.9MB 输入和 3 路输出张量的 fastrpc 搬运
        npu_ms = (lat or {}).get('avg_ms', 0)
        tot_ms = (lat or {}).get('total_ms', 0)
        a(fmt_timing(lat, call_ms, runtime))
        if 'avg_layer_ms' in info:
            a(f"平均层耗时: {info['avg_layer_ms']:.2f} ms\n")
        if 'max_ms' in info:
            a(f"最慢单层: {info['max_ms']:.2f} ms\n")
        if 'fps' in info:
            a(f"FPS: {info['fps']:.1f}\n")
        a(f"\n{'─'*55}\n  📋 原始输出\n{'─'*55}\n{raw}")
        # 输出目录信息
        try:
            outs = os.listdir(out_dir)
            if outs:
                a(f"\n{'─'*55}\n  📂 输出文件 ({out_dir})\n{'─'*55}\n")
                for f in sorted(outs):
                    p = os.path.join(out_dir, f)
                    a(f"  {f}  ({os.path.getsize(p)} bytes)\n")
        except:
            pass
        a(f"\n输出目录: {out_dir}\n")

        # 状态栏优先显示 NPU 纯推理延迟, 没有则回退调用总耗时
        sb_ms = npu_ms if npu_ms > 0 else (info.get('total_ms', 0) or call_ms)
        status = info.get('status', '完成' if rc == 0 else '失败')
        if sb_ms > 0:
            self.statusbar.set_markup(
                f'<span foreground="{"green" if rc==0 else "red"}">'
                f'{"✅" if rc==0 else "❌"} 推理{status} - {sb_ms:.2f}ms / {1000.0/sb_ms:.1f} FPS</span>')
        else:
            self.statusbar.set_markup(
                f'<span foreground="{"green" if rc==0 else "red"}">{"✅" if rc==0 else "❌"} 推理{status}</span>')
        self.logger.info(f"推理完成: rc={rc}, NPU={npu_ms:.2f}ms, 调用={call_ms:.0f}ms")

    # ========== Tab YOLO: 目标检测 ==========
    def build_yolo_tab(self):
        tab = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        tab.set_margin_start(8)
        tab.set_margin_end(8)
        tab.set_margin_top(8)
        self.notebook.append_page(tab, Gtk.Label(label="🎯 YOLO检测"))

        desc = Gtk.Label(xalign=0)
        desc.set_markup('<span size="small" foreground="gray">'
                        '加载 YOLO .dlc(snpe)/.so(qnn) 模型做目标检测, 解析检测框并渲染到图片上. '
                        '自带 yolov7.dlc 可直接检测; yolov7_onnx 为源模型需先转换</span>')
        tab.pack_start(desc, False, False, 0)

        # 选择区
        sel = Gtk.Box(spacing=10)
        tab.pack_start(sel, False, False, 0)

        # YOLO 模型
        mb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        mb.pack_start(Gtk.Label(label="YOLO 模型:", xalign=0), False, False, 0)
        self.cmb_yolo_model = Gtk.ComboBoxText()
        self._yolo_models = []
        self._refresh_yolo_models()
        btn_refresh_yolo = Gtk.Button(label="🔄")
        btn_refresh_yolo.set_tooltip_text("刷新模型列表")
        btn_refresh_yolo.set_size_request(32, -1)
        btn_refresh_yolo.connect("clicked", lambda b: self._refresh_yolo_models())
        ym_hbox = Gtk.Box(spacing=4)
        ym_hbox.pack_start(self.cmb_yolo_model, True, True, 0)
        ym_hbox.pack_start(btn_refresh_yolo, False, False, 0)
        mb.pack_start(ym_hbox, False, False, 0)
        sel.pack_start(mb, True, True, 0)

        # 图片
        ib = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        ib.pack_start(Gtk.Label(label="测试图片:", xalign=0), False, False, 0)
        yimg_hbox = Gtk.Box(spacing=4)
        self.cmb_yolo_image = Gtk.ComboBoxText()
        self._yolo_images = find_images()
        for img in self._yolo_images:
            self.cmb_yolo_image.append_text(os.path.basename(img))
        # 默认选 bus.jpg
        for i, img in enumerate(self._yolo_images):
            if "bus" in os.path.basename(img):
                self.cmb_yolo_image.set_active(i)
                break
        else:
            if self._yolo_images:
                self.cmb_yolo_image.set_active(0)
        self.cmb_yolo_image.connect("changed",
            lambda c: self.load_image_preview(self.cmb_yolo_image, self._yolo_images, self.yolo_img_widget))
        yimg_hbox.pack_start(self.cmb_yolo_image, True, True, 0)
        btn_yupload = Gtk.Button(label="📁")
        btn_yupload.set_tooltip_text("上传本地图片")
        btn_yupload.set_size_request(32, -1)
        btn_yupload.connect("clicked",
            lambda b: self.on_upload_image(self.cmb_yolo_image, self._yolo_images, self.yolo_img_widget))
        yimg_hbox.pack_start(btn_yupload, False, False, 0)
        ib.pack_start(yimg_hbox, False, False, 0)
        sel.pack_start(ib, True, True, 0)

        # Runtime
        rb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        rb.pack_start(Gtk.Label(label="Runtime:", xalign=0), False, False, 0)
        self.cmb_yolo_runtime = Gtk.ComboBoxText()
        for r in ("DSP (NPU/HTP)", "GPU (Adreno)", "CPU"):
            self.cmb_yolo_runtime.append_text(r)
        self.cmb_yolo_runtime.set_active(0)
        rb.pack_start(self.cmb_yolo_runtime, False, False, 0)
        sel.pack_start(rb, False, False, 0)

        # 按钮
        bbtn = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        bbtn.pack_start(Gtk.Label(label=""), False, False, 0)
        self.btn_yolo = Gtk.Button(label="▶ 开始检测")
        self.btn_yolo.get_style_context().add_class("suggested-action")
        self.btn_yolo.connect("clicked", self.on_yolo_clicked)
        bbtn.pack_start(self.btn_yolo, False, False, 0)

        # 一键量三档: 同一张图跑 CPU / GPU / NPU, 把 delay 差摆在面板上
        self.btn_cmp = Gtk.Button(label="⚡ 三档对比")
        self.btn_cmp.set_tooltip_text(
            "同一模型同一张图依次跑 CPU / Adreno GPU / HTP(NPU), 用实测延迟对比")
        self.btn_cmp.connect("clicked", self.on_compare_clicked)
        bbtn.pack_start(self.btn_cmp, False, False, 0)

        # 摄像头在独立 Tab 里看(占满窗口, 不跟静态图抢地方)
        self.btn_cam = Gtk.Button(label="📷 摄像头实时识别")
        self.btn_cam.set_tooltip_text("/dev/video10 (CamxBridge) 实时画面 + YOLO 叠框")
        self.btn_cam.connect("clicked", self.on_cam_clicked)
        bbtn.pack_start(self.btn_cam, False, False, 0)
        sel.pack_start(bbtn, False, False, 0)

        # 内容: 左图右结果
        paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
        tab.pack_start(paned, True, True, 0)

        img_frame = Gtk.Frame(label="图片 (输入/输出)")
        self.yolo_img_widget = Gtk.Image()
        self.yolo_img_widget.set_size_request(320, 320)
        img_frame.add(self.yolo_img_widget)
        paned.pack1(img_frame, resize=False, shrink=False)

        result_frame = Gtk.Frame(label="检测结果")
        sw = Gtk.ScrolledWindow()
        self.yolo_result_text = Gtk.TextView()
        self.yolo_result_text.set_editable(False)
        self.yolo_result_text.set_monospace(True)
        self.yolo_result_text.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.yolo_result_text.get_buffer().set_text(
            "本工具自带 YOLOv7 模型:\n\n"
            "  yolov7.dlc        SNPE 容器, 板上可直接检测 ✓\n"
            "  yolov7_onnx/      AI Hub ONNX 源模型, 需在 PC 上用\n"
            "                    qairt-converter 转成 .so 后才能跑\n\n"
            "选择 yolov7.dlc + 图片 + runtime, 点 [开始检测]即可。\n"
            "也可放入自己的 .dlc / .so 模型到\n"
            "/usr/share/qcom-npu-test/models/ 后点 🔄 刷新。")
        sw.add(self.yolo_result_text)
        result_frame.add(sw)
        paned.pack2(result_frame, resize=True, shrink=False)

        GLib.idle_add(lambda: self.load_image_preview(
            self.cmb_yolo_image, self._yolo_images, self.yolo_img_widget))

    # ---------- 常驻会话预热 ----------
    def warm_sessions(self):
        """开界面就把常驻会话拉起来, 不管用户先点哪个 Tab.

        CPU 档只建会话 ~0.1s 却要多占 ~200MB (每层都留 float buffer), 不常驻,
        第一次用到时再起, 起来后一直留着.
        """
        log = logging.getLogger("npu_test")
        warm = os.path.join(MODEL_DIRS[0], "yolov7.dlc")
        if not os.path.exists(warm):
            return
        def _warm():
            for rt in ("dsp", "gpu"):
                t0 = time.monotonic()
                d = get_daemon(warm, rt)
                log.info(f"常驻会话 {rt}: "
                         + (f"就绪 {time.monotonic() - t0:.2f}s" if d else "启动失败"))
        threading.Thread(target=_warm, daemon=True).start()

    # ---------- 摄像头实时预览 ----------
    def on_cam_clicked(self, btn):
        if self._cam_on:
            self._cam_stop()
            return
        if self._mm_on:
            # 同一个 /dev/video10, 只能有一个读者
            self.logger.info("摄像头实时识别: 先停掉多模态预览")
            self._mm_stop()
        mi = self.cmb_yolo_model.get_active()
        if mi < 0 or mi >= len(self._yolo_models):
            self.show_error("请先选择 YOLO 模型")
            return
        model = self._yolo_models[mi]
        if model_backend(model) != "snpe":
            self.show_error("摄像头实时识别只支持 SNPE .dlc 模型\n请选 yolov7.dlc")
            return
        if get_daemon(model, "dsp") is None:
            self.show_error("npu-daemon 起不来, 看日志")
            return
        self._cam_model = model
        self._cam_on = True
        self.notebook.set_current_page(self._cam_tab_idx)
        self.btn_cam_tab.set_label("⏹ 停止")
        # 预览线程共用一个 daemon 会话, 期间禁止点检测, 否则输出 raw 会串
        self.btn_yolo.set_sensitive(False)
        self.btn_cmp.set_sensitive(False)
        self.logger.info("摄像头实时识别: 启动")
        threading.Thread(target=self._cam_loop, daemon=True).start()

    def _cam_stop(self):
        if not self._cam_on:
            return
        self._cam_on = False
        with self._cam_cond:
            self._cam_cond.notify_all()   # 叫醒等帧的推理线程
        p, self._cam_proc = self._cam_proc, None
        if p is not None:
            try:
                p.kill()
                p.wait(timeout=3)
            except Exception:
                pass
        GLib.idle_add(self._cam_stopped)

    def _cam_stopped(self):
        try:
            self.btn_cam_tab.set_label("▶ 开始")
            self.lbl_cam.set_text("/dev/video10 · CamxBridge 1280x720 · YOLO on HTP")
            self.btn_yolo.set_sensitive(True)
            self.btn_cmp.set_sensitive(True)
            self.logger.info("摄像头实时识别: 已停止")
        except Exception:
            pass    # 窗口正在销毁

    def _cam_loop(self):
        """取流线程 + 推理线程, 分开跑.

        取流线程只干一件事: 把管道抽干, 永远只保留最新一帧. 推理慢(40ms/帧)时
        丢掉中间的帧, 管道也不会被我们压住; 之前取流和推理串在一起, 一慢就落后
        于摄像头, v4l2loopback 的生产端就会覆盖正在读的缓冲, 画面就撕裂了.
        """
        w, h = CAM_W, CAM_H
        nbytes = w * h * 3
        cmd = camera_pipeline_cmd(w, h)
        try:
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL, bufsize=nbytes * 2)
        except Exception as e:
            self.logger.error(f"摄像头启动失败: {e}")
            self._cam_on = False
            GLib.idle_add(self._cam_stopped)
            return
        self._cam_proc = p
        threading.Thread(target=self._cam_infer_loop,
                         args=(w, h), daemon=True).start()

        buf = bytearray(nbytes)
        view = memoryview(buf)
        fd = p.stdout.fileno()
        try:
            while self._cam_on:
                got = 0
                while got < nbytes:               # os.read 会短读, 凑满一帧
                    n = os.readv(fd, [view[got:]])
                    if not n:
                        break
                    got += n
                if got < nbytes:
                    self.logger.warning("摄像头流中断")
                    break
                with self._cam_cond:
                    # 换缓冲而不是就地写: 推理线程正持有的那份不能被改
                    self._cam_raw, buf = bytes(buf), buf
                    self._cam_seq += 1
                    self._cam_cond.notify()
        finally:
            try:
                p.kill()
            except Exception:
                pass
            if self._cam_on:            # 意外退出, 复位按钮
                self._cam_on = False
                GLib.idle_add(self._cam_stopped)

    def _cam_infer_loop(self, w, h):
        """只处理最新帧: 跳过积压, 画面永远是最新的, 检测框和这一帧同源不漂移."""
        seq = 0
        now = time.monotonic()
        frames, t_log, t_fps, infer_ms = 0, now, now, 0.0
        fps, hud = 0.0, ""
        while self._cam_on:
            with self._cam_cond:
                while self._cam_on and self._cam_seq == seq:
                    self._cam_cond.wait(0.5)
                if not self._cam_on:
                    return
                seq, raw = self._cam_seq, self._cam_raw
            if not raw:
                continue
            pb = GdkPixbuf.Pixbuf.new_from_bytes(
                GLib.Bytes.new(raw), GdkPixbuf.Colorspace.RGB,
                False, 8, w, h, w * 3)
            try:
                dets, infer_ms = yolo_detect_pixbuf(self._cam_model, pb)
            except Exception as e:
                dets = []
                self.logger.warning(f"摄像头检测失败: {e}")
            with self._cam_lock:
                # 帧 + 框 + HUD 一起换, 不会错位
                self._cam_frame = (pb, dets, hud)
            GLib.idle_add(self.cam_area.queue_draw)
            frames += 1
            now = time.monotonic()
            if now - t_fps >= 1.0:
                fps = frames / (now - t_fps)
                frames, t_fps = 0, now
                hud = f"{fps:.1f} FPS · {len(dets)} 目标"
                GLib.idle_add(self.lbl_cam.set_text,
                              f"/dev/video10 · 1280x720 · {fps:.1f} FPS · "
                              f"NPU {infer_ms:.1f} ms · {len(dets)} 个目标")
            if dets and now - t_log >= 3.0:   # 只在识别到目标时才记日志
                self.logger.info(f"摄像头识别: {dets[0]['name']} 等 {len(dets)} 个目标, "
                                 f"NPU {infer_ms:.2f} ms, "
                                 f"{fps:.1f} FPS")
                t_log = now

    def _on_cam_draw(self, area, ctx):
        with self._cam_lock:
            frame = self._cam_frame
        return self._draw_frame(area, ctx, frame)

    def _draw_frame(self, area, ctx, frame):
        """画面铺满 (保持比例居中) + 左上 HUD + 检测框. 摄像头/视频共用."""
        W, H = area.get_allocated_width(), area.get_allocated_height()
        ctx.set_source_rgb(0.08, 0.08, 0.08)   # 先铺底, 尺寸变化时不留残影
        ctx.rectangle(0, 0, W, H)
        ctx.fill()
        if not frame:
            return False
        pb, dets, hud = frame
        w, h = pb.get_width(), pb.get_height()
        s = min(W / w, H / h)
        dw, dh = w * s, h * s
        ox, oy = (W - dw) / 2, (H - dh) / 2
        ctx.save()
        ctx.translate(ox, oy)
        ctx.scale(s, s)
        Gdk.cairo_set_source_pixbuf(ctx, pb, 0, 0)
        ctx.paint()
        ctx.restore()
        # 帧率/进度直接叠在画面上
        ctx.set_font_size(16)
        tw = ctx.text_extents(hud).width
        ctx.set_source_rgba(0, 0, 0, 0.55)
        ctx.rectangle(ox + 8, oy + 8, tw + 14, 26)
        ctx.fill()
        ctx.set_source_rgb(0.4, 1.0, 0.5)
        ctx.move_to(ox + 15, oy + 27)
        ctx.show_text(hud)
        COLORS = [(0.0, 0.8, 1.0), (1.0, 0.5, 0.0), (0.0, 1.0, 0.4),
                  (1.0, 0.2, 0.2), (0.8, 0.4, 1.0), (1.0, 1.0, 0.0)]
        cols = {}
        for d in dets:
            c = cols.setdefault(d['name'], COLORS[len(cols) % len(COLORS)])
            x1, y1 = ox + d['left'] * s, oy + d['top'] * s
            x2, y2 = ox + d['right'] * s, oy + d['bottom'] * s
            ctx.set_source_rgba(c[0], c[1], c[2], 0.15)
            ctx.rectangle(x1, y1, x2 - x1, y2 - y1)
            ctx.fill()
            ctx.set_source_rgb(*c)
            ctx.set_line_width(2)
            ctx.rectangle(x1, y1, x2 - x1, y2 - y1)
            ctx.stroke()
            label = f"{d['name']} {d['conf']:.0%}"
            ctx.set_font_size(11)
            tw = ctx.text_extents(label).width
            ty = max(0, y1 - 16)
            ctx.set_source_rgba(c[0], c[1], c[2], 0.85)
            ctx.rectangle(x1, ty, tw + 6, 16)
            ctx.fill()
            ctx.set_source_rgb(1, 1, 1)
            ctx.move_to(x1 + 3, ty + 12)
            ctx.show_text(label)
        return False

    # ---------- 多模态测试 (Qwen3.5 视觉) ----------
    def build_multimodal_tab(self):
        tab = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        tab.set_margin_start(8)
        tab.set_margin_end(8)
        tab.set_margin_top(8)
        self._mm_tab_idx = self.notebook.append_page(
            tab, Gtk.Label(label="\U0001F441 多模态测试"))

        desc = Gtk.Label(xalign=0)
        desc.set_line_wrap(True)
        desc.set_markup('<span size="small" foreground="gray">'
                        '把摄像头画面送给本地视觉模型, 问它看到了什么并输出回答. '
                        '摄像头源与"摄像头实时"页相同 (/dev/video10), 两页互斥使用. '
                        '本页打开即自动开始取流, 切走即释放摄像头</span>')
        tab.pack_start(desc, False, False, 0)

        # ---- 操作栏 ----
        bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)

        # 识别是这一页的主操作: 放大字号 + 加内边距 + 主色, 一眼能看见
        self.btn_mm_rec = Gtk.Button(label="\U0001F50D 识别")
        self.btn_mm_rec.set_sensitive(False)   # 有帧之后才可点
        self.btn_mm_rec.set_tooltip_text("把当前预览帧送给视觉模型, 问它看到了什么")
        self.btn_mm_rec.get_style_context().add_class("suggested-action")
        self.btn_mm_rec.set_name("vlm-recognize-btn")
        self.btn_mm_rec.set_size_request(170, 56)
        self.btn_mm_rec.connect("clicked", self.on_mm_recognize_clicked)
        bar.pack_start(self.btn_mm_rec, False, False, 0)

        self.btn_mm_prev = Gtk.Button(label="\u25B6 打开预览")
        self.btn_mm_prev.connect("clicked", self.on_mm_preview_clicked)
        bar.pack_start(self.btn_mm_prev, False, False, 0)

        # 取流卡住时的兜底: 自动重试也不行才需要人工点一下。
        # 做小一点, 不跟识别键抢注意力。
        self.btn_mm_reset = Gtk.Button(label="\u21BB 重置摄像头")
        self.btn_mm_reset.set_tooltip_text(
            "预览不出画面时点一下: 重新释放并打开 " + CAM_DEVICE)
        self.btn_mm_reset.set_relief(Gtk.ReliefStyle.NONE)
        self.btn_mm_reset.set_valign(Gtk.Align.CENTER)   # 不跟同行按钮一起撑高
        self.btn_mm_reset.set_size_request(0, 0)
        self.btn_mm_reset.get_style_context().add_class("vlm-small-btn")
        self.btn_mm_reset.connect("clicked", self.on_mm_reset_clicked)
        bar.pack_start(self.btn_mm_reset, False, False, 0)

        self.btn_mm_clear = Gtk.Button(label="\U0001F5D1 清空结果")
        self.btn_mm_clear.connect("clicked", self.on_mm_clear_clicked)
        bar.pack_start(self.btn_mm_clear, False, False, 0)

        self.lbl_mm = Gtk.Label(
            label=f"{CAM_DEVICE} · CamxBridge {CAM_W}x{CAM_H} · 预览未开启", xalign=0)
        bar.pack_start(self.lbl_mm, True, True, 0)
        tab.pack_start(bar, False, False, 0)

        # ---- 模型状态 / 提示词 / 生成长度 ----
        qb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.lbl_mm_qwen = Gtk.Label(label="视觉模型: 检测中…", xalign=0)
        qb.pack_start(self.lbl_mm_qwen, False, False, 0)

        self.btn_mm_start = Gtk.Button(label=f"启动视觉服务 ({VLM_SERVICE})")
        self.btn_mm_start.set_tooltip_text(
            f"systemctl start {VLM_SERVICE} (默认不开机自启: 独占 4 个大核 + 约 490MB)")
        self.btn_mm_start.connect("clicked", self.on_mm_start_qwen)
        qb.pack_start(self.btn_mm_start, False, False, 0)

        qb.pack_start(Gtk.Label(label="提示词:", xalign=0), False, False, 0)
        self.ent_mm_prompt = Gtk.Entry()
        self.ent_mm_prompt.set_text(VLM_PROMPT)
        self.ent_mm_prompt.set_hexpand(True)
        self.ent_mm_prompt.set_tooltip_text("回车或点『识别』发送")
        self.ent_mm_prompt.connect("activate", self.on_mm_recognize_clicked)
        qb.pack_start(self.ent_mm_prompt, True, True, 0)

        qb.pack_start(Gtk.Label(label="最多生成:", xalign=0), False, False, 0)
        # 生成长度上限不写死: 按服务端 runtime.json 的 limits.max_tokens 定范围
        cap = max(16, int(VLM_PROFILE.max_tokens_cap))
        self.sp_mm_tokens = Gtk.SpinButton.new_with_range(16, cap, 16)
        self.sp_mm_tokens.set_value(min(VLM_MAX_TOKENS, cap))
        self.sp_mm_tokens.set_tooltip_text(
            f"越多回答越长也越慢 (上限 {cap}, 来自服务端配置; "
            f"实测解码约 {VLM_PROFILE.tps:.1f} tok/s)")
        qb.pack_start(self.sp_mm_tokens, False, False, 0)
        qb.pack_start(Gtk.Label(label="tokens", xalign=0), False, False, 0)
        tab.pack_start(qb, False, False, 0)

        # ---- 上半预览 / 下半回答, 分隔可拖 ----
        paned = Gtk.Paned(orientation=Gtk.Orientation.VERTICAL)
        paned.set_position(360)

        self.mm_area = Gtk.DrawingArea()
        self.mm_area.set_size_request(640, 360)
        self.mm_area.connect("draw", self._on_mm_draw)
        paned.pack1(self.mm_area, True, False)

        rbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)

        # 标题行: 左边大标题, 右边一行小字指标
        hb = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        self.lbl_mm_res = Gtk.Label(xalign=0)
        self.lbl_mm_res.set_markup('<span size="x-large" weight="bold">'
                                   '\U0001F4AC 识别结果</span>')
        hb.pack_start(self.lbl_mm_res, False, False, 0)
        self.lbl_mm_meta = Gtk.Label(xalign=0)
        hb.pack_start(self.lbl_mm_meta, True, True, 0)
        rbox.pack_start(hb, False, False, 0)

        # ---- 识别中的动画区 (平时隐藏) ----
        self.mm_prog = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        self.mm_prog.set_margin_top(4)
        ptop = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        self.mm_spinner = Gtk.Spinner()
        ptop.pack_start(self.mm_spinner, False, False, 0)
        self.lbl_mm_stage = Gtk.Label(xalign=0)
        self.lbl_mm_stage.set_name("vlm-busy-stage")
        ptop.pack_start(self.lbl_mm_stage, False, False, 0)
        self.lbl_mm_elapsed = Gtk.Label(xalign=1)
        ptop.pack_end(self.lbl_mm_elapsed, False, False, 0)
        self.mm_prog.pack_start(ptop, False, False, 0)
        # pulse 模式: 服务端不给进度, 用不确定式进度条表达"在动", 不伪造成百分比
        self.mm_pbar = Gtk.ProgressBar()
        self.mm_pbar.set_pulse_step(0.06)
        self.mm_prog.pack_start(self.mm_pbar, False, False, 0)
        self.lbl_mm_stage_desc = Gtk.Label(xalign=0)
        self.lbl_mm_stage_desc.set_name("vlm-stage-text")
        self.mm_prog.pack_start(self.lbl_mm_stage_desc, False, False, 0)
        rbox.pack_start(self.mm_prog, False, False, 0)
        # set_no_show_all 必须在 hide 之前: 否则外层 show_all() 会把它们又弹出来
        self.mm_prog.set_no_show_all(True)
        self.mm_prog.hide()

        # ---- 回答本体: 大字号 + 底色框, 一眼能看到 ----
        ans_frame = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        ans_frame.set_name("vlm-answer-frame")
        sw = Gtk.ScrolledWindow()
        sw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        sw.set_shadow_type(Gtk.ShadowType.NONE)
        self.mm_result = Gtk.TextView()
        self.mm_result.set_name("vlm-answer-text")
        self.mm_result.set_editable(False)
        self.mm_result.set_cursor_visible(False)
        self.mm_result.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.mm_result.set_left_margin(14)
        self.mm_result.set_right_margin(14)
        self.mm_result.set_top_margin(12)
        self.mm_result.set_bottom_margin(12)
        self.mm_result.set_pixels_above_lines(5)     # 中文长句拉开行距更好读
        self.mm_result.set_pixels_below_lines(5)
        # 字号主要靠 CSS(#vlm-answer-text), 但 override_font 优先级比 CSS 高,
        # 所以这里不无条件覆盖; 只有 CSS 没装上时(见 __init__ 里的回退)才补.
        self.mm_result_buf = self.mm_result.get_buffer()
        sw.add(self.mm_result)
        ans_frame.pack_start(sw, True, True, 0)
        rbox.pack_start(ans_frame, True, True, 0)

        # ---- 处理过程时间轴 (回答后填真实耗时, 可折叠) ----
        self.mm_expander = Gtk.Expander()
        self.mm_expander.set_label("\u23F1 处理过程")
        self.mm_stage = Gtk.TextView()
        self.mm_stage.set_name("vlm-stage-text")
        self.mm_stage.set_editable(False)
        self.mm_stage.set_cursor_visible(False)
        self.mm_stage.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.mm_stage.set_left_margin(10)
        self.mm_stage.set_right_margin(10)
        self.mm_stage.set_top_margin(6)
        self.mm_stage.set_bottom_margin(6)
        self.mm_stage_buf = self.mm_stage.get_buffer()
        ssw = Gtk.ScrolledWindow()
        ssw.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        ssw.set_min_content_height(110)
        ssw.add(self.mm_stage)
        self.mm_expander.add(ssw)
        rbox.pack_start(self.mm_expander, False, False, 0)
        self.mm_expander.set_no_show_all(True)
        self.mm_expander.hide()

        paned.pack2(rbox, True, False)
        tab.pack_start(paned, True, True, 0)

        self._mm_busy_t0 = 0.0
        self._mm_busy_tick = None
        self._mm_max_tokens = VLM_MAX_TOKENS
        if not install_vlm_css():
            # CSS 挂不上时才用 override_font 保底(它会盖过 CSS, 所以只能二选一)
            self.logger.warning("多模态回答区样式未生效, 已回退到内置字号")
            try:
                self.mm_result.override_font(Pango.FontDescription("Sans 13"))
                self.mm_stage.override_font(Pango.FontDescription("Monospace 9"))
            except Exception:
                pass

        # 开界面后台探一下 qwen35 在不在, 不让探测阻塞主循环
        threading.Thread(target=self._mm_probe_worker, daemon=True).start()

    def on_mm_clear_clicked(self, btn):
        if self._mm_busy:
            return                              # 识别中别抢 buffer
        self.mm_result_buf.set_text("")
        self.lbl_mm_meta.set_markup("")
        self.mm_expander.hide()
        self.lbl_mm_res.set_markup('<span size="x-large" weight="bold">'
                                   '\U0001F4AC 识别结果</span>')

    # ---------- 视觉模型服务状态 ----------
    def _mm_probe_worker(self):
        GLib.idle_add(self._mm_qwen_label, vlm_probe())

    def _mm_qwen_label(self, mid):
        try:
            if mid:
                # 模型名/视觉尺寸都是读出来的: mid 来自 /v1/models,
                # 尺寸来自 runtime.json 或上次实测的 decoder_size_wh
                if VLM_PROFILE.model_id is None:
                    VLM_PROFILE.model_id = str(mid)
                self.lbl_mm_qwen.set_markup(
                    '<span foreground="#2ec27e">视觉模型: 就绪</span>  '
                    '<span size="small" foreground="gray">'
                    + GLib.markup_escape_text(VLM_PROFILE.model_display)
                    + ' · ' + GLib.markup_escape_text(VLM_PROFILE.vision_size_text)
                    + '</span>')
                self.btn_mm_start.set_sensitive(False)
                # 探测到模型后把生成长度上限和提示速度也按实际刷新
                cap = max(16, int(VLM_PROFILE.max_tokens_cap))
                self.sp_mm_tokens.set_range(16, cap)
                self.sp_mm_tokens.set_tooltip_text(
                    f"越多回答越长也越慢 (上限 {cap}, 来自服务端配置; "
                    f"实测解码约 {VLM_PROFILE.tps:.1f} tok/s)")
            else:
                self.lbl_mm_qwen.set_markup(
                    '<span foreground="#e01b24">视觉模型: 未运行</span>  '
                    '<span size="small" foreground="gray">'
                    '需先启动 ' + VLM_SERVICE + ' 服务</span>')
                self.btn_mm_start.set_sensitive(True)
        except Exception:
            pass

    def on_mm_start_qwen(self, btn):
        btn.set_sensitive(False)
        self.lbl_mm_qwen.set_text(f"视觉模型: 启动中… (首次加载约 10~30s)")
        threading.Thread(target=self._mm_start_qwen_worker, daemon=True).start()

    def _mm_start_qwen_worker(self):
        try:
            r = subprocess.run(["sudo", "-n", "systemctl", "start", "qwen35"],
                               capture_output=True, timeout=300)
        except FileNotFoundError:
            GLib.idle_add(self.show_error,
                          "找不到 sudo, 请手动执行: sudo systemctl start qwen35")
            GLib.idle_add(self._mm_qwen_label, None)
            return
        except Exception as e:
            self.logger.error(f"启动 qwen35 失败: {e}")
            GLib.idle_add(self.show_error, f"启动 qwen35 失败: {e}")
            GLib.idle_add(self._mm_qwen_label, None)
            return
        if r.returncode != 0:
            msg = (r.stderr or r.stdout or b"").decode("utf-8", "replace").strip()
            self.logger.error(f"启动 qwen35 失败: {msg or 'rc=' + str(r.returncode)}")
            GLib.idle_add(self.show_error,
                          f"启动 qwen35 失败 ({msg or 'rc=' + str(r.returncode)})\n"
                          "请手动执行: sudo systemctl start qwen35")
            GLib.idle_add(self._mm_qwen_label, None)
            return
        # systemctl start 返回不代表端口已监听, 等 /v1/models 真的能应答
        deadline = time.monotonic() + 180
        mid = None
        while time.monotonic() < deadline:
            mid = vlm_probe(timeout=2.0)
            if mid:
                break
            time.sleep(1.0)
        if mid:
            self.logger.info(f"Qwen3.5 已就绪: {mid}")
        else:
            self.logger.error("Qwen3.5 启动超时, 端口 8000 没起来")
        GLib.idle_add(self._mm_qwen_label, mid)

    # ---------- 预览 ----------
    def on_mm_preview_clicked(self, btn):
        if self._mm_on:
            self._mm_stop()
            return
        self._mm_start()

    def _mm_start(self, reason="手动"):
        """开始取流。切页进来、手动点按钮、重置后重开都走同一条路。"""
        if self._mm_on:
            return
        if self._cam_on:
            # 同一个 /dev/video10, 只能有一个读者
            self.logger.info("多模态预览: 先停掉摄像头实时识别")
            self._cam_stop()
        self._mm_on = True
        self._mm_ready = False
        self.btn_mm_rec.set_sensitive(False)
        self.btn_mm_prev.set_label("\u23F9 停止预览")
        self.lbl_mm.set_text(f"{CAM_DEVICE} · {CAM_W}x{CAM_H} · 正在打开…")
        self.logger.info(f"多模态预览: 启动 ({reason})")
        threading.Thread(target=self._mm_preview_loop, daemon=True).start()

    def on_mm_reset_clicked(self, btn):
        """预览不出画面时的手工兜底。在后台线程做, 不阻塞界面。

        普通用户就能完成的部分: 杀掉自己残留的取流子进程、关掉并重新打开
        v4l2 节点(CamxBridge 会在最后一个读者断开后重连 camera)。
        需要 root 的部分(重启桥接服务)只在退避重试也救不回来时才尝试,
        非 root 时把命令告诉用户, 不弹密码框。
        """
        btn.set_sensitive(False)
        self.logger.info("多模态预览: 手动重置摄像头")
        threading.Thread(target=self._mm_reset_worker, daemon=True).start()

    def _mm_reset_worker(self):
        try:
            self._mm_on = False               # 令取流循环退出
            self._mm_kill_proc()
            self._mm_kill_stale_helpers()
            time.sleep(0.5)                   # 给 bridge 时间重建 pipeline
            self._mm_fail_streak = 0
            GLib.idle_add(self._mm_restart_from_reset)
        except Exception as e:
            self.logger.error(f"重置摄像头失败: {e}")
            GLib.idle_add(self._mm_reset_failed, str(e))

    def _mm_restart_from_reset(self):
        try:
            self.btn_mm_reset.set_sensitive(True)
        except Exception:
            pass
        self._mm_start("重置后")

    def _mm_reset_failed(self, msg):
        try:
            self.btn_mm_reset.set_sensitive(True)
        except Exception:
            pass
        self.lbl_mm.set_text(f"{CAM_DEVICE} · 重置失败: {msg}")

    def _mm_kill_proc(self):
        """杀掉当前取流子进程并等它真的退出(不等会和新进程抢设备)。"""
        p, self._mm_proc = self._mm_proc, None
        if p is None:
            return
        try:
            p.kill()
            p.wait(timeout=3)
        except Exception:
            pass

    def _mm_kill_stale_helpers(self):
        """清掉以前遗留的取流管道。只动本进程启动的、命令行带本程序特征的那些。"""
        me = os.getuid()
        killed = 0
        for entry in os.listdir("/proc"):
            if not entry.isdigit() or int(entry) == os.getpid():
                continue
            base = f"/proc/{entry}"
            try:
                if os.stat(base).st_uid != me:
                    continue            # 不是自己的进程不碰
                with open(f"{base}/cmdline", "rb") as f:
                    cmd = f.read().replace(b"\0", b" ").decode("utf-8", "replace")
                # 只匹配我们发出的那条管道, 避免误伤用户自己的 gst 命令
                if "v4l2src" in cmd and CAM_DEVICE in cmd and "fdsink" in cmd \
                        and "videoconvert" in cmd:
                    os.kill(int(entry), signal.SIGKILL)
                    killed += 1
            except Exception:
                continue
        if killed:
            self.logger.info(f"多模态预览: 清掉 {killed} 个残留取流进程")
        return killed

    def on_notebook_switch_page(self, notebook, page, page_num):
        """切页自动开关预览: 进本页就取流, 离本页就释放摄像头。

        手动停止后切走再切回仍会重新开始 —— 页面的默认状态就是“在预览”。
        检测中(_mm_busy)切走不拦截请求: 帧已经送出去, 结果回来仍会填到回答区,
        只是不再占着摄像头。
        """
        try:
            if page_num == self._mm_tab_idx:
                if not self._mm_on:
                    self._mm_start("切到本页")
            elif self._mm_on:
                self.logger.info("多模态预览: 离开本页, 释放摄像头")
                self._mm_stop()
        except Exception:
            pass        # 启动阶段页面索引未就绪, 不因切页报错卡住界面

    def _mm_stop(self):
        if not self._mm_on:
            return
        self._mm_on = False
        p, self._mm_proc = self._mm_proc, None
        if p is not None:
            try:
                p.kill()
                p.wait(timeout=3)
            except Exception:
                pass
        GLib.idle_add(self._mm_stopped)

    def _mm_stopped(self, failed=False):
        try:
            self.btn_mm_prev.set_label("\u25B6 打开预览")
            self.btn_mm_rec.set_sensitive(False)
            self._mm_ready = False
            if failed:
                # 自动重试也救不回来: 把办法直接写在状态行, 不用去翻日志
                self._mm_recover_camera()
            else:
                self.lbl_mm.set_text(
                    f"{CAM_DEVICE} · CamxBridge {CAM_W}x{CAM_H} · 预览未开启")
            with self._mm_lock:
                self._mm_raw = None
                self._mm_frame = None
            self.mm_area.queue_draw()
            self.logger.info("多模态预览: 已停止")
        except Exception:
            pass    # 窗口正在销毁

    def _mm_recover_camera(self):
        """取流反复失败后的收尾: 能自己重启就重启, 不能就把该输的命令给出。"""
        unit = "camx-v4l2-bridge@0"
        cmd = f"sudo systemctl restart {unit}"
        if os.geteuid() == 0:
            try:
                r = subprocess.run(["systemctl", "restart", unit],
                                   capture_output=True, timeout=60)
                if r.returncode == 0:
                    self.logger.info(f"已重启 {unit}, 重新开始预览")
                    self.lbl_mm.set_text(
                        f"{CAM_DEVICE} · 已重启摄像头服务, 正在重新取流…")
                    self._mm_fail_streak = 0
                    GLib.timeout_add(1500, self._mm_auto_restart)
                    return
                err = (r.stderr or b"").decode("utf-8", "replace").strip()
                self.logger.error(f"重启 {unit} 失败: {err or r.returncode}")
            except Exception as e:
                self.logger.error(f"重启 {unit} 失败: {e}")
        self.lbl_mm.set_text(
            f"{CAM_DEVICE} · 取不到画面, 已自动重试 {self._mm_fail_streak} 次\n"
            f"先点“\u21BB 重置摄像头”; 仍不行就在终端执行: {cmd}")

    def _mm_auto_restart(self):
        """重启完摄像头服务后自动接回预览(仍在本页才做)。"""
        try:
            if self.notebook.get_current_page() == self._mm_tab_idx and not self._mm_on:
                self._mm_start("摄像头服务重启后")
        except Exception:
            pass
        return False

    def _mm_preview_loop(self):
        """只取流不推理: 每帧转成 pixbuf 供绘制, 同时留住裸 RGB 供识别用.

        不做 YOLO 检测, 因此不需要拆成取流/推理两个线程: 30fps 下单线程
        就能跟上, 画面永远是摄像头当前帧.
        """
        w, h = CAM_W, CAM_H
        nbytes = w * h * 3
        buf = bytearray(nbytes)
        view = memoryview(buf)
        frames, t_fps = 0, time.monotonic()
        seen = 0                             # 累计帧数, 不随 FPS 统计清零
        # CamxBridge 是按需建流的: 上一个客户端断开后它要重建整条 pipeline,
        # 这个窗口里新连上来的读者会一帧都拿不到就 EOF. 切页自动预览比手动点
        # 按钮更容易撞上这个窗口, 所以前几帧都拿不到时静默重试几次, 而不是
        # 让用户看到一个空的预览区再去手动切换。
        attempts, max_attempts = 0, 3
        while self._mm_on and attempts < max_attempts:
            attempts += 1
            started = False                  # 本次尝试是否已经出过完整帧
            try:
                p = subprocess.Popen(camera_pipeline_cmd(w, h),
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL,
                                     bufsize=nbytes * 2)
            except Exception as e:
                self.logger.error(f"多模态预览启动失败: {e}")
                break
            self._mm_proc = p
            fd = p.stdout.fileno()
            try:
                while self._mm_on:
                    got = 0
                    while got < nbytes:      # os.read 会短读, 凑满一帧
                        n = os.readv(fd, [view[got:]])
                        if not n:
                            break
                        got += n
                    if got < nbytes:
                        break
                    started = True
                    # 换缓冲而不是就地写: 识别线程可能正握着上一帧
                    raw, buf = bytes(buf), buf
                    pb = GdkPixbuf.Pixbuf.new_from_bytes(
                        GLib.Bytes.new(raw), GdkPixbuf.Colorspace.RGB,
                        False, 8, w, h, w * 3)
                    with self._mm_lock:
                        self._mm_raw = raw
                        self._mm_frame = pb
                    GLib.idle_add(self.mm_area.queue_draw)
                    if not self._mm_ready and seen >= MM_WARMUP_FRAMES:
                        # 过了预热期才让点识别, 否则送出去的是开头的黑帧
                        self._mm_ready = True
                        self._mm_fail_streak = 0     # 真出画面了, 健康计数归零
                        GLib.idle_add(self.btn_mm_rec.set_sensitive, True)
                    frames += 1
                    seen += 1
                    now = time.monotonic()
                    if now - t_fps >= 1.0:
                        fps = frames / (now - t_fps)
                        frames, t_fps = 0, now
                        GLib.idle_add(self.lbl_mm.set_text,
                                      f"{CAM_DEVICE} · {w}x{h} · {fps:.1f} FPS · "
                                      f"亮度 {mm_frame_luma(raw):.0f} · "
                                      f"预览中 (识别送入 {VLM_PROFILE.model_display})")
            finally:
                try:
                    p.kill()
                    p.wait(timeout=3)
                except Exception:
                    pass
                self._mm_proc = None
            if not self._mm_on:
                return                       # 用户主动停止/切走, 不报错不重试
            # 跑起来过再断 = 桥接重建或摄像头侧抽风, 也值得自动重连,
            # 但只在没出过帧时才算“启动失败”(用于判断要不要提示手动重置)。
            if started:
                self._mm_fail_streak = max(self._mm_fail_streak, 1)
                if attempts < max_attempts:
                    self.logger.warning(
                        f"多模态预览: 取流中断, 自动重连 ({attempts}/{max_attempts})")
                    GLib.idle_add(self.lbl_mm.set_text,
                                  f"{CAM_DEVICE} · {w}x{h} · 取流中断, "
                                  f"自动重连 ({attempts}/{max_attempts})…")
                    time.sleep(0.8)
                    continue
            else:
                self._mm_fail_streak += 1
                if attempts < max_attempts:
                    self.logger.info(
                        f"多模态预览: 桥接正在重建, {attempts}/{max_attempts} 次未取到帧, "
                        "稍后重试")
                    GLib.idle_add(self.lbl_mm.set_text,
                                  f"{CAM_DEVICE} · {w}x{h} · 正在重试取流 "
                                  f"({attempts}/{max_attempts})…")
                    time.sleep(0.8)
                    continue
            break
        if self._mm_on:                  # 意外退出, 复位按钮
            self._mm_on = False
            GLib.idle_add(self._mm_stopped, self._mm_fail_streak > 0)

    def _on_mm_draw(self, area, ctx):
        with self._mm_lock:
            pb = self._mm_frame
        W, H = area.get_allocated_width(), area.get_allocated_height()
        ctx.set_source_rgb(0.08, 0.08, 0.08)   # 先铺底, 尺寸变化时不留残影
        ctx.rectangle(0, 0, W, H)
        ctx.fill()
        if not pb:
            return False
        w, h = pb.get_width(), pb.get_height()
        s = min(W / w, H / h)                  # 保持比例撑满, 居中留黑边
        ctx.save()
        ctx.translate((W - w * s) / 2, (H - h * s) / 2)
        ctx.scale(s, s)
        Gdk.cairo_set_source_pixbuf(ctx, pb, 0, 0)
        ctx.paint()
        ctx.restore()
        return False

    # ---------- 识别 ----------
    def on_mm_recognize_clicked(self, btn):
        if self._mm_busy:
            return
        with self._mm_lock:
            raw = self._mm_raw
        if not raw:
            self.show_error("预览还没有画面, 稍等一下再点识别")
            return
        luma = mm_frame_luma(raw)
        if luma < MM_MIN_LUMA:
            # 全黑/极暗帧: 通常是 bridge 刚建流, 真实画面还没到
            self.show_error(
                f"当前帧几乎全黑 (亮度 {luma:.0f}/255), 识别没意义\n"
                "预览刚打开时前几帧是黑的, 等画面出来再点识别")
            return
        prompt = self.ent_mm_prompt.get_text().strip() or VLM_PROMPT
        max_tokens = int(self.sp_mm_tokens.get_value())
        self._mm_busy = True
        self.btn_mm_rec.set_sensitive(False)
        self.btn_mm_rec.set_label("\u23F3 识别中…")
        self.mm_result_buf.set_text("")
        self.lbl_mm_meta.set_markup("")
        self.mm_expander.hide()
        self._mm_anim_start(max_tokens)
        self.logger.info(f"多模态识别: 发送 1 帧 {CAM_W}x{CAM_H}, "
                         f"prompt={prompt!r}, max_tokens={max_tokens}")
        threading.Thread(target=self._mm_worker,
                         args=(raw, prompt, max_tokens), daemon=True).start()

    # ---------- 识别中的动画 ----------
    def _mm_anim_start(self, max_tokens):
        """启动旋转图标 + pulse 进度条 + 阶段提示.

        服务端是同步 HTTP, 没有逐阶段进度可拿, 所以时间轴按实测耗时估算推进,
        进度条用 pulse(不确定态) 而不是伪造百分比. 已经用掉的秒数会实时显示,
        用户能判断"它还在跑"还是"卡住了".
        """
        self._mm_busy_t0 = time.monotonic()
        self._mm_max_tokens = max_tokens
        # 时间轴每次启动时从 VLM_PROFILE 重建: 跑过几次后就是实测节奏
        self._mm_stages = VLM_PROFILE.stage_plan()
        self._mm_tps = VLM_PROFILE.tps
        self._mm_show_dyn(self.mm_prog)
        self.mm_spinner.start()
        self.lbl_mm_elapsed.set_text("0.0s")
        self._mm_anim_tick()
        if self._mm_busy_tick is None:
            self._mm_busy_tick = GLib.timeout_add(120, self._mm_anim_tick)

    @staticmethod
    def _mm_show_dyn(widget):
        """显示一个 set_no_show_all 的容器.

        被标了 no_show_all 的控件, 连自己调 show_all() 都无效(这是 GTK 的语义),
        所以这里逐个 show() 显式打开.
        """
        widget.show()
        if hasattr(widget, "get_children"):
            for ch in widget.get_children():
                NPUTestWindow._mm_show_dyn(ch)

    def _mm_anim_tick(self):
        """每 120ms 刷一次: 推进度条 + 按已用时间选当前阶段."""
        if not self._mm_busy:
            self._mm_busy_tick = None
            return False
        el = time.monotonic() - self._mm_busy_t0
        try:
            self.mm_pbar.pulse()
            self.lbl_mm_elapsed.set_text(f"{el:.1f}s")
            stages = getattr(self, "_mm_stages", None) or VLM_PROFILE.stage_plan()
            stage = stages[0]
            for s in stages:
                if el >= s[0]:
                    stage = s
            name, desc = stage[1], stage[2]
            # 解码阶段能按实测 tok/s 估个大概进度, 给用户点期待
            if stage is stages[-1] and self._mm_max_tokens:
                tps = getattr(self, "_mm_tps", None) or VLM_PROFILE.tps
                est = self._mm_max_tokens / tps
                done = min(self._mm_max_tokens,
                           int((el - stages[-1][0]) * tps))
                desc = f"已生成约 {max(0, done)}/{self._mm_max_tokens} tokens" \
                       f" (按实测 {tps:.1f} tok/s, 上限 {est:.0f}s, 提前结束即为准)"
            self.lbl_mm_stage.set_text(name)
            self.lbl_mm_stage_desc.set_text(desc)
        except Exception:
            pass
        return True

    def _mm_anim_stop(self):
        self._mm_busy_tick = None
        try:
            self.mm_spinner.stop()
            self.mm_prog.hide()
        except Exception:
            pass

    def _mm_worker(self, raw, prompt, max_tokens):
        t0 = time.monotonic()
        try:
            pb = GdkPixbuf.Pixbuf.new_from_bytes(
                GLib.Bytes.new(raw), GdkPixbuf.Colorspace.RGB,
                False, 8, CAM_W, CAM_H, CAM_W * 3)
            ok, jpeg = pb.save_to_bufferv(
                "jpeg", ["quality"], [VLM_JPEG_QUALITY])
            if not ok or not jpeg:
                raise RuntimeError("GdkPixbuf JPEG 编码失败")
            # 留一份实际发出去的图, 便于复现/比对回答
            try:
                d = os.path.join(LOG_DIR, "vlm")
                os.makedirs(d, exist_ok=True)
                stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
                with open(os.path.join(d, f"frame_{stamp}.jpg"), "wb") as f:
                    f.write(jpeg)
            except Exception:
                pass
            text, meta = vlm_ask_image(jpeg, prompt, max_tokens)
        except urllib.error.HTTPError as e:
            detail = ""
            try:
                detail = e.read().decode("utf-8", "replace")
                detail = ((json.loads(detail).get("error") or {}).get("message")
                          or detail)
            except Exception:
                pass
            self.logger.error(f"多模态识别失败: HTTP {e.code} {detail[:400]}")
            GLib.idle_add(self._mm_failed, f"Qwen3.5 返回 HTTP {e.code}\n{detail[:800]}")
            return
        except urllib.error.URLError as e:
            self.logger.error(f"多模态识别失败: 连不上 qwen35 ({e.reason})")
            GLib.idle_add(self._mm_failed,
                          f"连不上 Qwen3.5 服务 ({VLM_ENDPOINT})\n"
                          f"原因: {e.reason}\n"
                          "请点\"启动 Qwen3.5\"或执行: sudo systemctl start qwen35")
            return
        except Exception as e:
            self.logger.error(f"多模态识别失败: {e}")
            GLib.idle_add(self._mm_failed, str(e))
            return
        elapsed = time.monotonic() - t0
        self.logger.info(f"多模态识别完成: {elapsed:.1f}s, "
                         f"JPEG {len(jpeg) / 1024:.0f}KB, "
                         f"生成 {meta.get('completion_tokens')} tokens, "
                         f"{meta.get('decode_tps') or 0:.1f} tok/s")
        GLib.idle_add(self._mm_done, text, meta, elapsed, len(jpeg))

    def _mm_done(self, text, meta, elapsed, jpeg_len):
        self._mm_anim_stop()
        try:
            self.mm_result_buf.set_text(text)
            self.lbl_mm_meta.set_markup(
                '<span size="small" foreground="#2ec27e" weight="bold">'
                f'{elapsed:.1f}s</span>'
                '<span size="small" foreground="gray">  '
                + GLib.markup_escape_text(
                    f"{VLM_PROFILE.model_display} · "
                    + vlm_meta_line(meta, None, jpeg_len))
                + '</span>')
            self._mm_fill_stages(meta, elapsed, jpeg_len)
            # 模型状态行也跟着用实测值刷新(尺寸/速度可能刚学到)
            self.lbl_mm_qwen.set_markup(
                '<span foreground="#2ec27e">视觉模型: 就绪</span>  '
                '<span size="small" foreground="gray">'
                + GLib.markup_escape_text(VLM_PROFILE.model_display)
                + ' · ' + GLib.markup_escape_text(VLM_PROFILE.vision_size_text)
                + '</span>')
        except Exception:
            pass
        self._mm_busy = False
        self.btn_mm_rec.set_label("\U0001F50D 识别")
        self.btn_mm_rec.set_sensitive(self._mm_on)

    def _mm_fill_stages(self, meta, elapsed, jpeg_len):
        """把服务端报的真实阶段耗时画成一条时间轴(带占比进度条)."""
        rows = vlm_stage_rows(meta)
        buf = self.mm_stage_buf
        buf.set_text("")
        if not rows:
            self.mm_expander.hide()
            return
        total = sum(r[1] for r in rows) or 1.0
        # 等宽字体才能用空格把条形对齐
        tag = buf.create_tag("mono", family="Monospace")
        it = buf.get_end_iter()
        for name, sec, extra in rows:
            bar_n = max(1, int(round(sec / total * 26)))
            buf.insert(it, f"{name:<14}{sec:6.2f}s  ", -1)
            buf.insert_with_tags(it, "\u2588" * bar_n, tag)
            buf.insert(it, "\u2591" * (26 - bar_n)
                       + f" {sec / total * 100:4.1f}%\n", -1)
            if extra:
                buf.insert(it, f"{'':<14}\u2514 {extra}\n", -1)
        buf.insert(it, f"{'─' * 54}\n", -1)
        if meta.get("request_seconds"):
            buf.insert(it, f"{'服务端 self 计时':<14}{meta['request_seconds']:6.2f}s\n", -1)
        buf.insert(it, f"{'阶段合计(可重叠)':<14}{total:6.2f}s\n", -1)
        buf.insert(it, f"{'客户端端到端':<14}{elapsed:6.2f}s  "
                       f"(含 JPEG {jpeg_len / 1024:.0f}KB 编码与传输)\n", -1)
        self.mm_expander.set_expanded(False)
        self._mm_show_dyn(self.mm_expander)

    def _mm_failed(self, msg):
        self._mm_anim_stop()
        try:
            self.mm_result_buf.set_text(msg)
            self.lbl_mm_res.set_markup(
                '<span size="x-large" weight="bold" foreground="#e01b24">'
                '\u26A0 识别失败</span>')
            self.lbl_mm_meta.set_markup("")
            self.mm_expander.hide()
        except Exception:
            pass
        self._mm_busy = False
        self.btn_mm_rec.set_label("\U0001F50D 识别")
        self.btn_mm_rec.set_sensitive(self._mm_on)

    def _refresh_yolo_models(self):
        self.cmb_yolo_model.remove_all()
        # 过滤: basename 或父目录名含 "yolo"
        self._yolo_models = [m for m in find_models()
                            if "yolo" in m.lower()]
        for m in self._yolo_models:
            # 对子目录里的模型 (如 yolov7_onnx/model.onnx) 用父目录名显示
            parent = os.path.basename(os.path.dirname(m))
            if parent and parent != os.path.basename(m):
                self.cmb_yolo_model.append_text(f"{parent}/{os.path.basename(m)}")
            else:
                self.cmb_yolo_model.append_text(os.path.basename(m))
        if self._yolo_models:
            self.cmb_yolo_model.set_active(0)
        else:
            self.cmb_yolo_model.append_text("（无 YOLO 模型，请放 .dlc/.onnx）")
            self.cmb_yolo_model.set_active(0)

    def on_yolo_clicked(self, button):
        mi = self.cmb_yolo_model.get_active()
        ii = self.cmb_yolo_image.get_active()
        if mi < 0 or mi >= len(self._yolo_models):
            self.show_error("请先选择 YOLO 模型\n把 yolov*.dlc / yolov*_onnx 放到 "
                            "/usr/share/qcom-npu-test/models/ 后点 🔄 刷新")
            return
        if ii < 0:
            self.show_error("请选择测试图片")
            return
        model = self._yolo_models[mi]
        image = self._yolo_images[ii]
        rt_idx = self.cmb_yolo_runtime.get_active()
        backend = model_backend(model)
        runtime_name = ["DSP (NPU/HTP)", "GPU (Adreno)", "CPU"][rt_idx]

        if backend == "onnx_reference":
            self.show_error(
                "此 .onnx 是 Qualcomm AI Hub 源模型, 无法在板上直接推理。\n"
                "需在 PC 上用 qairt-converter 转成 .so 后再用。\n\n"
                "如需在板上直接检测, 请选 yolov7.dlc (SNPE 容器)。")
            return
        if backend == "snpe":
            if not os.path.exists(SNPE_NET_RUN):
                self.show_error(f"snpe-net-run 不存在: {SNPE_NET_RUN}")
                return
            runtime_flag = ["--use_dsp", "--use_gpu", ""][rt_idx]
        else:  # qnn (.so)
            if not os.path.exists(QNN_NET_RUN):
                self.show_error(f"qnn-net-run 不存在: {QNN_NET_RUN}\n请确认 qcom-qnn-sdk 已安装")
                return
            runtime_flag = ["--backend libQnnHtp.so",
                            "--backend libQnnGpu.so",
                            "--backend libQnnCpu.so"][rt_idx]

        self.btn_yolo.set_sensitive(False)
        self.btn_yolo.set_label("⏳ 检测中...")
        self.logger.info(f"YOLO: model={os.path.basename(model)} img={os.path.basename(image)} "
                         f"backend={backend} rt={runtime_name}")

        def run():
            import time as _time
            t0 = _time.monotonic()
            raw, out_path, dets, out_dir = run_yolo(model, image, runtime_flag)
            call_ms = (_time.monotonic() - t0) * 1000.0
            # 推理延迟从本次运行的 SNPEDiag.log 读, 不再另跑一个进程.
            t_lat = _time.monotonic()
            lat = measure_npu_latency(model, out_dir) if out_dir else {}
            lat_ms = (_time.monotonic() - t_lat) * 1000.0
            self.logger.info(f"YOLO 分项: 总={call_ms:.0f}ms "
                             f"diagview={lat_ms:.0f}ms NPU={lat.get('avg_ms', 0):.2f}ms")
            GLib.idle_add(self.on_yolo_done, raw, model, image, runtime_name,
                          out_path, dets, call_ms, lat)
        threading.Thread(target=run, daemon=True).start()

    def on_compare_clicked(self, btn):
        """同一张图依次跑 CPU / GPU / NPU, 只用实测数字画对比表."""
        mi = self.cmb_yolo_model.get_active()
        ii = self.cmb_yolo_image.get_active()
        if mi < 0 or mi >= len(self._yolo_models) or ii < 0:
            self.show_error("请先选好模型和图片")
            return
        model, image = self._yolo_models[mi], self._yolo_images[ii]
        if model_backend(model) != "snpe":
            self.show_error("三档对比只支持 .dlc (SNPE); .onnx 需先转换")
            return
        btn.set_sensitive(False)
        btn.set_label("⏳ 对比中…")

        def run():
            rows = []
            # 慢的在前: 落到 NPU 那行时的反差就是卖点
            for name, flag in (("CPU (8×A78)", ""),
                               ("GPU (Adreno)", "--use_gpu"),
                               ("DSP (NPU/HTP)", "--use_dsp")):
                t0 = time.monotonic()
                raw, _p, dets, out_dir = run_yolo(model, image, flag)
                wall = (time.monotonic() - t0) * 1000.0
                lat = measure_npu_latency(model, out_dir) if out_dir else {}
                ms = lat.get('total_ms') or lat.get('avg_ms') or 0
                rows.append((name, ms, wall, len(dets or [])))
            GLib.idle_add(self.show_compare, rows, btn)

        threading.Thread(target=run, daemon=True).start()

    def show_compare(self, rows, btn):
        btn.set_sensitive(True)
        btn.set_label("⚡ 三档对比")
        ok = [r for r in rows if r[1] > 0]
        if not ok:
            self.show_error("三档都失败了, 看日志")
            return
        buf = self.yolo_result_text.get_buffer()
        buf.set_text("")
        buf.insert(buf.get_end_iter(), compare_text(rows))
        for name, ms, _w, _n in rows:
            if ms > 0:
                _RUNTIME_MS[name.split()[0]] = ms

    def on_yolo_done(self, raw, model, image, runtime, out_path, dets=None, call_ms=0, lat=None):
        self.btn_yolo.set_sensitive(True)
        self.btn_yolo.set_label("▶ 开始检测")
        if dets is None:
            dets = []
        # parse_yolo_output is kept as a fallback for tools that still emit
        # text-form detections (e.g. QNN .so without raw dump); for the SNPE
        # yolov7.dlc path dets already comes decoded from run_yolo.
        info = parse_yolo_output(raw) if not dets else {}
        if not dets:
            dets = info.get('detections', [])
        buf = self.yolo_result_text.get_buffer()
        buf.set_text("")

        def a(text):
            buf.insert(buf.get_end_iter(), text)

        a(f"{'='*55}\n  🎯 YOLO 目标检测结果\n{'='*55}\n\n")
        a(f"模型: {os.path.basename(model)}\n")
        a(f"图片: {os.path.basename(image)}\n")
        a(f"Runtime: {runtime}\n\n")
        if 'build_ver' in info:
            a(f"SNPE 版本: {info['build_ver']}\n")
        if 'model_h' in info:
            a(f"模型输入: {info['model_w']}x{info['model_h']}x{info['model_c']}\n")
        # 耗时统计: NPU 延迟取自本次运行的 SNPEDiag.log (snpe-diagview).
        # 拆成两行, 否则单帧冷跑的 Total 会被误当成"NPU 变慢了":
        #   NPU计算 = DSP 上跑网络的时间 (随 DVFS 变, 单帧 ~0.9GHz / 满载 ~1.4GHz)
        #   单次推理 = 计算 + 4.9MB 输入和 3 路输出张量的 fastrpc 搬运
        npu_ms = (lat or {}).get('avg_ms', 0)
        tot_ms = (lat or {}).get('total_ms', 0)
        a(fmt_timing(lat, call_ms, runtime))
        a("\n")

        if dets:
            a(f"{'─'*55}\n  🎯 检测到 {len(dets)} 个目标\n{'─'*55}\n")
            for i, d in enumerate(dets, 1):
                w = d['right'] - d['left']
                h = d['bottom'] - d['top']
                bar = "█" * int(d['conf'] * 30) + "░" * (30 - int(d['conf'] * 30))
                a(f"  #{i} {d['name']:>10s}  置信度: {d['conf']:.4f}  [{bar}]\n")
                a(f"       位置: ({d['left']}, {d['top']}) - ({d['right']}, {d['bottom']})  {w}x{h}\n\n")
        else:
            a("未检测到目标 (或 numpy/输出 raw 缺失)\n\n")
        a(f"{'─'*55}\n  📋 原始输出\n{'─'*55}\n{raw}")

        # 在原图上画检测框并显示 (cairo 渲染)
        display_path = out_path if out_path and os.path.exists(out_path) else image
        self._render_yolo_image(display_path, dets)
        if out_path:
            a(f"\n📸 输出图片: {out_path}\n")

        n = len(dets)
        # 状态栏优先显示 NPU 纯推理延迟, 没有则不显示耗时
        if npu_ms:
            self.statusbar.set_markup(
                f'<span foreground="green">✅ YOLO完成 - {n}个目标, NPU {npu_ms:.2f}ms</span>')
        else:
            self.statusbar.set_markup(
                f'<span foreground="green">✅ YOLO完成 - {n}个目标</span>')
        self.logger.info(f"YOLO完成: {n}个目标, NPU={npu_ms:.2f}ms, 调用={call_ms:.0f}ms")

    def _render_yolo_image(self, path, dets):
        """在图片上画检测框并显示 (复用 RK3576 rknpu-test 的 cairo 渲染逻辑)"""
        try:
            pixbuf = GdkPixbuf.Pixbuf.new_from_file(path)
        except Exception:
            self.yolo_img_widget.set_from_icon_name("image-missing", Gtk.IconSize.DIALOG)
            return
        w, h = pixbuf.get_width(), pixbuf.get_height()
        if not w or not h:
            self.yolo_img_widget.set_from_pixbuf(pixbuf)
            return
        if not dets:
            scale = min(320/w, 320/h)
            pb = pixbuf.scale_simple(int(w*scale), int(h*scale), GdkPixbuf.InterpType.BILINEAR)
            self.yolo_img_widget.set_from_pixbuf(pb)
            return
        # cairo 画框
        surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, h)
        ctx = cairo.Context(surface)
        Gdk.cairo_set_source_pixbuf(ctx, pixbuf, 0, 0)
        ctx.paint()
        COLORS = [(0.0,0.8,1.0),(1.0,0.5,0.0),(0.0,1.0,0.4),(1.0,0.2,0.2),
                  (0.8,0.4,1.0),(1.0,1.0,0.0),(0.0,0.6,1.0),(1.0,0.6,0.8)]
        class_colors = {}
        ci = 0
        font_size = max(14, min(w, h) // 22)
        line_width = max(2, min(w, h) // 150)
        for det in dets:
            name = det['name']
            conf = det['conf']
            x1, y1, x2, y2 = det['left'], det['top'], det['right'], det['bottom']
            if name not in class_colors:
                class_colors[name] = COLORS[ci % len(COLORS)]
                ci += 1
            r, g, b = class_colors[name]
            ctx.set_source_rgba(r, g, b, 0.15)
            ctx.rectangle(x1, y1, x2-x1, y2-y1)
            ctx.fill()
            ctx.set_source_rgb(r, g, b)
            ctx.set_line_width(line_width)
            ctx.rectangle(x1, y1, x2-x1, y2-y1)
            ctx.stroke()
            label = f"{name} {conf:.0%}"
            ctx.set_font_size(font_size)
            ext = ctx.text_extents(label)
            tag_w = ext.width + 10
            tag_h = ext.height + 8
            tag_x = x1
            tag_y = y1 - tag_h - 2 if y1 > tag_h + 10 else y1 + 2
            ctx.set_source_rgba(r, g, b, 0.85)
            ctx.rectangle(tag_x, tag_y, tag_w, tag_h)
            ctx.fill()
            ctx.set_source_rgb(1, 1, 1)
            ctx.move_to(tag_x + 5, tag_y + tag_h - 4)
            ctx.show_text(label)
        surface.flush()
        # NOTE: 用 Gdk.pixbuf_get_from_surface 替换原来的手动字节交换 +
        # new_from_data。后者在 pygobject 下不拷贝像素,只保存指针;
        # bytes(raw) 与 surface 均为临时对象,函数返回后立即被 GC,
        # 导致后续 scale_simple/set_from_pixbuf 读到已释放内存,
        # 画面呈现"九宫格/九遍平铺"式损坏。pixbuf_get_from_surface
        # 内部做受控深拷贝并正确处理 cairo ARGB32(BGRA, 小端)→
        # GdkPixbuf(RGBA) 的字节序与 rowstride 对齐。
        result_pb = Gdk.pixbuf_get_from_surface(surface, 0, 0, w, h)
        scale = min(320/w, 320/h)
        if scale < 1.0:
            result_pb = result_pb.scale_simple(int(w*scale), int(h*scale),
                                                GdkPixbuf.InterpType.BILINEAR)
        self.yolo_img_widget.set_from_pixbuf(result_pb)
        self.yolo_img_widget.queue_draw()

    # ========== Tab3: 吞吐量基准 ==========
    def build_camera_tab(self):
        tab = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        tab.set_border_width(8)
        self._cam_tab_idx = self.notebook.append_page(tab, Gtk.Label(label="📷 摄像头实时"))

        bar = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=8)
        self.btn_cam_tab = Gtk.Button(label="▶ 开始")
        self.btn_cam_tab.get_style_context().add_class("suggested-action")
        self.btn_cam_tab.connect("clicked", self.on_cam_clicked)
        bar.pack_start(self.btn_cam_tab, False, False, 0)
        self.lbl_cam = Gtk.Label(label="/dev/video10 · CamxBridge 1280x720 · YOLO on HTP")
        bar.pack_start(self.lbl_cam, False, False, 0)
        tab.pack_start(bar, False, False, 0)

        # 占满整个 Tab: 画面按比例缩放居中, 检测框跟着缩放
        self.cam_area = Gtk.DrawingArea()
        self.cam_area.set_size_request(640, 360)
        self.cam_area.connect("draw", self._on_cam_draw)
        tab.pack_start(self.cam_area, True, True, 0)

    def build_throughput_tab(self):
        tab = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=8)
        tab.set_margin_start(8)
        tab.set_margin_end(8)
        tab.set_margin_top(8)
        self.notebook.append_page(tab, Gtk.Label(label="⚡ 吞吐量基准"))

        desc = Gtk.Label(xalign=0)
        desc.set_markup('<span size="small" foreground="gray">'
                        '用 snpe-throughput-net-run 在指定时长内并发压测, 输出平均 FPS / 延迟 / 总推理数</span>')
        tab.pack_start(desc, False, False, 0)

        sel = Gtk.Box(spacing=10)
        tab.pack_start(sel, False, False, 0)

        # 模型
        mb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        mb.pack_start(Gtk.Label(label="DLC 模型:", xalign=0), False, False, 0)
        self.cmb_tp_model = Gtk.ComboBoxText()
        for m in find_models():
            self.cmb_tp_model.append_text(os.path.basename(m))
        if find_models():
            self.cmb_tp_model.set_active(0)
        mb.pack_start(self.cmb_tp_model, False, False, 0)
        sel.pack_start(mb, True, True, 0)

        # 图片
        ib = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        ib.pack_start(Gtk.Label(label="输入图片:", xalign=0), False, False, 0)
        self.cmb_tp_image = Gtk.ComboBoxText()
        for img in find_images():
            self.cmb_tp_image.append_text(os.path.basename(img))
        if find_images():
            self.cmb_tp_image.set_active(0)
        ib.pack_start(self.cmb_tp_image, False, False, 0)
        sel.pack_start(ib, True, True, 0)

        # 时长
        db = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        db.pack_start(Gtk.Label(label="压测时长(秒):", xalign=0), False, False, 0)
        self.spin_duration = Gtk.SpinButton.new_with_range(1, 120, 1)
        self.spin_duration.set_value(10)
        db.pack_start(self.spin_duration, False, False, 0)
        sel.pack_start(db, False, False, 0)

        # Runtime
        rb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        rb.pack_start(Gtk.Label(label="Runtime:", xalign=0), False, False, 0)
        self.cmb_tp_runtime = Gtk.ComboBoxText()
        self.cmb_tp_runtime.append_text("DSP (NPU/HTP)")
        self.cmb_tp_runtime.append_text("GPU (Adreno)")
        self.cmb_tp_runtime.append_text("CPU")
        self.cmb_tp_runtime.set_active(0)
        rb.pack_start(self.cmb_tp_runtime, False, False, 0)
        sel.pack_start(rb, False, False, 0)

        # perf profile
        pb = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        pb.pack_start(Gtk.Label(label="性能档位:", xalign=0), False, False, 0)
        self.cmb_perf = Gtk.ComboBoxText()
        for p in ("balanced", "high_performance", "sustained_high_performance",
                  "burst", "power_saver", "default"):
            self.cmb_perf.append_text(p)
        self.cmb_perf.set_active(1)
        pb.pack_start(self.cmb_perf, False, False, 0)
        sel.pack_start(pb, False, False, 0)

        # 按钮
        bbtn = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        bbtn.pack_start(Gtk.Label(label=""), False, False, 0)
        self.btn_tp = Gtk.Button(label="▶ 开始压测")
        self.btn_tp.get_style_context().add_class("suggested-action")
        self.btn_tp.connect("clicked", self.on_throughput_clicked)
        bbtn.pack_start(self.btn_tp, False, False, 0)
        sel.pack_start(bbtn, False, False, 0)

        # 结果区
        result_frame = Gtk.Frame(label="压测结果")
        sw = Gtk.ScrolledWindow()
        self.tp_result_text = Gtk.TextView()
        self.tp_result_text.set_editable(False)
        self.tp_result_text.set_monospace(True)
        self.tp_result_text.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.tp_result_text.get_buffer().set_text("选择模型和压测时长, 点击 [开始压测]...\n"
            "压测期间 NPU 会满载, 输出平均 FPS、平均延迟、总推理数")
        sw.add(self.tp_result_text)
        result_frame.add(sw)
        tab.pack_start(result_frame, True, True, 0)

    def on_throughput_clicked(self, button):
        mi = self.cmb_tp_model.get_active()
        ii = self.cmb_tp_image.get_active()
        if mi < 0:
            self.show_error("请选择模型")
            return
        if not os.path.exists(SNPE_THROUGHPUT_RUN):
            self.show_error(f"snpe-throughput-net-run 不存在: {SNPE_THROUGHPUT_RUN}\n"
                           "(吞吐量压测仅支持 SNPE .dlc 模型)")
            return
        models = find_models()
        model = models[mi]
        # snpe-throughput-net-run 只吃 .dlc, ONNX 模型不支持
        if model_backend(model) != "snpe":
            self.show_error("吞吐量压测仅支持 SNPE .dlc 模型\n"
                           "请选择 .dlc 模型 (如 yolov7.dlc)")
            return
        images = find_images()
        image = images[ii] if ii >= 0 and ii < len(images) else (images[0] if images else None)
        if not image:
            self.show_error("找不到测试图片")
            return
        rt_idx = self.cmb_tp_runtime.get_active()
        runtime_flag = ["--use_dsp", "--use_gpu", ""][rt_idx]
        runtime_name = ["DSP (NPU/HTP)", "GPU (Adreno)", "CPU"][rt_idx]
        duration = int(self.spin_duration.get_value())
        perf = self.cmb_perf.get_active_text()

        self.btn_tp.set_sensitive(False)
        self.btn_tp.set_label("⏳ 压测中...")
        self.logger.info(f"吞吐量压测: model={os.path.basename(model)} runtime={runtime_name} "
                         f"duration={duration}s perf={perf}")

        def run():
            out_dir = tempfile.mkdtemp(prefix="snpe_tp_", dir="/tmp")

            # snpe-throughput-net-run 也只吃 raw, 预处理同单次推理
            np = _np()
            raw_bytes = None
            input_file = image
            if np is not None:
                try:
                    raw_bytes, _, _ = yolo_letterbox(image, 640)
                    raw_path = os.path.join(out_dir, "image.raw")
                    with open(raw_path, "wb") as f:
                        f.write(raw_bytes)
                    input_file = raw_path
                except Exception as e:
                    self.logger.warning(f"图片预处理失败, 回退原始文件: {e}")

            # 常驻会话优先: 会话只建一次, 报的是真实吞吐, 不含每次重建的开销
            if raw_bytes and model_backend(model) == "snpe":
                rt = ["dsp", "gpu", "cpu"][rt_idx]
                txt = throughput_via_daemon(model, rt, raw_bytes, duration)
                if txt:
                    GLib.idle_add(self.on_throughput_done, txt, 0, model,
                                  runtime_name, duration, out_dir)
                    return

            # snpe-throughput-net-run 不支持 --input_list, 用 --input_raw
            cmd = [SNPE_THROUGHPUT_RUN, f"--container={model}",
                   f"--input_raw={input_file}", f"--duration={duration}",
                   f"--perf_profile={perf}"]
            cmd.extend(runtime_flag.split())    # CPU 档为空, 不能塞空串进去
            # 加超时给模型加载留余量
            out, rc = run_cmd(cmd, timeout=duration + 120)
            GLib.idle_add(self.on_throughput_done, out, rc, model, runtime_name, duration, out_dir)
        threading.Thread(target=run, daemon=True).start()

    def on_throughput_done(self, raw, rc, model, runtime, duration, out_dir):
        self.btn_tp.set_sensitive(True)
        self.btn_tp.set_label("▶ 开始压测")
        info = parse_throughput(raw)
        buf = self.tp_result_text.get_buffer()
        buf.set_text("")

        def a(text):
            buf.insert(buf.get_end_iter(), text)

        a(f"{'='*55}\n  ⚡ SNPE 吞吐量基准测试\n{'='*55}\n\n")
        a(f"模型: {os.path.basename(model)}\n")
        a(f"Runtime: {runtime}\n")
        a(f"压测时长: {duration} 秒\n")
        a(f"返回码: {rc}\n\n")
        if 'build_ver' in info:
            a(f"SNPE 版本: {info['build_ver']}\n")
        if 'network' in info:
            a(f"网络: {info['network']}\n")
        if 'avg_fps' in info or 'avg_ms' in info:
            a(f"\n{'─'*55}\n  🚀 性能结果\n{'─'*55}\n")
            if 'avg_fps' in info:
                a(f"平均 FPS: {info['avg_fps']:.1f}\n")
            if 'avg_ms' in info:
                a(f"平均延迟: {info['avg_ms']:.2f} ms\n")
            if 'total_inferences' in info:
                a(f"总推理数: {info['total_inferences']}\n")
            if 'total_time_s' in info:
                a(f"总执行时间: {info['total_time_s']:.1f} s\n")
            if info.get('avg_ms', 0) > 0 and 'avg_fps' not in info:
                a(f"推算 FPS: {1000.0/info['avg_ms']:.1f}\n")
        if 'fps_samples' in info and info['fps_samples']:
            a(f"\n{'─'*55}\n  📊 逐时段 FPS\n{'─'*55}\n")
            for i, fps in enumerate(info['fps_samples']):
                bar = "█" * int(fps / 10)
                a(f"  [{i:2d}] {fps:7.1f} FPS  {bar}\n")
        a(f"\n{'─'*55}\n  📋 原始输出\n{'─'*55}\n{raw}")

        fps = info.get('avg_fps', 0)
        ms = info.get('avg_ms', 0)
        self.statusbar.set_markup(
            f'<span foreground="{"green" if rc==0 else "red"}">'
            f'{"✅" if rc==0 else "❌"} 压测{"完成" if rc==0 else "失败"} - '
            f'{fps:.1f} FPS / {ms:.2f} ms</span>' if (fps or ms)
            else f'<span foreground="{"green" if rc==0 else "red"}">{"✅" if rc==0 else "❌"} 压测{"完成" if rc==0 else "失败"}</span>')
        self.logger.info(f"压测完成: {fps:.1f} FPS, {ms:.2f} ms")

    # ========== 公共方法 ==========
    def on_upload_image(self, combo, images, widget):
        dialog = Gtk.FileChooserDialog(
            title="选择图片", parent=self, action=Gtk.FileChooserAction.OPEN)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                           Gtk.STOCK_OPEN, Gtk.ResponseType.OK)
        filt = Gtk.FileFilter()
        filt.set_name("图片文件")
        filt.add_mime_type("image/*")
        dialog.add_filter(filt)
        if dialog.run() != Gtk.ResponseType.OK:
            dialog.destroy()
            return
        local = dialog.get_filename()
        dialog.destroy()
        if not local:
            return
        upload_dir = "/tmp/npu_uploads"
        os.makedirs(upload_dir, exist_ok=True)
        try:
            os.chmod(upload_dir, 0o777)
        except:
            pass
        dest = os.path.join(upload_dir, os.path.basename(local))
        try:
            shutil.copy2(local, dest)
            self.logger.info(f"上传图片: {os.path.basename(local)} -> {dest}")
        except Exception as e:
            self.show_error(f"上传失败: {e}")
            return
        if dest not in images:
            images.append(dest)
            combo.append_text(os.path.basename(local))
        combo.set_active(images.index(dest))
        self.load_image_preview(combo, images, widget)

    def load_image_preview(self, combo, images, widget):
        idx = combo.get_active()
        if idx < 0 or idx >= len(images):
            return
        path = images[idx]
        try:
            pixbuf = GdkPixbuf.Pixbuf.new_from_file(path)
            w, h = pixbuf.get_width(), pixbuf.get_height()
            scale = min(280/w, 280/h) if w and h else 1
            pixbuf = pixbuf.scale_simple(int(w*scale), int(h*scale),
                                         GdkPixbuf.InterpType.BILINEAR)
            widget.set_from_pixbuf(pixbuf)
        except Exception:
            widget.set_from_icon_name("image-missing", Gtk.IconSize.DIALOG)

    def on_save_log(self, button):
        dialog = Gtk.FileChooserDialog(
            title="保存日志", parent=self, action=Gtk.FileChooserAction.SAVE)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                           Gtk.STOCK_SAVE, Gtk.ResponseType.OK)
        dialog.set_current_name(f"qcom_npu_test_{datetime.datetime.now():%Y%m%d_%H%M%S}.log")
        filt = Gtk.FileFilter()
        filt.set_name("日志文件 (*.log)")
        filt.add_pattern("*.log")
        dialog.add_filter(filt)
        if dialog.run() == Gtk.ResponseType.OK:
            path = dialog.get_path()
            buf = self.log_textview.get_buffer()
            start, end = buf.get_bounds()
            text = buf.get_text(start, end, True)
            with open(path, 'w', encoding='utf-8') as f:
                f.write(text)
            self.logger.info(f"日志已保存: {path}")
        dialog.destroy()

    def show_error(self, msg):
        d = Gtk.MessageDialog(transient_for=self, message_type=Gtk.MessageType.ERROR,
                              buttons=Gtk.ButtonsType.OK, text=msg)
        d.run()
        d.destroy()


def _selftest():
    """板上自检 (不弹 GUI): python3 /usr/bin/qcom-npu-test --selftest

    校验 letterbox 输出尺寸/填充值, 以及 snpe-net-run + diagview 延迟解析
    这条主链路能不能跑通. 改了预处理或延迟解析后跑一次即可.
    """
    img = os.path.join(IMAGE_DIRS[0], "bus.jpg")
    model = os.path.join(MODEL_DIRS[0], "yolov7.dlc")
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    raw, (top, left, scale), (iw, ih) = yolo_letterbox(img, 640)
    assert len(raw) == 640 * 640 * 3 * 4, len(raw)
    arr = _np().frombuffer(raw, dtype="float32").reshape(640, 640, 3)
    if top:
        assert abs(float(arr[0, 0, 0]) - 114.0 / 255.0) < 1e-6, float(arr[0, 0, 0])
    txt, out_path, dets, out_dir = run_yolo(model, img, "--use_dsp")
    assert out_dir, txt
    lat = measure_npu_latency(model, out_dir)
    assert (lat.get('avg_ms', 0) or lat.get('total_ms', 0)) > 0, \
        f"未解析到推理时间: {txt[-400:]}"
    # 摄像头走 pixbuf 直喂(不落盘), 校验它和 run_yolo 的检测结果一致
    cdets, _cms = yolo_detect_pixbuf(model, GdkPixbuf.Pixbuf.new_from_file(img))
    assert len(cdets) == len(dets), (len(cdets), len(dets))
    # 吞吐页走的是常驻会话压测, 校验它输出的文本能被 parse_throughput 解析回来
    tp = throughput_via_daemon(model, "dsp", raw, 2)
    assert tp and parse_throughput(tp).get('avg_fps', 0) > 0, tp
    print(f"SELFTEST OK: {len(dets)} dets, 推理 "
          f"{(lat.get('total_ms') or lat.get('avg_ms', 0)):.2f} ms"
          + (f" @ {lat['clock_mhz']:.0f}MHz" if lat.get('clock_mhz') else ""))


def _vlm_selftest():
    """多模态链路自检 (不弹 GUI): python3 qcom-npu-test --vlm-selftest

    不需要摄像头也不弹窗: 拿自带的 bus.jpg 按摄像头同分辨率重采样成 1280x720
    再编码成 JPEG, 走一遍真实的 /v1/chat/completions 图像请求. 需要 qwen35 已
    启动 (sudo systemctl start qwen35).
    """
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    img = os.path.join(IMAGE_DIRS[0], "bus.jpg")
    assert os.path.isfile(img), f"找不到测试图: {img}"

    mid = vlm_probe(timeout=5.0)
    print(f"视觉服务: {vlm_display_name(mid) if mid else '未运行'}")
    assert mid, "Qwen3.5 未就绪, 先执行: sudo systemctl start qwen35"

    # 与摄像头帧走同一条路径: raw RGB -> pixbuf -> JPEG
    pb = GdkPixbuf.Pixbuf.new_from_file_at_scale(img, CAM_W, CAM_H, False)
    ok, jpeg = pb.save_to_bufferv("jpeg", ["quality"], [VLM_JPEG_QUALITY])
    assert ok and jpeg, "JPEG 编码失败"
    assert jpeg[:3] == b"\xff\xd8\xff", jpeg[:3]
    print(f"JPEG: {pb.get_width()}x{pb.get_height()}, {len(jpeg) / 1024:.0f} KB")

    t0 = time.monotonic()
    text, meta = vlm_ask_image(jpeg, VLM_PROMPT, 64)
    elapsed = time.monotonic() - t0
    assert text and text != "(模型没有输出内容)", "模型无输出"
    assert meta.get("mode") == "image", f"服务端没走图像通路: {meta}"
    print(f"回答 ({elapsed:.1f}s): {text}")
    print(f"指标: {vlm_meta_line(meta, elapsed, len(jpeg))}")
    print("VLM SELFTEST OK")


def main():
    win = NPUTestWindow()
    win.connect("destroy", lambda w: w._cam_stop())
    win.connect("destroy", lambda w: w._mm_stop())
    win.connect("destroy", Gtk.main_quit)
    # --camera: 直接开最大化摄像头窗口, 不用点 Tab (大屏展示/无人值守用)
    if "--camera" in sys.argv:
        win.maximize()
        win.notebook.set_current_page(win._cam_tab_idx)
        GLib.idle_add(win.on_cam_clicked, None)
    # --multimodal: 直接开多模态页并启动预览
    if "--multimodal" in sys.argv:
        win.maximize()
        win.notebook.set_current_page(win._mm_tab_idx)
        GLib.idle_add(win.on_mm_preview_clicked, None)
    win.show_all()
    Gtk.main()


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        _selftest()
    elif "--vlm-selftest" in sys.argv:
        _vlm_selftest()
    else:
        main()
