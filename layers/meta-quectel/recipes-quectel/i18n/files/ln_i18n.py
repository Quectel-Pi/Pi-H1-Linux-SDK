"""ln_i18n -- Quectel 板载 Python GUI 共用的极简 gettext 包装。

设计前提（改动前先读，否则容易把中文弄丢）：

  * **msgid 就是源码里的中文原文**。中文用户不加载任何 .mo，直接看到原文，
    所以中文界面永远不会因为缺翻译文件而变英文。
  * 系统语言不是中文时，加载 /usr/share/locale/<lang>/LC_MESSAGES/<domain>.mo
    拿英文。**en.mo 是打包必需品**：缺失时 gettext 会退回 msgid，也就是中文，
    非中文用户就会看到中文。仓库里 tools/i18n-check.sh 负责在提交前把这条兜住。
  * C / POSIX / 未设置语言变量 → 当作英文（和 .desktop 里 `Name=` 的默认值一致）。
  * 不做复数：中文没有复数形态，需要数量时把数字写进占位符
    （"共 {n} 个文件"），英文 .po 里给一句就好，不要用 ngettext。

用法：

    import ln_i18n
    _ = ln_i18n.setup("quectel-ota-gui")     # 一次，在 main() 最前面

    label.set_text(_("刷新"))
    label.set_text(_("共 {n} 个文件").format(n=n))   # 占位符走 str.format
    label.set_markup(_("<b>首次初始化</b>"))         # Pango markup 一起进 msgid

禁止：`_(f"共 {n} 个")` —— msgid 会变成格式化后的成品串，英文永远命不中。
f-string 一律改写成 `_("共 {n} 个").format(n=n)`。

本文件不 import gi / GTK：无 GUI 的 CLI 路径（engine.py 一类）也要能 import 它。
"""

import gettext as _gettext
import os

LOCALE_DIR = "/usr/share/locale"

# msgid 的源语言。setup() 用它判断"用户是不是中文，无需翻译"。
SOURCE_LANG = "zh"

_state = {
    "domain": None,
    "languages": [],
    "chinese": True,
    "translator": None,
}


def _normalize(value):
    """'zh_CN.UTF-8@euro' -> 'zh_CN'；'C'/'POSIX'/空 -> None。"""
    value = (value or "").strip()
    if not value:
        return None
    value = value.split(".", 1)[0]
    value = value.split("@", 1)[0]
    if value.upper() in ("C", "POSIX"):
        return None
    return value


def effective_languages():
    """按 glibc 的优先级还原语言偏好，返回如 ['zh_CN', 'zh']。

    LANGUAGE 排在前面是 glibc 的规矩（它是冒号分隔的列表）；
    LC_ALL 优先级高于 LC_MESSAGES 高于 LANG。
    """
    for var in ("LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"):
        raw = os.environ.get(var)
        if not raw:
            continue
        if var == "LANGUAGE":
            candidates = [_normalize(part) for part in raw.split(":")]
        else:
            candidates = [_normalize(raw)]
        candidates = [c for c in candidates if c]
        if candidates:
            # 补短码，便于 gettext 找到 zh/en 这种不带地区的 .mo
            short = candidates[0].split("_", 1)[0]
            if short and short not in candidates:
                candidates.append(short)
            return candidates
    return []


def setup(domain, localedir=LOCALE_DIR):
    """加载翻译并返回 `_()`。找不到 .mo 时返回英文兜底函数，绝不抛异常。"""
    languages = effective_languages()
    chinese = bool(languages) and languages[0].lower().startswith(SOURCE_LANG)
    _state.update(domain=domain, languages=languages, chinese=chinese,
                  translator=None)

    if chinese:
        def identity(message):
            return message

        return identity

    translator = _gettext.translation(
        domain,
        localedir=localedir,
        languages=languages + ["en"],
        fallback=True,
    )
    _state["translator"] = translator

    def translate(message):
        return translator.gettext(message)

    return translate


def is_chinese():
    """界面是否需要中文习惯（日期格式、默认输入提示、语音语言等）。"""
    return _state["chinese"]


def languages():
    return list(_state["languages"])


def domain():
    return _state["domain"]
