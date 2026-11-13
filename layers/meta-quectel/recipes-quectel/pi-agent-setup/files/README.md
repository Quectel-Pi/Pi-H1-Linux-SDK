# Pi 编程助手（pi-agent-setup）

这个包**不预装 pi**。它提供：

- 应用列表里一个图标 **“Pi 编程助手”**
- 首次登录自动弹出的安装向导

两者都指向同一个小启动器 `/usr/bin/pi-agent`，由它按当前状态分派：

| 状态 | 点图标后 |
|---|---|
| pi 未安装 | 弹安装向导 |
| pi 已装、但 API 没填 | 直接打开向导的“配置 API 端点”页（不重复问要不要装） |
| pi 已装且已配置 | 开一个终端，在 `~/pi-workspace` 里运行 `pi` |

> 为什么需要这层 shell：`.desktop` 的 `Exec` 不能判断状态，同一个图标要"装前引导、
> 装后运行"就必须有个分派器。用 shell 而不是 python，是为了点开 pi 时不必先起一个
> Python + GTK 进程。

想强制打开向导（改端点、重装）：

```bash
/usr/bin/pi-agent --setup
```

右键图标也能看到“安装 / 重新配置向导”这一项。

## 首次登录弹窗

开机登录 GNOME 后，如果 pi 还没装（或装了没配），会自动弹窗：

```
安装 Pi 编程助手？
  需要联网：下载 Node.js 运行时和 pi 本体（约 250 MB）
  不需要管理员密码：全部装在你的主目录（~/.local）
  随时可卸载

  [开始安装]  [下次询问]  [不再安装]
```

- **下次询问**：7 天后再弹（记录在 `~/.config/pi-agent-setup/state.json`）
- **不再安装**：以后不再自动弹；图标仍在应用列表里，点它就是安装向导
- **开始安装**：先检查网络（nodejs.org 和 npm registry），通了才继续

安装过程：

1. 从 nodejs.org 下载官方 Node.js 22 LTS（aarch64 包，`SHASUMS256.txt` 验签）
   解到 `~/.local/share/pi/node/`
2. `npm install -g --prefix ~/.local/share/pi/npm-global @earendil-works/pi-coding-agent`
3. 在 `~/.local/bin/pi` 建软链
4. 弹第二页要 **API URL / API Key / 模型 ID / 接口类型**
   - 填好 URL 和 Key 后点 **探测模型**，会列出端点提供的模型，下拉选择即可
   - 也可以**直接手填**模型 ID：端点不实现 `GET /models` 时只能手填，
     探测失败不会阻塞手填
   - "测试连接"验证这套配置是否真的能用
5. 写 `~/.pi/agent/{models,auth,settings}.json`，`auth.json` 权限 600
6. 最后一页给出启动方法（也可点"复制启动步骤"）

## 装完之后怎么用

两种方式等价：

1. 点应用列表里的 **Pi 编程助手** 图标 → 自动开终端进 `~/pi-workspace` 并运行 pi
2. 自己开终端：

   ```bash
   cd ~/my-project
   pi
   ```

窗口里也写了：`/model` 切模型、`/login` 换 Key、`Ctrl+C` 退出、
`pi -c` 继续上次会话。

## 卸载

向导装的东西全在主目录，删掉即可：

```bash
rm -rf ~/.local/share/pi ~/.local/bin/pi
# 想连会话和配置一起清：
rm -rf ~/.pi ~/.config/pi-agent-setup
```

不需要 root，不动系统文件，也不需要 `apt`。

## 为什么用不了 sudo

这条完全是无 root 设计的：

| 环节 | 做法 |
|---|---|
| 下载 | python3 `urllib`（Debian 13 样例 rootfs 里**没有** `/usr/bin/curl`） |
| 解压 | python3 `tarfile` + `lzma` |
| 装 pi | npm 装到 `--prefix ~/.local/share/pi/npm-global` |
| 配置 | 写 `~/.pi/agent/*.json` |

整条链路的写权限都在这位桌面用户自己身上，所以不会弹 polkit/密码框，也不会
因为升级固件被覆盖。

## 模型探测支持哪些端点

"探测模型"会按下面的顺序尝试，第一个能解析出模型列表的就用它。都不行时
退回手填，不影响后续保存。

| 端点类型 | 探测请求 | 响应格式 |
|---|---|---|
| OpenAI 兼容 | `GET /models` | `{"data":[{"id":...}]}` |
| OpenAI 兼容（网关） | `GET /v1/models` | 同上 |
| Anthropic | `GET /models` | `{"data":[{"id":...}]}` |
| Google Generative AI | `GET /models?key=...` | `{"models":[{"name":"models/..."}]}` |
| Ollama | `GET /api/tags` | `{"models":[{"name":...,"size":...}]}` |

Google 的 `models/` 前缀会被自动剥掉（那部分不属于模型 id）；Ollama 的
`size` 会显示成 GB。列表去重并按名字排序。

很多商业网关故意不实现 `GET /models`（或要求不同鉴权），所以**手填永远可用**，
这是刻意设计：只允许从列表里选，会让这类端点根本配不了。

探测与"测试连接"都在后台线程跑，主线程用轮询取结果，超时分别是 45s / 60s——
按钮不会因为一个不响应的端点永久变灰。

## 命令行（无桌面时）

