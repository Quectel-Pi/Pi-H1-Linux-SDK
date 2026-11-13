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
                "下载校验失败（sha256 不匹配）\n  期望 %s\n  实际 %s" % (expect_sha256, actual))
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
        steps("node 已就绪：%s" % existing)
        return os.path.join(NODE_DIR, "current/bin/node")

    bases = [NODE_DIST_BASE]
    if use_mirror and MIRROR_NODE not in bases:
        bases.append(MIRROR_NODE)
    last_error = None
    for base in bases:
        try:
            steps("获取 node 版本信息：%s" % base)
            sums = read_url(base.rstrip("/") + "/" + NODE_LINE + "/SHASUMS256.txt",
                            timeout=25).decode("utf-8", "replace")
        except Exception as exc:              # noqa: BLE001
            last_error = "%s: %s" % (base, exc)
            steps("  失败：%s" % last_error)
            continue
        pattern = re.compile(
            r"^([0-9a-f]{64})\s+(node-v([0-9.]+)-linux-%s\.tar\.xz)$" % re.escape(arch_name()),
            re.MULTILINE)
        match = pattern.search(sums)
        if not match:
            last_error = "%s 里没有 linux-%s 的 node 包" % (base, arch_name())
            steps("  失败：%s" % last_error)
            continue
        digest, filename, version = match.group(1), match.group(2), match.group(3)
        url = "%s/%s/%s" % (base.rstrip("/"), "v" + version, filename)
        target_dir = os.path.join(NODE_DIR, "v" + version)
        tarball = os.path.join(CACHE_DIR, filename)
        steps("下载 node v%s（%.1f MB 级）" % (version, 0))
        try:
            download(url, tarball, expect_sha256=digest,
                     progress=lambda done, total: _progress("node", done, total))
        except Exception as exc:              # noqa: BLE001
            last_error = "%s: %s" % (url, exc)
            steps("  下载失败：%s" % last_error)
            continue
        steps("解包 node v%s" % version)
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
            last_error = "解包后找不到 bin/node"
            steps("  失败：%s" % last_error)
            continue
        os.chmod(node_bin, 0o755)
        steps("node 安装完成：%s" % node_version_for_pi())
        return node_bin
    raise SetupError("node 运行时安装失败：%s" % (last_error or "所有来源都不可用"))


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
            raise SetupError("压缩包包含越界路径：%s" % member.name)
        if member.issym() or member.islnk():
            link = os.path.abspath(os.path.join(os.path.dirname(target), member.linkname))
            if not link.startswith(dest_abs):
                raise SetupError("压缩包包含越界链接：%s" % member.name)
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
        raise SetupError("node 包里没有 npm：%s" % npm)

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

    steps("安装 %s（npm，安装到 %s）" % (PI_PACKAGE, NPM_PREFIX))
    rc, tail = run(cmd, env=env, log_path=logdir)
    if rc != 0 and registry:
        steps("镜像安装失败（rc=%d），改用官方源重试" % rc)
        cmd = [part for part in cmd if part not in ("--registry", registry)]
        rc, tail = run(cmd, env=env, log_path=logdir)
    if rc != 0:
        raise SetupError("npm 安装失败（退出码 %d）：\n%s" % (rc, tail[-1200:]))

    launcher = os.path.join(NPM_PREFIX, "bin/pi")
    if not os.path.exists(launcher):
        raise SetupError("安装结束但找不到启动器：%s" % launcher)
    os.chmod(launcher, 0o755)
    os.makedirs(BIN_DIR, exist_ok=True)
    write_launcher(node_bin, launcher)

    version = pi_version(launcher)
    if not version:
        raise SetupError("pi 已安装但无法运行：%s --version 没有输出" % launcher)
    steps("pi 安装成功：%s" % version)

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
    ("openai-completions", "OpenAI 兼容（大部分中转/自建服务）"),
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
        raise SetupError("填写 API URL 和 API Key 才能探测模型列表")
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
            return models, "✓ %s（%d 个模型）" % (url, len(models))
        detail.append("%s 返回的内容里没有模型列表" % url)
    return [], "\n".join(detail) or "无法连接"


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
            detail.append("✓ %s（返回的模型列表里含 %s）" % (url, model))
        else:
            detail.append("✓ %s（可访问，未在模型列表里匹配到 %s，端点可能只列出部分模型）"
                          % (url, model))
        return True, "\n".join(detail)
    return False, "\n".join(detail) or "无法连接"


def configure(base_url, api_key, model, api="openai-completions",
              provider_name="自定义端点", context_window=128000, max_tokens=8192):
    if not base_url or not api_key or not model:
        raise SetupError("API URL、API Key、模型 ID 都不能为空")
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
        description="pi agent 安装引擎（GUI 与命令行共用）")
    sub = parser.add_subparsers(dest="command", required=True)

    net = sub.add_parser("check-network", help="检查网络连通性")
    net.add_argument("--json", action="store_true")

    inst = sub.add_parser("install", help="下载 node 运行时并安装 pi")
    inst.add_argument("--mirror", action="store_true", help="下载失败时改用国内镜像")

    conf = sub.add_parser("configure", help="写入 API URL / API Key / 模型")
    conf.add_argument("--url", required=True)
    conf.add_argument("--key", required=True)
    conf.add_argument("--model", required=True)
    conf.add_argument("--api", default="openai-completions", choices=[x[0] for x in API_TYPES])
    conf.add_argument("--name", default="自定义端点")
    conf.add_argument("--test", action="store_true", help="写入前先探测端点")

    lst = sub.add_parser("list-models", help="探测端点提供的模型列表")
    lst.add_argument("--url", required=True)
    lst.add_argument("--key", required=True)
    lst.add_argument("--api", default="openai-completions", choices=[x[0] for x in API_TYPES])
    lst.add_argument("--json", action="store_true")

    sub.add_parser("status", help="打印当前安装/配置状态")
    sub.add_parser("should-prompt", help="登录时是否该弹窗（0=该弹，1=不该）")
    sn = sub.add_parser("snooze", help="下次再询问")
    sn.add_argument("--days", type=int, default=DEFAULT_SNOOZE_DAYS)
    sub.add_parser("decline", help="不再询问")
    sub.add_parser("uninstall", help="移除 pi 运行时（保留 ~/.pi 会话与配置）")

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
        print("已安装 pi %s" % version)
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
        paths = configure(args.url, args.key, args.model, args.api, args.name)
        for key, path in paths.items():
            print("%-9s %s" % (key, path))
        return 0
    if args.command == "status":
        print(json.dumps(status(), indent=2, ensure_ascii=False))
        return 0
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
            print("已删除 %s" % path)
        return 0
    return 2


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except SetupError as exc:
        print("错误：%s" % exc, file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        sys.exit(130)
