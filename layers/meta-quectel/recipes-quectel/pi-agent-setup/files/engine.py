# pi-agent-setup engine -- network check, runtime download, pi install, config
#
# Everything here runs as the logged-in desktop user. Nothing needs root:
#   <home>/.local/share/pi/node/current   node 22 runtime (official tarball)
#   <home>/.local/share/pi/npm-global     npm prefix, holds bin/pi
#   <home>/.local/bin/pi                  symlink into the prefix
#   <home>/.pi/agent/{models,auth,settings}.json
#
# That is deliberate. This board ships Debian GNOME and Ubuntu GNOME images
# where /usr/bin/curl does not exist (Debian 13 sample rootfs) and the desktop
# user is unprivileged; an installer that needed apt/polkit would have to ask
# for a password on every run and would still lose its files on the next
# firmware flash. downloads go through urllib, which is present in both
# rootfs images (python3 + _ssl + ca-certificates are all guaranteed, the GUI
# itself is python).

import errno
import hashlib
import inspect
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request

# Board language, same contract as the wizard: the msgid is the Chinese source
# text, a Chinese (or C/POSIX) session shows it unchanged, everything else reads
# /usr/share/locale/en/LC_MESSAGES/pi-agent-setup.mo. This module is imported by
# the GTK wizard AND run directly (`engine.py status` from the shell launcher),
# so it must not depend on gi and must survive ln-i18n being absent.
sys.path.insert(0, "/usr/share/ln-i18n")
try:
    import ln_i18n  # noqa: E402
    _ = ln_i18n.setup("pi-agent-setup")
except ImportError:                      # ln-i18n not installed: keep source text
    def _(message):
        return message

# --------------------------------------------------------------------------
# layout
# --------------------------------------------------------------------------
# PI_AGENT_HOME exists so this can be exercised in a sandbox (and so the CLI
# can install for a different user than the one invoking it, e.g. from ssh).
HOME_DIR = os.environ.get("PI_AGENT_HOME") or os.path.expanduser("~")

PI_ROOT = os.path.join(HOME_DIR, ".local/share/pi")
NODE_DIR = os.path.join(PI_ROOT, "node")           # node/current -> node/vX.Y.Z
NPM_PREFIX = os.path.join(PI_ROOT, "npm-global")   # bin/pi + lib/node_modules
BIN_DIR = os.path.join(HOME_DIR, ".local/bin")
CACHE_DIR = os.path.join(HOME_DIR, ".cache/pi-agent-setup")
AGENT_DIR = os.path.join(HOME_DIR, ".pi/agent")
STATE_PATH = os.path.join(
    os.environ.get("XDG_CONFIG_HOME") or os.path.join(HOME_DIR, ".config"),
    "pi-agent-setup/state.json")
MARKER_PATH = os.path.join(AGENT_DIR, "pi-agent-setup.json")

PI_PACKAGE = "@earendil-works/pi-coding-agent"
PROVIDER_ID = "pi-setup"

# Board-side skill set. Shipped by the firmware as a *pointer only*: this image
# does not contain a single file from the repository, so distributing the image
# does not redistribute it (the repo carries no licence). The content lives in
# the user's own checkout under $HOME and is fetched only after they say yes.
SKILLS_SLUG = os.environ.get("PI_SKILLS_SLUG", "super617/pi-skills")
SKILLS_REPO = os.environ.get("PI_SKILLS_REPO", "https://github.com/%s" % SKILLS_SLUG)
SKILLS_DIR = os.path.join(AGENT_DIR, "pi-skills")
SKILLS_LINK_DIR = os.path.join(AGENT_DIR, "skills")
SKILLS_TIMEOUT = int(os.environ.get("PI_SKILLS_NET_TIMEOUT", "12"))

# Node 22 is the oldest LTS pi accepts (package.json: engines.node >= 22.19.0).
NODE_DIST_BASE = os.environ.get("PI_AGENT_NODE_MIRROR", "https://nodejs.org/dist")
NODE_LINE = os.environ.get("PI_AGENT_NODE_LINE", "latest-v22.x")
NPM_REGISTRY = os.environ.get("PI_AGENT_NPM_REGISTRY", "").strip()

# Mirrors worth trying when the direct download fails; only used with the
# "use mirror" switch, never silently.
MIRROR_NODE = "https://npmmirror.com/mirrors/node"
MIRROR_NPM = "https://registry.npmmirror.com"


class SetupError(Exception):
    """An error with a message meant for the person in front of the board."""


# Progress-callback stages, language independent: the wizard drives its progress
# bar off these instead of matching the (translated) message text.
STAGE_DOWNLOAD = "download"
STAGE_NODE_DONE = "node-done"
STAGE_PI = "pi"


def _step(steps, text, stage=None):
    """Emit one progress line, with an optional stage tag.

    Callbacks written against the old one-argument signature still work: the
    stage is only handed over when the callable accepts it.
    """
    if stage is None:
        steps(text)
        return
    try:
        takes_stage = len(inspect.signature(steps).parameters) >= 2
    except (TypeError, ValueError):
        takes_stage = False
    if takes_stage:
        steps(text, stage)
    else:
        steps(text)


# --------------------------------------------------------------------------
# small helpers
# --------------------------------------------------------------------------
def log(msg):
    print(msg, flush=True)


def _request(url, timeout=20, headers=None):
    req = urllib.request.Request(url, headers=headers or {})
    req.add_header("User-Agent", "pi-agent-setup/1.0 (Quectel PI H1)")
    return urllib.request.urlopen(req, timeout=timeout)


def read_url(url, timeout=20, limit=1 << 20):
    with _request(url, timeout=timeout) as resp:
        return resp.read(limit)


