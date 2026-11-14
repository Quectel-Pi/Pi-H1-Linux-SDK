# 板载 Python GUI 的中英文切换（i18n）

本目录是 **时间胶囊 / NPU Chat / NPU 测试 / Pi Agent** 四个应用共用的翻译基础设施。
四个应用各自有 worker 改造，这里只放大家都要用的东西，**不要**在每个应用里再写一套。

## 目录

| 路径 | 作用 |
|---|---|
| `files/ln_i18n.py` | 唯一的运行期模块。`setup(domain)` → `_()`，读取系统语言 |
| `ln-i18n_1.0.bb` | 把上面那个文件装到 `/usr/share/ln-i18n/ln_i18n.py` |
| `tools/i18n-check.py` | 提交门禁：漏包串 / 未翻条目 / en.mo 不同步，一次报出来 |
| `po/<app>/<domain>.po` | 英文翻译（msgstr = 英文） |
| `po/<app>/<domain>.mo` | 由 .po 编译，**入库**，装到 `/usr/share/locale/en/LC_MESSAGES/` |
| `po/<app>/<domain>.pot` | 抽取模板，仅作比对用 |

## 机制（一句话）

**msgid = 源码里的中文原文。**
中文会话不需要任何 `.mo`，直接显示原文；非中文会话读 `en.mo`。
好处：改造是纯机械的（把中文字面量套进 `_()`），漏翻时兜底是"显示中文"而不是空白。
代价：`en.mo` 一旦缺失或没同步，非中文用户就会看到中文 —— 这条由 `i18n-check.py` 兜住，
不允许绕过。

## 每个应用要做的固定动作

### 1. 启动时初始化（每个应用的 main() 最前面）

```python
sys.path.insert(0, "/usr/share/ln-i18n")
try:
    import ln_i18n
    _ = ln_i18n.setup("<domain>")
except ImportError:                      # ln-i18n 包没装：整机退回中文
    def _(message):
        return message
```

`sys.path.insert` 必须放在 `import` 之前，因为这是普通文件而不是 site-packages 安装。
必须带 `try/except ImportError`：这个包丢了也不该让应用起不来。

各应用的 domain：

| 应用 | recipe | domain（= .mo 文件名） |
|---|---|---|
| 时间胶囊 | `quectel-ota` | `quectel-ota-gui` |
| NPU Chat | `npu-llm` | `npu-chat` |
| NPU 测试 | `qcom-npu-test` | `qcom-npu-test` |
| Pi Agent | `pi-agent-setup` | `pi-agent-setup` |

### 2. 包字符串的规矩

```python
label.set_text(_("刷新"))                                # 直接字面量
label.set_text(_("共 {n} 个文件").format(n=n))            # 占位符用 .format
label.set_markup(_("<b>首次初始化</b>"))                  # Pango markup 整串进 msgid
raise SetupError(_("npm 安装失败（退出码 %d）：\n%s") % (rc, tail))
```

- **禁止 `_(f"...")`**：msgid 会变成格式化后的成品串，英文永远命不中。
  f-string 一律改写成 `_("… {x} …").format(x=x)`。
- 拼接好的多行文案（`"第一句"` `"第二句"` 相邻）可以整体包进 `_()`，xgettext 会合成一个 msgid。
- 纯变量名、路径、协议名、命令名、模型名不要包（`_("pkexec")` 只会增加翻译负担）。
- `.desktop` 的 `Name` / `GenericName` / `Comment` / `Keywords` 不归这里管，
  直接写 `Name=`（英文默认）+ `Name[zh_CN]=`。
- 日志里给人看的文案要包；纯技术日志（`CMD: ...`、寄存器 dump）不包。

### 3. 每个应用交付这几样

```
recipes-quectel/<app>/files/<app>.py            # 改好的源码
recipes-quectel/<app>/files/locale/en/LC_MESSAGES/<domain>.mo
recipes-quectel/i18n/po/<app>/<domain>.po       # 英文译文（人工翻）
recipes-quectel/i18n/po/<app>/<domain>.pot      # 抽取模板
recipes-quectel/<app>/<app>_1.0.bb             # do_install 里加 .mo 和 ln_i18n 依赖
```

`do_install` 里装翻译（注意 `.mo` 是**编好的**，不在板子上编译）：

```bash
install -d ${D}${datadir}/locale/en/LC_MESSAGES
install -m 0644 ${S}/locale/en/LC_MESSAGES/<domain>.mo \
    ${D}${datadir}/locale/en/LC_MESSAGES/<domain>.mo
```

`RDEPENDS` 加 `ln-i18n`（DEBIAN/UBUNTU 维度是 rsync 的 rootfs，包名照写，
它在 `conf/deb-track/bsp-userspace.txt` 里已登记）。

### 4. 提交前跑门禁

```bash
python3 layers/meta-quectel/recipes-quectel/i18n/tools/i18n-check.py
python3 .../i18n-check.py --app npu-chat          # 只查一个
python3 .../i18n-check.py --app npu-chat --refresh # 改了源码后重抽 pot
```

四项检查：漏包的中文字面量、f-string、en.po 有空 msgstr、en.mo 与 en.po 不同步。
**四项全绿才算改造完成。**

## 常见问题

**Q: 为什么不用 `inherit gettext` + `oe_runmake`？**
这些应用是纯 Python 脚本、没有构建步骤，`.mo` 是仓库里的交付物而不是编译产物。
OE 的 `gettext` 类解决的是"源码里 po/ 目录 + Makefile 编译"，这里没有 Makefile。
把 `.mo` 入库的好处是：DEBIAN/UBUNTU 镜像走 rsync overlay，不经过 bitbake 的
`do_compile`，编出来的 .mo 本来就不会被拷进去。

**Q: 板上切语言怎么切？**
GNOME「设置 → 区域与语言」改语言会写 `AccountsService` 的 `Language=` 和
`~/.pam_environment` / `/etc/default/locale`，下次登录 `LANG` 就变了，应用重启后生效。
不需要 root，也不需要额外 locale（`zh_CN.utf8` / `en_US.utf8` 板上都有）。

**Q: 为什么不给中文也出 .mo？**
那会让中文显示依赖 `.mo` 存在。少一个文件就整机变英文，风险方向不对。

**Q: 新增一个应用怎么办？**
1. 在 `i18n-check.py` 的 `APPS` 里加一条（名字 / domain / 源文件）；
2. `--app <name> --refresh` 生成 pot；
3. 翻译出 `po/<app>/<domain>.po`，`msgfmt -o <domain>.mo <domain>.po`；
4. recipe 里按第 3 节装 `.mo`、加 `RDEPENDS`；
5. 跑门禁。
