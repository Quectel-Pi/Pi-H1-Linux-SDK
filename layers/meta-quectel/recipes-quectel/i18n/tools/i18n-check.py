#!/usr/bin/env python3
"""i18n-check -- 提交前的翻译完整性闸门。

跑法（仓库根目录）：

    python3 layers/meta-quectel/recipes-quectel/i18n/tools/i18n-check.py
    python3 .../i18n-check.py --app npu-chat        # 只查一个
    python3 .../i18n-check.py --refresh             # 重抽 pot（改了源码之后）

它检查四件事，任何一条失败都返回非 0：

1. **漏包的串**：源码里的中文字面量有没有没套 `_()` 的。
   做法是用 xgettext 抽出所有 `_()` 里的串，再和"正则扫出来的中文字面量"求差集。
   差集非空 = 有串永远翻不到。
2. **过期/缺失的条目**：en.po 里有没有 pot 已经不存在的 msgid（msgmerge 会标 #~）。
3. **没翻的条目**：en.po 里有 msgstr 为空的条目。
4. **en.mo 是否与 en.po 同步、是否会被 gettext 真的读到**。
   en.mo 缺失时非中文用户会看到中文，这是本机制最致命的坑。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
I18N_DIR = os.path.dirname(HERE)
RECIPES = os.path.dirname(I18N_DIR)
REPO = os.path.abspath(os.path.join(RECIPES, "..", "..", ".."))

CJK = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]")

# 豁免标记，见 unwrapped_strings() 里的说明。写在注释里，xgettext 也看不到它们。
IGNORE_NEXT = "# i18n-ignore:"
IGNORE_START = "# i18n-ignore-start"
IGNORE_END = "# i18n-ignore-end"


def git_tracked(paths):
    out = subprocess.run(["git", "ls-files", "--"] + list(paths),
                         cwd=REPO, capture_output=True, text=True)
    return [p for p in out.stdout.splitlines() if p.strip()]


class App:
    def __init__(self, name, domain, sources, podir=None):
        self.name = name
        self.domain = domain
        self.sources = sources          # 相对仓库根
        self.podir = podir or os.path.join(
            os.path.dirname(HERE), "po", name)


APPS = [
    App("timecapsule", "quectel-ota-gui",
        ["layers/meta-quectel/recipes-quectel/quectel-ota/files/ota_gui.py"]),
    App("npu-chat", "npu-chat",
        ["layers/meta-quectel/recipes-quectel/npu-llm/files/npu-chat"]),
    App("npu-test", "qcom-npu-test",
        ["layers/meta-quectel/recipes-quectel/qcom-npu-test/files/npu_test_gui.py"]),
    App("pi-agent", "pi-agent-setup",
        ["layers/meta-quectel/recipes-quectel/pi-agent-setup/files/pi-agent-setup.py",
         "layers/meta-quectel/recipes-quectel/pi-agent-setup/files/engine.py"]),
]


def extract_pot(app, out_pot):
    """用 xgettext 按源码语言抽 pot。返回 (ok, message)。"""
    cmd = ["xgettext", "--from-code=UTF-8", "-o", out_pot,
           "--package-name", app.domain,
           "--no-location" if False else "--add-location=file"]
    for src in app.sources:
        path = os.path.join(REPO, src)
        if not os.path.exists(path):
            return False, "源码不存在: %s" % src
        if src.endswith(".py"):
            cmd += ["-L", "Python", path]
        else:
            # 无扩展名的 python 脚本：npu-chat 就是这种，必须显式指定语言
            cmd += ["-L", "Python", path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0, (r.stderr or "").strip()


def pot_msgids(pot):
    """pot 里的全部 msgid（多行条目拼成一条）。

    xgettext 会把长串和带 \\n 的串写成多条 `msgid ""` + 续行，只匹配单行
    `msgid "..."` 会把这类条目整条漏掉：源码里明明包了 _()，比对时却因为
    msgid 集合里没有它而报"没套 _()"。所以续行必须拼回去。
    """
    ids, cur, collecting = set(), None, False
    with open(pot, encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            m = re.match(r'^msgid\s+"(.*)"$', line)
            if m:
                if cur:
                    ids.add(cur)
                cur, collecting = m.group(1), True
                continue
            m = re.match(r'^(msgid_plural|msgstr\w*)\s+"(.*)"$', line)
            if m:
                if cur:
                    ids.add(cur)
                cur, collecting = None, False
                continue
            m = re.match(r'^"(.*)"$', line)
            if m and collecting:
                cur += m.group(1)
                continue
            if not line.strip() and cur:
                ids.add(cur)
                cur, collecting = None, False
    if cur:
        ids.add(cur)
    return {i for i in ids if i}


def unwrapped_strings(app):
    """源码里出现但不是 msgid 的中文字面量 —— 即永远翻不到的串。"""
    pot_msgids_set = set()
    with tempfile.TemporaryDirectory() as tmp:
        pot = os.path.join(tmp, "x.pot")
        ok, err = extract_pot(app, pot)
        if not ok:
            return None, err
        if os.path.exists(pot):
            pot_msgids_set = pot_msgids(pot)

    missing = []
    str_re = re.compile(r'''f?("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')''')
    for src in app.sources:
        path = os.path.join(REPO, src)
        ignore_next = False
        in_ignore = False
        with open(path, encoding="utf-8") as f:
            for lineno, line in enumerate(f, 1):
                stripped = line.lstrip()
                # 两种豁免标记，都要求冒号后写理由：
                #
                #   # i18n-ignore: 理由           → 跳过下一条语句（含其续行）
                #   # i18n-ignore-start: 理由     → 跳过这一段
                #   # i18n-ignore-end
                #
                # 用在"不是给人看的字面量"上：数据表（数字读法 DIGITS/UNITS/
                # SAY_MAP）、自检断言的期望值、语音自检念的样例句、带中文举例的
                # docstring。它们翻成英文会直接改变程序行为（把"十二"读成
                # "twelve"，或让 assert 在英文会话下失败），包 _() 反而是 bug。
                if IGNORE_END in line:
                    in_ignore = False
                    continue
                if IGNORE_START in line:
                    in_ignore = True
                    continue
                if in_ignore:
                    continue
                if IGNORE_NEXT in line:
                    ignore_next = True
                    continue
                if not stripped.strip() or stripped.startswith("#"):
                    continue        # 空行 / 注释里的中文不用翻
                if ignore_next:
                    ignore_next = False
                    continue
                for m in str_re.finditer(line):
                    lit = m.group(1)
                    if not CJK.search(lit):
                        continue
                    body = lit[1:-1]
                    if m.group(0).startswith("f"):
                        missing.append((src, lineno, lit,
                                        "f-string 里的中文：msgid 会变成成品串，"
                                        "改成 _(...) 加 .format()"))
                        continue
                    # 相邻字面量的隐式拼接会被 xgettext 合成一个 msgid，
                    # 所以"是某个 msgid 的一部分"也算已包住。
                    if body in pot_msgids_set:
                        continue
                    if any(body in mid for mid in pot_msgids_set):
                        continue
                    missing.append((src, lineno, lit,
                                    "没套 _()，xgettext 抽不到"))
    return missing, ""


def stale_msgsids(app):
    """pot 里已不存在的中文 msgid，但源码里还有 —— 说明串改了但没重抽 pot。

    这项能抓到一类隐形错误：改完文案忘了跑 --refresh，于是 en.po 里
    还是旧 msgid，界面上那句就永远显示中文。
    """
    pot_msgids_set = set()
    with tempfile.TemporaryDirectory() as tmp:
        pot = os.path.join(tmp, "x.pot")
        ok, err = extract_pot(app, pot)
        if not ok or not os.path.exists(pot):
            return [], err
        pot_msgids_set = pot_msgids(pot)

    po = os.path.join(app.podir, "%s.po" % app.domain)
    if not os.path.exists(po):
        return [], ""
    po_ids = set()
    with open(po, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#~"):
                continue
            m = re.match(r'^msgid\s+"(.*)"$', line)
            if m and m.group(1):
                po_ids.add(m.group(1))
    return sorted(po_ids - pot_msgids_set), ""


def refresh(app):
    """重抽 pot 并把它和已有 en.po 合并（保留已翻的条目）。"""
    os.makedirs(app.podir, exist_ok=True)
    pot = os.path.join(app.podir, "%s.pot" % app.domain)
    ok, err = extract_pot(app, pot)
    if not ok:
        print("抽 pot 失败：%s" % err)
        return 1
    if not os.path.exists(pot):
        # xgettext 在"没有可翻译串"时不写文件。改造刚开始就是这个状态。
        print("%s 里还没有任何 _() 包裹的串，跳过" % app.name)
        return 0
    po = os.path.join(app.podir, "%s.po" % app.domain)
    if os.path.exists(po):
        r = subprocess.run(["msgmerge", "--update", "--backup=none", po, pot],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("msgmerge 失败：%s" % r.stderr.strip())
            return 1
        print("已更新 %s" % os.path.relpath(po, REPO))
    else:
        r = subprocess.run(["msginit", "--no-translator", "-l", "en",
                            "-i", pot, "-o", po], capture_output=True, text=True)
        if r.returncode != 0:
            print("msginit 失败：%s" % r.stderr.strip())
            return 1
        print("已创建 %s（msgstr 全空，要人工填）" % os.path.relpath(po, REPO))
    print("pot: %s" % os.path.relpath(pot, REPO))
    return 0


def check_po(app):
    """检查 en.po 是否齐全，并确认 en.mo 与 po 同步。"""
    problems = []
    po = os.path.join(app.podir, "%s.po" % app.domain)
    mo = os.path.join(app.podir, "%s.mo" % app.domain)
    if not os.path.exists(po):
        return ["缺少 %s" % os.path.relpath(po, REPO)]

    r = subprocess.run(["msgattrib", "--untranslated", po],
                       capture_output=True, text=True)
    untranslated = [l for l in r.stdout.splitlines() if l.startswith("msgid ")
                    and l != 'msgid ""']
    if untranslated:
        problems.append("en.po 有 %d 条没翻：%s" % (
            len(untranslated), ", ".join(untranslated[:5])))

    with tempfile.TemporaryDirectory() as tmp:
        fresh = os.path.join(tmp, "fresh.mo")
        rc = subprocess.run(["msgfmt", "-o", fresh, po],
                            capture_output=True, text=True)
        if rc.returncode != 0:
            problems.append("en.po 编译失败：%s" % rc.stderr.strip())
        elif not os.path.exists(mo):
            problems.append("缺少已编译的 %s（非中文用户会看到中文）"
                            % os.path.relpath(mo, REPO))
        else:
            with open(fresh, "rb") as a, open(mo, "rb") as b:
                if a.read() != b.read():
                    problems.append("%s 与 %s 不同步，重新 msgfmt"
                                    % (os.path.relpath(mo, REPO),
                                       os.path.relpath(po, REPO)))
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", help="只检查/只处理指定应用")
    ap.add_argument("--refresh", action="store_true",
                    help="重抽 pot 并合并进 en.po（改了源码之后跑）")
    args = ap.parse_args()

    apps = APPS
    if args.app:
        apps = [a for a in APPS if a.name == args.app]
        if not apps:
            print("未知应用 %s，可选：%s" % (
                args.app, ", ".join(a.name for a in APPS)))
            return 2

    failed = 0
    for app in apps:
        print("== %s (domain %s)" % (app.name, app.domain))
        if args.refresh:
            failed += refresh(app)
        missing, err = unwrapped_strings(app)
        if missing is None:
            print("   FAIL 抽 pot 失败：%s" % err)
            failed += 1
            continue
        if missing:
            failed += 1
            print("   FAIL %d 处中文翻不到：" % len(missing))
            for src, lineno, lit, why in missing[:20]:
                print("        %s:%d  %s" % (src, lineno, lit))
                print("            -> %s" % why)
            if len(missing) > 20:
                print("        ... 还有 %d 处" % (len(missing) - 20))
        else:
            print("   OK   所有中文字面量都在 _() 里")

        problems = check_po(app)
        for p in problems:
            failed += 1
            print("   FAIL %s" % p)
        if not problems:
            print("   OK   en.po / en.mo 齐全且同步")

        stale, err = stale_msgsids(app)
        if stale:
            failed += 1
            print("   FAIL en.po 有 %d 条 msgid 已不在源码里（改了文案没重抽 pot）："
                  % len(stale))
            for s in stale[:10]:
                print("        %s" % s)
        elif not err:
            print("   OK   en.po 与源码 msgid 对齐")

    print()
    if failed:
        print("i18n-check: %d 项未通过" % failed)
        return 1
    print("i18n-check: 全部通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