GUI、图标、命令行共用同一个引擎：

```bash
pi-agent --status                 # 看状态（同安装向导的一页）
pi-agent --setup                  # 强制打开向导
pi-agent --here                   # 在当前目录跑 pi
pi-agent --only-if-needed         # autostart 用：没事就静默退出

# 引擎本体（ssh 里也能用）
python3 /usr/share/pi-agent-setup/engine.py check-network
python3 /usr/share/pi-agent-setup/engine.py install [--mirror]
python3 /usr/share/pi-agent-setup/engine.py configure \
    --url https://api.example.com/v1 --key sk-... --model my-model --test
python3 /usr/share/pi-agent-setup/engine.py snooze --days 7
python3 /usr/share/pi-agent-setup/engine.py decline
python3 /usr/share/pi-agent-setup/engine.py uninstall
```

`--mirror` 只在国内网络打不开 nodejs.org / registry.npmjs.org 时用：
node 改走 npmmirror，npm 加 `--registry https://registry.npmmirror.com`，
镜像也失败时自动退回官方源。

## 装的到底是什么

pi 的配置目录是 `~/.pi/agent/`（可用 `PI_CODING_AGENT_DIR` 覆盖），本向导写入：

| 文件 | 内容 |
|---|---|
| `models.json` | 一个 provider `pi-setup`，里面一条模型，`baseUrl` 来自你填的 URL |
| `auth.json` | `{"pi-setup": {"type": "api_key", "key": "..."}}`，权限 600 |
| `settings.json` | `defaultProvider` / `defaultModel` / `enabledModels` |
| `pi-agent-setup.json` | 向导自己的记录（版本、端点、配置时间），用于判断还要不要弹窗 |

`models.json`、`auth.json`、`settings.json` 是 pi 自己的文件，向导只合并
不覆盖：已有其它 provider、其它扩展设置都会保留。手写坏的 JSON 会被备份成
`*.bak-<时间戳>` 再重建，不会静默丢掉内容。

## 验证过的事实（2026-11，本机与样例 rootfs）

- Debian 13 样例 rootfs：`python3 3.13` + `python3-gi 3.50` +
  `gir1.2-gtk-4.0 4.18.6` + `gir1.2-adw-1 1.7.6`，**无** node/npm/curl
- Ubuntu 26.04 样例 rootfs：`python3 3.14` + `python3-gi 3.56` +
  `gir1.2-gtk-4.0 4.22.4` + `gir1.2-adw-1 1.9.1`
- pi 1.5.2 要求 `node >= 22.19.0`；node 22 LTS 当前为 v22.23.5
- 引擎在 x86_64 沙箱 HOME 里实测：下载 node v22.23.5 → npm 装 pi 1.5.2 →
  写三份 JSON → `pi --version` = 1.5.2、`pi auth check --provider pi-setup` =
  `ready`、`pi --model my-model` 指向配置的端点
- GUI 五个页面在 GTK 4.6（本机 Xvfb）全部正常构建；用的都是 GTK 4.6 就有的
  API，所以 4.14/4.18/4.22 也覆盖
- 图标：`pi-agent.svg` 在真实 GTK 图标主题里被解析为 `pi-agent`（16/48/128px
  都定位到同一份 SVG）；在 48px 以上珊瑚色 `>` 与蓝色 `_` 清晰可辨
- 模型探测：OpenAI / Anthropic / Google / Ollama 四种返回格式都用本地 mock
  端点验证过解析正确（含 Google 的 `models/` 前缀剥离、Ollama 的 size 转 GB、
  去重排序）；404 与非 JSON 响应回退到手填并给出原因
- 探测不阻塞 UI：`_on_probe_clicked` 在板子（Wayland/GNOME，GTK 4.18）上
  实测 6 ms 返回；对不可达端点，50s 内给出 `Errno 111` 原文并恢复按钮
- 启动器六个分支实测：未安装→向导、半配置→`--page config`（预填 URL/模型，
  Key 留空）、已配置→终端内运行 pi、`--setup`→强制向导、`--here`→当前目录、
  autostart 已配置→静默退出 rc=0

## 已知取舍

- **不预装 node**：走官方 tarball 是为了不依赖发行版源（Debian 13 的
  nodejs 是 22.22.2，已经满足 pi 的 >= 22.19，但版本由发行版锁定，且装它要
  root）；代价是占用户主目录约 250 MB。
- **不用 `libadwaita` 的 `EntryRow`**：`Adw.EntryRow` 是 1.2 才有的，本机
  GTK 环境是 1.1.7，用 `Gtk.Entry` 才能让同一份代码到处能跑并能本地验证。
- **"不再安装"写的是状态文件，不是删 autostart**：这样图标还在应用列表里、
  随时能手动装；卸载包才会真正移除 autostart。
- **pi 是终端程序，所以"点击图标打开 pi"= 开终端**。没有做成嵌入窗口或
  webview：pi 的交互（Ctrl+C、`/model` 选择器、多行编辑）都是终端原生的。
  终端按 `x-terminal-emulator` → `gnome-terminal` → `kgx` → `xterm` 顺序找。
- **SVG 注释位置有要求**：`<svg>` 必须在前 256 字节内，否则 gdk-pixbuf
  嗅探不到、图标静默不显示（SVG 本身合法，`xmllint` 也过）。已在文件里注明。