def arch_name():
    """Node's own name for this CPU."""
    machine = os.uname().machine
    return {
        "aarch64": "arm64",
        "arm64": "arm64",
        "x86_64": "x64",
        "amd64": "x64",
        "armv7l": "armv7l",
    }.get(machine, machine)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, dest, progress=None, timeout=30, expect_sha256=None):
    """Stream url to dest. progress(done, total) is called as data arrives."""
    part = dest + ".part"
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.exists(dest) and expect_sha256:
        if sha256_file(dest) == expect_sha256:
            if progress:
                progress(os.path.getsize(dest), os.path.getsize(dest))
            return dest
        os.unlink(dest)
    with _request(url, timeout=timeout) as resp, open(part, "wb") as handle:
        total = int(resp.headers.get("Content-Length") or 0)
        done = 0
        last = 0.0
        while True:
            chunk = resp.read(1 << 16)
            if not chunk:
                break
            handle.write(chunk)
            done += len(chunk)
            now = time.monotonic()
            if progress and (now - last > 0.15 or done == total):
                last = now
                progress(done, total)
    if expect_sha256:
        actual = sha256_file(part)
        if actual != expect_sha256:
            os.unlink(part)
            raise SetupError(
                _("下载校验失败（sha256 不匹配）\n  期望 %s\n  实际 %s")
                % (expect_sha256, actual))
    os.replace(part, dest)
    return dest


def run(cmd, env=None, cwd=None, log_path=None, timeout=None):
    """Run a command, tee its output to stdout (the GUI log) and return rc."""
    merged = dict(os.environ)
    if env:
        merged.update(env)
    proc = subprocess.Popen(
        cmd, cwd=cwd, env=merged, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1)
    tail = []
    with open(log_path, "a") if log_path else _nullcontext() as sink:
        for line in proc.stdout:
            line = line.rstrip("\n")
            log("  " + line)
            if sink:
                sink.write(line + "\n")
            tail.append(line)
            del tail[:-40]
    rc = proc.wait(timeout=timeout)
    return rc, "\n".join(tail)


class _nullcontext:
    def __enter__(self):
        return None

    def __exit__(self, *exc):
        return False


# --------------------------------------------------------------------------
# network
# --------------------------------------------------------------------------
def check_network():
    """Return (ok, human readable detail). Never raises."""
    targets = [
        ("nodejs.org", NODE_DIST_BASE.rstrip("/") + "/" + NODE_LINE + "/SHASUMS256.txt"),
        ("npm registry",
         (NPM_REGISTRY or "https://registry.npmjs.org") + "/" + PI_PACKAGE.replace("/", "%2f") + "/latest"),
    ]
    results = []
    ok_any = False
    for name, url in targets:
        started = time.monotonic()
        try:
            body = read_url(url, timeout=12, limit=4096)
        except Exception as exc:              # noqa: BLE001 - report, never crash
            results.append((name, False, "%s: %s" % (url, exc), 0.0))
            continue
        elapsed = (time.monotonic() - started) * 1000.0
        ok = bool(body)
        results.append((name, ok, url, elapsed))
        ok_any = ok_any or ok
    lines = []
    for name, ok, detail, elapsed in results:
        if ok:
            lines.append("✓ %s  (%.0f ms)" % (name, elapsed))
        else:
            lines.append("✗ %s  %s" % (name, detail))
    return ok_any, "\n".join(lines)


