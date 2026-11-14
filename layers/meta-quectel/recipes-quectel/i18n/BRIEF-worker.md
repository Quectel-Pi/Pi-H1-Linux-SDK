# 任务简报：给板载 GUI 加中英文切换（worker 必读）

仓库根：`/home/igni/Downloads/debian/1.7-QuecPi-QCLinux-BL01`
共享基础设施（**已由 leader 写好，不要改，也不要另写一套**）：

- `layers/meta-quectel/recipes-quectel/i18n/files/ln_i18n.py`
- `layers/meta-quectel/recipes-quectel/i18n/ln-i18n_1.0.bb`
- `layers/meta-quectel/recipes-quectel/i18n/tools/i18n-check.py`
- `layers/meta-quectel/recipes-quectel/i18n/README.md` ← **先读这份**

## 上下文（为什么这么定）

- 板子当前 `LANG=C.UTF-8`，`locale -a` 里 `zh_CN.utf8` / `en_US.utf8` 都在，
  GNOME「区域与语言」改完下次登录即生效。所以**不需要**加 locale 包，只缺翻译文件。
- 这四个应用（时间胶囊 / NPU Chat / NPU 测试 / Pi Agent）界面全是硬编码中文，
  且没有任何 `.mo`。本任务就是把这四块补齐。
- **msgid = 中文原文**（leader 已定，不要改成英文 msgid）。
  中文会话零成本显示原文；非中文会话读 `en.mo`。

## 硬规则（违反就算没做完）

1. **只用 `edit` 工具做精确替换**，禁止任何 python/sed 批量替换源码。
   一次只改一处，`oldText` 要能唯一定位。
2. **禁止 `_(f"...")`**。f-string 里带中文的一律改写成 `_("… {x} …").format(x=x)`。
3. 每个包完必须让门禁全绿：
   ```bash
   python3 layers/meta-quectel/recipes-quectel/i18n/tools/i18n-check.py --app <你的 app>
   ```
4. **不许删任何调试打印 / 日志行**（项目规矩）。日志文案要包 `_()`，但不要改它的内容。
5. **不要跑 `bitbake` / `buildall` / `buildconfig`**（项目规矩：编译由用户手动执行）。
   你可以用 `python3 -m py_compile` 做语法检查。
6. 改完 `git diff -- <你负责的文件>` 逐行读一遍，确认没有误删相邻语句。
7. 不要提交 git commit，leader 统一收。

## 每个应用的标准动作

### a) 引入 ln_i18n（放在 main() 最前面，或模块顶部）

```python
import sys
sys.path.insert(0, "/usr/share/ln-i18n")
try:
    import ln_i18n
    _ = ln_i18n.setup("<domain>")
except ImportError:                       # ln-i18n 没装：整机退回中文原文
    def _(message):
        return message
```

`sys.path.insert` 必须在 `import ln_i18n` **之前**。

### b) 包字符串

| 要包 | 不要包 |
|---|---|
| 按钮/标签/标题/表头/对话框文案 | 变量名、路径、命令名、模型 ID |
| 错误提示、给用户看的日志文案 | `CMD: ...`、寄存器 dump、协议字符串 |
| Pango markup 整串（`"<b>首次初始化</b>"`） | 纯符号/emoji（`"⏳"`、`"🔄"`）单独时 |
| `%` 格式化模板（`"失败（%d）：%s"`） | 已经 `.format()` 完的成品串 |

- 相邻字面量拼接（`"第一句"` `"第二句"`）整体包一次 `_()`，xgettext 会合成一个 msgid，
  翻译时看到的是完整句子。
- 混合拼接如 `"共 " + str(n) + " 个"` 改成 `_("共 {n} 个").format(n=n)`。
- **不要包** `.desktop` 文件里的东西（那边用 `Name[zh_CN]=` 那套，见 d）。

### c) .desktop 文件（四个应用里只有部分有）

统一成「英文是默认值，中文是 `[zh_CN]`」，不要反过来：

```ini
Name=TimeCapsule
Name[zh_CN]=时间胶囊
GenericName=System snapshot manager
GenericName[zh_CN]=系统快照管理
Comment=...
Comment[zh_CN]=...
Keywords=ota;backup;rollback;snapshot;
Keywords[zh_CN]=升级;备份;回退;快照;
```

`Exec=` / `Icon=` / `Categories=` 不翻译。

### d) 翻译文件

```bash
# 1. 抽 pot + 生成 en.po（msgstr 全空）
python3 layers/meta-quectel/recipes-quectel/i18n/tools/i18n-check.py --app <app> --refresh

# 2. 人工填 msgstr（英文），保留 {n} %d \n 和 markup 原样
# 3. 编译并放进 recipe
msgfmt -o layers/meta-quectel/recipes-quectel/<app>/files/locale/en/LC_MESSAGES/<domain>.mo \
          layers/meta-quectel/recipes-quectel/i18n/po/<app>/<domain>.po
```

`.po` / `.pot` / `.mo` **都要入库**（.mo 是交付物，板上不编译）。
`ln_i18n.py` 里 `.mo` 的查找路径是 `/usr/share/locale/<lang>/LC_MESSAGES/<domain>.mo`。

### e) recipe（`<app>_1.0.bb`）

`do_install` 里加：

```bash
install -d ${D}${datadir}/locale/en/LC_MESSAGES
install -m 0644 ${S}/locale/en/LC_MESSAGES/<domain>.mo \
    ${D}${datadir}/locale/en/LC_MESSAGES/<domain>.mo
```

`SRC_URI` 里加 `file://locale/en/LC_MESSAGES/<domain>.mo`，
`RDEPENDS:${PN}` 里加 `ln-i18n`。

> `ln-i18n` 这个新包是否需要登记到 `conf/deb-track/bsp-userspace.txt`
> **由用户决定**，你不要自己加，也不要自己去动 image 的 `IMAGE_INSTALL`。

## 验收清单（回复 leader 时按这个格式）

```
app: <名字>   domain: <domain>
- 源码：<文件> 改了 <N> 处
- 门禁：i18n-check --app <app>  → 全部通过 / 失败项
- 语法：python3 -m py_compile <文件> → OK
- .desktop：<改了/没改，改了哪些键>
- .mo：<路径>（msgfmt 编好）
- .bb：<加了哪些行>
- 没解决/需要决策的事项：<...>
```