# --------------------------------------------------------------------------
# node
# --------------------------------------------------------------------------
def node_version_for_pi():
    """Version of the node runtime inside <PI_ROOT>/node/current, or None."""
    node = os.path.join(NODE_DIR, "current/bin/node")
    if not os.path.exists(node):
        return None
    try:
        out = subprocess.run([node, "-v"], capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return None
    return out.stdout.strip() or None


def ensure_node(use_mirror=False, steps=None):
    """Install the official node tarball under PI_ROOT/node. Returns node path."""
    steps = steps or (lambda text: log(text))
    existing = node_version_for_pi()
    if existing:
        _step(steps, _("node 已就绪：%s") % existing, STAGE_NODE_DONE)
        return os.path.join(NODE_DIR, "current/bin/node")

    bases = [NODE_DIST_BASE]
    if use_mirror and MIRROR_NODE not in bases:
        bases.append(MIRROR_NODE)
    last_error = None
    for base in bases:
        try:
            _step(steps, _("获取 node 版本信息：%s") % base)
            sums = read_url(base.rstrip("/") + "/" + NODE_LINE + "/SHASUMS256.txt",
                            timeout=25).decode("utf-8", "replace")
        except Exception as exc:              # noqa: BLE001
            last_error = "%s: %s" % (base, exc)
            _step(steps, _("  失败：%s") % last_error)
            continue
        pattern = re.compile(
            r"^([0-9a-f]{64})\s+(node-v([0-9.]+)-linux-%s\.tar\.xz)$" % re.escape(arch_name()),
            re.MULTILINE)
        match = pattern.search(sums)
        if not match:
            last_error = _("%s 里没有 linux-%s 的 node 包") % (base, arch_name())
            _step(steps, _("  失败：%s") % last_error)
            continue
        digest, filename, version = match.group(1), match.group(2), match.group(3)
        url = "%s/%s/%s" % (base.rstrip("/"), "v" + version, filename)
        target_dir = os.path.join(NODE_DIR, "v" + version)
        tarball = os.path.join(CACHE_DIR, filename)
        _step(steps, _("下载 node v%s（%.1f MB 级）") % (version, 0), STAGE_DOWNLOAD)
        try:
            download(url, tarball, expect_sha256=digest,
                     progress=lambda done, total: _progress("node", done, total))
        except Exception as exc:              # noqa: BLE001
            last_error = "%s: %s" % (url, exc)
            _step(steps, _("  下载失败：%s") % last_error)
            continue
        _step(steps, _("解包 node v%s") % version)
        if os.path.isdir(target_dir):
            shutil.rmtree(target_dir)
        os.makedirs(target_dir, exist_ok=True)
        with tarfile.open(tarball, "r:xz") as archive:
            _safe_extract(archive, target_dir)
        inner = os.path.join(target_dir, filename[: -len(".tar.xz")])
        if not os.path.isdir(inner):
            # Some mirrors repack without the top directory.
            inner = target_dir
        _replace_symlink(os.path.join(NODE_DIR, "current"), inner)
        node_bin = os.path.join(NODE_DIR, "current/bin/node")
        if not os.path.exists(node_bin):
            last_error = _("解包后找不到 bin/node")
            _step(steps, _("  失败：%s") % last_error)
            continue
        os.chmod(node_bin, 0o755)
        _step(steps, _("node 安装完成：%s") % node_version_for_pi(), STAGE_NODE_DONE)
        return node_bin
    raise SetupError(_("node 运行时安装失败：%s")
                     % (last_error or _("所有来源都不可用")))


def _progress(what, done, total):
    if total:
        log("[%s] %5.1f%%  %.1f/%.1f MB"
            % (what, done * 100.0 / total, done / 1e6, total / 1e6))
    else:
        log("[%s] %.1f MB" % (what, done / 1e6))


def _safe_extract(archive, dest):
    dest_abs = os.path.abspath(dest)
    for member in archive.getmembers():
        target = os.path.abspath(os.path.join(dest_abs, member.name))
        if not (target == dest_abs or target.startswith(dest_abs + os.sep)):
            raise SetupError(_("压缩包包含越界路径：%s") % member.name)
        if member.issym() or member.islnk():
            link = os.path.abspath(os.path.join(os.path.dirname(target), member.linkname))
            if not link.startswith(dest_abs):
                raise SetupError(_("压缩包包含越界链接：%s") % member.name)
        archive.extract(member, dest_abs)


def _replace_symlink(path, target):
    if os.path.islink(path) or os.path.exists(path):
        if os.path.islink(path):
            os.unlink(path)
        elif os.path.isdir(path):
            shutil.rmtree(path)
        else:
            os.unlink(path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    os.symlink(os.path.relpath(target, os.path.dirname(path)), path)


# --------------------------------------------------------------------------
# pi
# --------------------------------------------------------------------------
def installed_pi():
    """Path of the pi launcher this installer owns, or None."""
    candidate = os.path.join(NPM_PREFIX, "bin/pi")
    if os.path.exists(candidate):
        return candidate
    return None


def pi_version(pi_path=None):
    pi_path = pi_path or installed_pi()
    if not pi_path:
        return None
    node = os.path.join(NODE_DIR, "current/bin/node")
    try:
        out = subprocess.run([node, pi_path, "--version"],
                             capture_output=True, text=True, timeout=90)
    except (OSError, subprocess.SubprocessError):
        return None
    text = (out.stdout or out.stderr).strip().splitlines()
    return text[0].strip() if text else None


def install_pi(use_mirror=False, steps=None):
    steps = steps or (lambda text: log(text))
    node_bin = ensure_node(use_mirror=use_mirror, steps=steps)
    node_bin_dir = os.path.dirname(node_bin)
    npm = os.path.join(node_bin_dir, "npm")
    if not os.path.exists(npm):
        raise SetupError(_("node 包里没有 npm：%s") % npm)

    env = {
        "PATH": node_bin_dir + ":" + os.environ.get("PATH", "/usr/bin:/bin"),
        # npm 11 gates freshly published versions behind a release age; the pi
        # docs pass --min-release-age=0 for exactly this reason. Setting it in
        # the environment keeps it out of argv where an older npm would refuse
        # to start.
        "npm_config_min_release_age": "0",
        "npm_config_fund": "false",
        "npm_config_audit": "false",
        "npm_config_update_notifier": "false",
    }
    registry = NPM_REGISTRY or (MIRROR_NPM if use_mirror else "")
    cmd = [npm, "install", "-g", "--prefix", NPM_PREFIX, "--ignore-scripts"]
    if registry:
        cmd += ["--registry", registry]
    cmd += ["%s@latest" % PI_PACKAGE]

    os.makedirs(NPM_PREFIX, exist_ok=True)
    logdir = os.path.join(CACHE_DIR, "install.log")
    os.makedirs(CACHE_DIR, exist_ok=True)
    open(logdir, "w").close()

    _step(steps, _("安装 %s（npm，安装到 %s）") % (PI_PACKAGE, NPM_PREFIX), STAGE_PI)
    rc, tail = run(cmd, env=env, log_path=logdir)
    if rc != 0 and registry:
        _step(steps, _("镜像安装失败（rc=%d），改用官方源重试") % rc, STAGE_PI)
        cmd = [part for part in cmd if part not in ("--registry", registry)]
        rc, tail = run(cmd, env=env, log_path=logdir)
    if rc != 0:
        raise SetupError(_("npm 安装失败（退出码 %d）：\n%s") % (rc, tail[-1200:]))

    launcher = os.path.join(NPM_PREFIX, "bin/pi")
    if not os.path.exists(launcher):
        raise SetupError(_("安装结束但找不到启动器：%s") % launcher)
    os.chmod(launcher, 0o755)
    os.makedirs(BIN_DIR, exist_ok=True)
    write_launcher(node_bin, launcher)

    version = pi_version(launcher)
    if not version:
        raise SetupError(_("pi 已安装但无法运行：%s --version 没有输出") % launcher)
    _step(steps, _("pi 安装成功：%s") % version, STAGE_PI)

    write_marker({
        "pi_version": version,
        "node_version": node_version_for_pi(),
        "prefix": NPM_PREFIX,
        "installed_at": int(time.time()),
    })
    return version


PI_LAUNCHER = """#!/bin/sh
# pi launcher written by pi-agent-setup.
#
# npm already puts a 'pi' into the prefix's bin/, but that file is a symlink to
# a '#! /usr/bin/env node' script, and the Node.js runtime this installer put
# in place is deliberately *not* on PATH (it lives under
# {root}/node/current/bin, so that the board's own node, if any, is never
# shadowed and so that removing this directory removes everything). Running the
# npm symlink directly therefore dies with \"SyntaxError: Unexpected token '?'\"
# or a missing-module stack trace, depending on the system node.
#
# So ~/.local/bin/pi is this wrapper instead: it puts the private runtime first
# on PATH and hands over to the npm-installed CLI.
PI_NODE_BIN={node_bin_dir}
PATH="$PI_NODE_BIN:$PATH"
export PATH
exec {launcher} "$@"
"""


def write_launcher(node_bin, npm_launcher):
    """Install the PATH-setting wrapper as <home>/.local/bin/pi."""
    path = os.path.join(BIN_DIR, "pi")
    body = PI_LAUNCHER.format(root=PI_ROOT,
                              node_bin_dir=os.path.dirname(node_bin),
                              launcher=npm_launcher)
    tmp = path + ".tmp"
    with open(tmp, "w") as handle:
        handle.write(body)
    os.chmod(tmp, 0o755)
    os.replace(tmp, path)
    return path


def write_marker(data):
    os.makedirs(AGENT_DIR, exist_ok=True)
    tmp = MARKER_PATH + ".tmp"
    with open(tmp, "w") as handle:
        json.dump(data, handle, indent=2)
        handle.write("\n")
    os.replace(tmp, MARKER_PATH)


# --------------------------------------------------------------------------
# configuration: models.json / auth.json / settings.json
# --------------------------------------------------------------------------
API_TYPES = [
    ("openai-completions", _("OpenAI 兼容（大部分中转/自建服务）")),
    ("openai-responses", "OpenAI Responses API"),
    ("anthropic-messages", "Anthropic Messages API"),
    ("google-generative-ai", "Google Generative AI"),
]


def _read_json(path, fallback):
    try:
        with open(path) as handle:
            text = handle.read().strip()
        if not text:
            return fallback
        return json.loads(text)
    except FileNotFoundError:
        return fallback
    except (OSError, ValueError):
        # A hand-edited file with a trailing comma is a very common state; keep
        # a copy instead of destroying it.
        try:
            shutil.copyfile(path, path + ".bak-%d" % int(time.time()))
        except OSError:
            pass
        return fallback


def _write_json(path, data, mode=0o644):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as handle:
        json.dump(data, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    os.chmod(tmp, mode)
    os.replace(tmp, path)


def _model_endpoints(base, api_key, api):
    """Candidate (url, headers) pairs that can list models, best first.

    There is no single API for this, so several shapes are tried and the first
    one that parses wins:
      OpenAI-compatible   GET /models            -> {"data": [{"id": ...}]}
      Anthropic           GET /models            -> {"data": [{"id": ...}]}
      Google              GET /models?key=...    -> {"models": [{"name": ...}]}
      Ollama              GET /api/tags          -> {"models": [{"name": ...}]}
      llama.cpp server    GET /v1/models         -> {"data": [{"id": ...}]}
    """
    out = []
    if api == "anthropic-messages":
        out.append((base + "/models", {
            "x-api-key": api_key, "anthropic-version": "2023-06-01"}))
    elif api == "google-generative-ai":
        sep = "&" if "?" in base else "?"
        out.append((base + "/models" + sep + "key=" + api_key, {}))
    else:
        headers = {"Authorization": "Bearer " + api_key}
        out.append((base + "/models", headers))
        # Some gateways only expose the OpenAI path under /v1.
        if not base.rstrip("/").endswith("/v1"):
            out.append((base + "/v1/models", headers))
        out.append((base + "/api/tags", headers))
    return out


def _parse_models(text):
    """Pull model ids out of any of the listing shapes above.

    Returns [(id, extra_label_or_None)], sorted, de-duplicated. Never raises:
    an unrecognised body yields an empty list so the caller can try the next
    URL instead of showing a stack trace.
    """
    try:
        data = json.loads(text)
    except ValueError:
        return []
    if not isinstance(data, dict):
        return []

    found = []
    for key in ("data", "models"):
        entries = data.get(key)
        if not isinstance(entries, list):
            continue
        for entry in entries:
            if isinstance(entry, str):
                found.append((entry, None))
                continue
            if not isinstance(entry, dict):
                continue
            # OpenAI uses "id"; Google and Ollama use "name"; Google's name is
            # prefixed with "models/", which is not part of the id you pass on.
            ident = entry.get("id") or entry.get("name") or entry.get("model")
            if not isinstance(ident, str) or not ident:
                continue
            if ident.startswith("models/"):
                ident = ident[len("models/"):]
            # Ollama reports a byte size; a gigabyte figure is a useful hint.
            label = None
            owned = entry.get("owned_by") or entry.get("provider")
            if isinstance(owned, str) and owned:
                label = owned
            size = entry.get("size")
            if isinstance(size, (int, float)) and size > 0:
                label = "%s GB" % round(size / 1e9, 1)
            found.append((ident, label))

    seen = set()
    result = []
    for ident, label in found:
        if ident in seen:
            continue
        seen.add(ident)
        result.append((ident, label))
    result.sort(key=lambda item: item[0].lower())
    return result


def list_models(base_url, api_key, api="openai-completions", timeout=15):
    """Ask the endpoint which models it offers.

    Returns (models, detail): models is [(id, extra_label_or_None)], detail is a
    human readable explanation of what was tried, so the wizard can say *why*
    the list is empty instead of silently showing nothing.
    """
    if not base_url or not api_key:
        raise SetupError(_("填写 API URL 和 API Key 才能探测模型列表"))
    base = base_url.strip()
    if "://" not in base:
        base = "https://" + base
    base = base.rstrip("/")
    if base.endswith("/chat/completions"):
        base = base[: -len("/chat/completions")]

    detail = []
    for url, headers in _model_endpoints(base, api_key, api):
        try:
            body = read_url(url, timeout=timeout, limit=1 << 20)
        except urllib.error.HTTPError as exc:
            # Keep failures short: several candidates are tried, and a wall of
            # HTML error pages in the wizard is noise. They are only shown when
            # nothing worked at all.
            detail.append("HTTP %s  %s" % (exc.code, url))
            continue
        except Exception as exc:              # noqa: BLE001
            detail.append("%s: %s" % (url, exc))
            continue
        models = _parse_models(body.decode("utf-8", "replace"))
        if models:
            return models, _("✓ %s（%d 个模型）") % (url, len(models))
        detail.append(_("%s 返回的内容里没有模型列表") % url)
    return [], "\n".join(detail) or _("无法连接")


def test_endpoint(base_url, api_key, model, api="openai-completions"):
    """Cheap reachability + auth probe. Returns (ok, detail)."""
    base = base_url.rstrip("/")
    if base.endswith("/chat/completions"):
        base = base[: -len("/chat/completions")]
    candidates = []
    if api == "anthropic-messages":
        candidates.append((base + "/messages", {"x-api-key": api_key,
                                                "anthropic-version": "2023-06-01"}))
    elif api == "google-generative-ai":
        sep = "&" if "?" in base else "?"
        candidates.append((base + "/models" + sep + "key=" + api_key, {}))
    else:
        headers = {"Authorization": "Bearer " + api_key}
        candidates.append((base + "/models", headers))
        candidates.append((base + "/chat/completions", headers))

    detail = []
    for url, headers in candidates:
        try:
            body = read_url(url, timeout=15, limit=1 << 16)
        except urllib.error.HTTPError as exc:
            snippet = exc.read(400).decode("utf-8", "replace").strip().replace("\n", " ")
            detail.append("HTTP %s %s %s" % (exc.code, url, snippet[:200]))
            continue
        except Exception as exc:              # noqa: BLE001
            detail.append("%s: %s" % (url, exc))
            continue
        text = body.decode("utf-8", "replace")
        if model and model in text:
            detail.append(_("✓ %s（返回的模型列表里含 %s）") % (url, model))
        else:
            detail.append(
                _("✓ %s（可访问，未在模型列表里匹配到 %s，端点可能只列出部分模型）")
                % (url, model))
        return True, "\n".join(detail)
    return False, "\n".join(detail) or _("无法连接")


def configure(base_url, api_key, model, api="openai-completions",
              provider_name=None, context_window=128000, max_tokens=8192):
    if provider_name is None:
        provider_name = _("自定义端点")
    if not base_url or not api_key or not model:
        raise SetupError(_("API URL、API Key、模型 ID 都不能为空"))
    if "://" not in base_url:
        base_url = "https://" + base_url
    base_url = base_url.rstrip("/")
    # Tolerate the two shapes people paste: the bare base and the full
    # /chat/completions endpoint. models.json wants the base.
    if base_url.endswith("/chat/completions"):
        base_url = base_url[: -len("/chat/completions")]

    models_path = os.path.join(AGENT_DIR, "models.json")
    auth_path = os.path.join(AGENT_DIR, "auth.json")
    settings_path = os.path.join(AGENT_DIR, "settings.json")

    models = _read_json(models_path, {})
    if not isinstance(models, dict):
        models = {}
    models.setdefault("$schema", "https://pi.dev/schemas/models.schema.json")
    providers = models.get("providers")
    if not isinstance(providers, dict):
        providers = {}
    providers[PROVIDER_ID] = {
        "name": provider_name,
        "baseUrl": base_url,
        "api": api,
        "models": [{
            "id": model,
            "name": model,
            "reasoning": False,
            "input": ["text"],
            "contextWindow": int(context_window),
            "maxTokens": int(max_tokens),
            "cost": {"input": 0, "output": 0, "cacheRead": 0, "cacheWrite": 0},
        }],
    }
    models["providers"] = providers
    _write_json(models_path, models)

    auth = _read_json(auth_path, {})
    if not isinstance(auth, dict):
        auth = {}
    auth[PROVIDER_ID] = {"type": "api_key", "key": api_key}
    _write_json(auth_path, auth, mode=0o600)

    settings = _read_json(settings_path, {})
    if not isinstance(settings, dict):
        settings = {}
    settings["defaultProvider"] = PROVIDER_ID
    settings["defaultModel"] = model
    enabled = settings.get("enabledModels")
    if not isinstance(enabled, list):
        enabled = []
    entry = "%s/%s" % (PROVIDER_ID, model)
    if entry not in enabled:
        enabled.append(entry)
    settings["enabledModels"] = enabled
    _write_json(settings_path, settings)

    marker = _read_json(MARKER_PATH, {}) or {}
    marker["config"] = {
        "provider": PROVIDER_ID,
        "baseUrl": base_url,
        "model": model,
        "api": api,
        "configured_at": int(time.time()),
    }
    write_marker(marker)
    return {"models": models_path, "auth": auth_path, "settings": settings_path}


# --------------------------------------------------------------------------
# state for the first-run prompt
# --------------------------------------------------------------------------
DEFAULT_SNOOZE_DAYS = 7


def read_state():
    state = _read_json(STATE_PATH, {})
    return state if isinstance(state, dict) else {}


def write_state(**updates):
    state = read_state()
    state.update(updates)
    state["updated_at"] = int(time.time())
    _write_json(STATE_PATH, state)
    return state


def snooze(days=DEFAULT_SNOOZE_DAYS):
    return write_state(status="snoozed",
                       snooze_until=int(time.time()) + days * 86400,
                       snooze_days=days)


def decline():
    return write_state(status="declined", snooze_until=0)


def is_configured():
    marker = _read_json(MARKER_PATH, {}) or {}
    return bool(marker.get("config")) and bool(installed_pi())


def should_prompt():
    """True when the first-run window should appear on login."""
    if is_configured():
        return False
    state = read_state()
    if state.get("status") == "declined":
        return False
    until = state.get("snooze_until") or 0
    return int(time.time()) >= int(until)


# --------------------------------------------------------------------------
# board skills (opt-in, user's own checkout)
# --------------------------------------------------------------------------
# Design rules, and they are the whole point of this section:
#
#   * The firmware ships NO skill content. The repository these skills come from
#     carries no licence, so putting even one file into the image would mean
#     redistributing it. Everything here is fetched into $HOME at runtime.
#   * Nothing is fetched without an explicit yes. install() asks once, and the
#     launcher's update check asks again before pulling a newer revision.
#   * "Declined" is sticky: once the person says no, neither the installer nor
#     the login hook offers again until they run `engine.py skills-install`.
#   * Offline is not an error. A board with no network must behave exactly like
#     one that is up to date: silent, no dialog, no failure in the launcher.

def _skills_http(url):
    """GET url, return text, raise SetupError on any problem."""
    try:
        with _request(url, timeout=SKILLS_TIMEOUT) as resp:
            return resp.read(4096).decode("utf-8", "replace")
    except Exception as exc:
        raise SetupError(_("无法访问 %s：%s") % (url, exc))


def skills_local_version():
    """Version of the installed checkout, or '' when there is none."""
    path = os.path.join(SKILLS_DIR, "VERSION")
    try:
        with open(path, encoding="utf-8") as handle:
            return handle.read().strip()
    except OSError:
        return ""


def skills_remote_version():
    """Version published upstream, or '' when offline.

    Reads VERSION over plain HTTP instead of asking git: this runs on every
    launch, and `git ls-remote` is both slower and fails differently (it wants a
    git binary the image may not have).
    """
    url = "https://raw.githubusercontent.com/%s/main/VERSION" % SKILLS_SLUG
    try:
        return _skills_http(url).strip()
    except SetupError:
        return ""


def _version_tuple(text):
    parts = []
    for chunk in str(text or "").split("."):
        digits = "".join(c for c in chunk if c.isdigit())
        parts.append(int(digits) if digits else 0)
    return tuple(parts) or (0,)


def skills_update_available():
    """True/False, or None when it cannot be determined (offline).

    A checkout that is *ahead* of upstream is not an update either -- the
    person may be tracking a branch of their own.
    """
    remote = skills_remote_version()
    if not remote:
        return None
    local = skills_local_version()
    if not local:
        return True
    return _version_tuple(remote) > _version_tuple(local)


def _link_skills(dirname=None):
    """(Re)point ~/.pi/agent/skills/<name> at the checkout.

    pi reads user skills from that directory, so linking rather than copying is
    what makes an update take effect without touching pi's settings. Only links
    this function created are removed; a directory the person put there
    themselves is left alone.
    """
    source = dirname or os.path.join(SKILLS_DIR, "skills")
    if not os.path.isdir(source):
        return 0
    os.makedirs(SKILLS_LINK_DIR, exist_ok=True)
    linked = 0
    for name in sorted(os.listdir(source)):
        src = os.path.join(source, name)
        if not os.path.isdir(src):
            continue
        link = os.path.join(SKILLS_LINK_DIR, name)
        if os.path.islink(link):
            os.unlink(link)
        elif os.path.exists(link):
            # A real directory of the same name belongs to the user.
            continue
        os.symlink(os.path.relpath(src, SKILLS_LINK_DIR), link)
        linked += 1
    return linked


def skills_install(progress=None, force=False):
    """Fetch the skill set into $HOME. Returns the installed version.

    `git` is preferred because it makes later updates incremental; without it
    the tree is downloaded as a tarball. The image may not carry git, and a
    missing git must not make the feature unavailable.
    """
    progress = progress or (lambda text: log(text))
    remote = skills_remote_version()
    if not remote:
        raise SetupError(_("无法读取远端版本（网络不通或仓库不存在）"))
    if not force and skills_local_version() == remote:
        _link_skills()
        return remote

    parent = os.path.dirname(SKILLS_DIR) or AGENT_DIR
    os.makedirs(parent, exist_ok=True)
    staging = SKILLS_DIR + ".new"
    if os.path.isdir(staging):
        shutil.rmtree(staging)

    git = shutil.which("git")
    if git:
        progress(_("拉取技能包（git）…"))
        rc, tail = run([git, "clone", "--quiet", "--depth", "1", SKILLS_REPO, staging])
        if rc != 0:
            shutil.rmtree(staging, ignore_errors=True)
            raise SetupError(_("git clone 失败（rc=%d）：%s") % (rc, tail[-600:]))
    else:
        progress(_("拉取技能包（压缩包）…"))
        archive = os.path.join(CACHE_DIR, "pi-skills.tar.gz")
        download("https://codeload.github.com/%s/tar.gz/refs/heads/main" % SKILLS_SLUG,
                 archive)
        os.makedirs(staging, exist_ok=True)
        with tarfile.open(archive, "r:gz") as tar:
            _safe_extract(tar, staging)
        # codeload wraps everything in a single <repo>-<ref>/ directory.
        entries = [e for e in os.listdir(staging) if not e.startswith(".")]
        if len(entries) == 1 and os.path.isdir(os.path.join(staging, entries[0])):
            inner = os.path.join(staging, entries[0])
            for name in os.listdir(inner):
                os.replace(os.path.join(inner, name), os.path.join(staging, name))
            os.rmdir(inner)

    version = skills_local_version() if os.path.isdir(SKILLS_DIR) else ""
    # Swap only after the new tree is complete, so a failed download never
    # leaves the board with no skills at all.
    if os.path.isdir(SKILLS_DIR):
        retired = SKILLS_DIR + ".old"
        shutil.rmtree(retired, ignore_errors=True)
        os.replace(SKILLS_DIR, retired)
    os.replace(staging, SKILLS_DIR)
    shutil.rmtree(SKILLS_DIR + ".old", ignore_errors=True)

    linked = _link_skills()
    version = skills_local_version()
    write_state(skills_version=version,
                skills_installed_at=int(time.time()),
                skills_status="installed")
    progress(_("技能包已安装：%s（%d 个技能）") % (version or "?", linked))
    return version


def skills_sync():
    """Re-point the links at the installed checkout. Cheap, runs every launch."""
    return _link_skills()


def skills_decline():
    """Remember a no, so neither the installer nor the login hook asks again."""
    return write_state(skills_status="declined")


def skills_should_ask():
    """True when the user has never answered the skill question.

    Both "installed" and "declined" are answers; only a missing status means
    the question has not been put yet.
    """
    return not (read_state().get("skills_status") in ("installed", "declined"))


def skills_status():
    state = read_state()
    return {
        "installed": bool(skills_local_version()),
        "version": skills_local_version(),
        "path": SKILLS_DIR,
        "links": SKILLS_LINK_DIR,
        "state": state.get("skills_status"),
        "checked_at": state.get("skills_checked_at"),
    }


def skills_autostart(ask_func):
    """Login hook: relink, then offer an update if one exists.

    `ask_func(local, remote)` returns "now" / "later" / "never", or None when
    there is no GUI or terminal to ask in. Nothing happens unless it returns
    "now" -- an update is never pulled silently.

    Silent by design in the common cases: no checkout and declined, already up
    to date, or offline. A login must not produce a dialog just because the
    network is down.
    """
    skills_sync()
    if read_state().get("skills_status") == "declined":
        return None
    available = skills_update_available()
    write_state(skills_checked_at=int(time.time()))
    if not available:
        return None
    first_install = not skills_local_version()
    answer = ask_func(skills_local_version(), skills_remote_version(), first_install)
    if answer == "never":
        skills_decline()
        return None
    if answer != "now":
        return None
    try:
        return skills_install()
    except SetupError as exc:
        log(_("技能包更新失败：%s") % exc)
        return None


def status():
    marker = _read_json(MARKER_PATH, {}) or {}
    return {
        "pi_installed": bool(installed_pi()),
        "pi_path": installed_pi(),
        "pi_version": pi_version() if installed_pi() else None,
        "node_version": node_version_for_pi(),
        "configured": is_configured(),
        "config": marker.get("config"),
        "state": read_state(),
        "provider_id": PROVIDER_ID,
        "prefix": NPM_PREFIX,
        "launcher": os.path.join(BIN_DIR, "pi"),
        "skills": skills_status(),
    }


def uninstall():
    """Remove what install() created. Keeps ~/.pi/agent (sessions, config)."""
    removed = []
    wrapper = os.path.join(BIN_DIR, "pi")
    if os.path.islink(wrapper) or os.path.isfile(wrapper):
        os.unlink(wrapper)
        removed.append(wrapper)
    if os.path.isdir(PI_ROOT):
        shutil.rmtree(PI_ROOT)
        removed.append(PI_ROOT)
    return removed


# --------------------------------------------------------------------------
# CLI (also the headless path for ssh work)
# --------------------------------------------------------------------------
def main(argv):
    import argparse

    parser = argparse.ArgumentParser(
        prog="pi-agent-setup-engine",
        description=_("Pi Agent 安装引擎（GUI 与命令行共用）"))
    sub = parser.add_subparsers(dest="command", required=True)

    net = sub.add_parser("check-network", help=_("检查网络连通性"))
    net.add_argument("--json", action="store_true")

    inst = sub.add_parser("install", help=_("下载 node 运行时并安装 pi"))
    inst.add_argument("--mirror", action="store_true",
                      help=_("下载失败时改用国内镜像"))

    conf = sub.add_parser("configure", help=_("写入 API URL / API Key / 模型"))
    conf.add_argument("--url", required=True)
    conf.add_argument("--key", required=True)
    conf.add_argument("--model", required=True)
    conf.add_argument("--api", default="openai-completions", choices=[x[0] for x in API_TYPES])
    # Resolved in the body, not here: the default must follow the session
    # language, and `_()` is only meaningful once that is known.
    conf.add_argument("--name", default=None)
    conf.add_argument("--test", action="store_true", help=_("写入前先探测端点"))

    lst = sub.add_parser("list-models", help=_("探测端点提供的模型列表"))
    lst.add_argument("--url", required=True)
    lst.add_argument("--key", required=True)
    lst.add_argument("--api", default="openai-completions", choices=[x[0] for x in API_TYPES])
    lst.add_argument("--json", action="store_true")

    sub.add_parser("status", help=_("打印当前安装/配置状态"))

    sk = sub.add_parser("skills-status", help=_("技能包状态（JSON）"))
    sk.add_argument("--json", action="store_true")
    ski = sub.add_parser("skills-install", help=_("拉取并安装技能包（需要网络）"))
    ski.add_argument("--force", action="store_true",
                     help=_("即使版本相同也重新拉取"))
    sub.add_parser("skills-sync", help=_("重新建立技能软链（不需要网络）"))
    sub.add_parser("skills-decline", help=_("记住“不安装技能包”，以后不再询问"))
    sub.add_parser("skills-should-ask", help=_("是否该问用户要不要装技能包（0=该问）"))
    sck = sub.add_parser("skills-check", help=_("是否有更新（0=有，1=无，2=无法判断）"))
    sck.add_argument("--json", action="store_true")
    # The login hook. engine.py has no GUI, so it only reports what needs asking
    # and lets the caller (pi-agent) put the question; this keeps GTK out of the
    # module the headless CLI paths import.
    sas = sub.add_parser("skills-autostart",
                         help=_("登录时调用：静默重建链接，报告是否需要询问"))
    sas.add_argument("--json", action="store_true")
    sub.add_parser("should-prompt", help=_("登录时是否该弹窗（0=该弹，1=不该）"))
    sn = sub.add_parser("snooze", help=_("下次再询问"))
    sn.add_argument("--days", type=int, default=DEFAULT_SNOOZE_DAYS)
    sub.add_parser("decline", help=_("不再询问"))
    sub.add_parser("uninstall", help=_("移除 pi 运行时（保留 ~/.pi 会话与配置）"))

    args = parser.parse_args(argv)

    if args.command == "check-network":
        ok, detail = check_network()
        if args.json:
            print(json.dumps({"ok": ok, "detail": detail}, ensure_ascii=False))
        else:
            print(detail)
        return 0 if ok else 1
    if args.command == "install":
        version = install_pi(use_mirror=args.mirror)
        print(_("已安装 pi %s") % version)
        return 0
    if args.command == "list-models":
        models, detail = list_models(args.url, args.key, args.api)
        if args.json:
            print(json.dumps({"models": [m for m, _ in models], "detail": detail},
                             ensure_ascii=False))
        else:
            print(detail)
            for ident, label in models:
                print("  %s%s" % (ident, "  [%s]" % label if label else ""))
        return 0 if models else 1
    if args.command == "configure":
        if args.test:
            ok, detail = test_endpoint(args.url, args.key, args.model, args.api)
            print(detail)
            if not ok:
                return 1
        paths = configure(args.url, args.key, args.model, args.api,
                          args.name or _("自定义端点"))
        for key, path in paths.items():
            print("%-9s %s" % (key, path))
        return 0
    if args.command == "status":
        print(json.dumps(status(), indent=2, ensure_ascii=False))
        return 0
    if args.command == "skills-status":
        info = skills_status()
        if args.json:
            print(json.dumps(info, indent=2, ensure_ascii=False))
        else:
            print(_("已安装：%s") % (info["version"] or _("否")))
            print(_("位置：%s") % info["path"])
            print(_("状态：%s") % (info["state"] or _("未询问")))
        return 0
    if args.command == "skills-install":
        print(skills_install(force=args.force))
        return 0
    if args.command == "skills-sync":
        print(_("已建立 %d 个技能链接") % skills_sync())
        return 0
    if args.command == "skills-decline":
        skills_decline()
        print(_("已记住：不安装技能包"))
        return 0
    if args.command == "skills-should-ask":
        return 0 if skills_should_ask() else 1
    if args.command == "skills-autostart":
        # Rebuild the links (local, cheap, always safe) and report whether the
        # caller should ask. Pulling is *not* done here: the caller owns the
        # dialog and only calls skills-install after an explicit yes.
        skills_sync()
        state = read_state()
        declined = state.get("skills_status") == "declined"
        available = None if declined else skills_update_available()
        if not declined:
            write_state(skills_checked_at=int(time.time()))
        info = {
            "ask": bool(available),
            "first_install": not skills_local_version(),
            "installed": skills_local_version(),
            "remote": skills_remote_version() if available else "",
            "declined": declined,
        }
        if args.json:
            print(json.dumps(info, ensure_ascii=False))
        elif info["ask"]:
            print("%s %s" % (info["installed"] or "-", info["remote"]))
        return 0 if info["ask"] else 1
    if args.command == "skills-check":
        available = skills_update_available()
        if args.json:
            print(json.dumps({"update": available,
                              "installed": skills_local_version(),
                              "remote": skills_remote_version()},
                             ensure_ascii=False))
        elif available is None:
            print(_("无法判断（离线）"))
        elif available:
            print(_("有更新：%s -> %s") % (skills_local_version() or _("未安装"),
                                          skills_remote_version()))
        else:
            print(_("已是最新：%s") % skills_local_version())
        return 0 if available else (2 if available is None else 1)
    if args.command == "should-prompt":
        return 0 if should_prompt() else 1
    if args.command == "snooze":
        print(json.dumps(snooze(args.days), ensure_ascii=False))
        return 0
    if args.command == "decline":
        print(json.dumps(decline(), ensure_ascii=False))
        return 0
    if args.command == "uninstall":
        for path in uninstall():
            print(_("已删除 %s") % path)
        return 0
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except SetupError as exc:
        print(_("错误：%s") % exc, file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
