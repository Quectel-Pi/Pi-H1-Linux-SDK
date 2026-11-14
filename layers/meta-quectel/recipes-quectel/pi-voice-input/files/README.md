# pi-voice-input — 按住右 Ctrl 说话，松手上屏

QCS6490（Quectel PI H1）Debian/Ubuntu GNOME 镜像的语音输入服务。没有悬浮窗：
唯一的界面是**状态栏里的一个开关**。

```
状态栏话筒图标/开关 ──┐
                      ├── D-Bus 会话总线 org.quectel.PiVoiceInput
按住右 Ctrl ──────────┘        └── pi-voice-inputd（后台常驻，用户态）
                                      ├─ ALSA/PipeWire 录音
                                      ├─ sherpa-onnx + SenseVoice（CPU）
                                      └─ 识别文字 → 扩展 → 焦点窗口
```

## 组成

| 文件 | 作用 |
|---|---|
| `/usr/bin/pi-voice-inputd` | 守护进程。唯一持有麦克风和识别模型；开关打开才加载模型 |
| `/usr/bin/pi-voice-hotkey` | 读 `/dev/input`，把**右 Ctrl** 的按下/松开转发给守护进程 |
| `/usr/bin/pi-voice-input` | 命令行：`on` / `off` / `toggle` / `state` / `client` |
| `gnome-shell/extensions/pi-voice-input@quectel.com` | 状态栏开关 + 文字上屏 |
| `pi-voice-input.service`、`pi-voice-hotkey.service` | **用户**（`systemd --user`）单元 |
| `org.quectel.PiVoiceInput.service` | D-Bus 激活文件，第一次调用时自动拉起守护进程 |

## 三个关键设计取舍

### 1. 为什么不用悬浮窗

悬浮窗在 GNOME/Wayland 下既抢不到焦点、也没法保证停在最上层（`xdg-shell` 不允许
应用自己置顶，需要 layer-shell 协议，Mutter 不实现）。状态栏开关是 GNOME 自己
画的，永远在最上层、点击一定生效，而且「关掉=释放资源」这件事放在一个可见的
开关上比藏在悬浮窗菜单里更符合直觉。

### 2. 「松开结束」只能读键盘，不能靠全局快捷键

GNOME 自定义快捷键（`org.gnome.settings-daemon.plugins.media-keys
.custom-keybinding`）**只在按下时触发一次，不通知松开**。只靠它，按住/松开
这种手势无法表达，只能退化成「按一次开始，再按一次结束」。

保持真正的「按住说话」有两个办法，本方案用前者：

- **读 `/dev/input/event*`**（`pi-voice-hotkey`）：这些设备属于 `input` 组，
  Debian/Ubuntu 的桌面用户 `p` 本来就在这个组里，所以**不需要 root**。
  它只读按键事件，不抓取、不拦截，不影响其他程序。
- GNOME 自定义快捷键 `<Control>q` → `pi-voice-input client`：作为**兜底**。
  在没有 `input` 组权限、或内核事件设备不可读时，按两次也能用（先查状态：
  recording 就停，否则开始）。

**主绑定是「单独按住右 Ctrl」**，不是 Ctrl+Q。理由见下面的已知限制：
`pi-voice-hotkey` 只读不拦，Ctrl+Q 在多数程序里是"退出"，按住说话会顺手把
目标窗口关掉。右 Ctrl 单独按在 GNOME 和常见应用里没有任何绑定，用作对讲键
不会误触。Ctrl+Q / Ctrl+Alt+V 仍然保留，作为旧版本文档的兼容入口。

> 已在本机验证：`/dev/uinput` 是 `root:root 0600`，`/dev/input/event*` 是
> `root:input 0660`，用户 `p` 在 `input` 组内。

### 3. 文字怎么进到焦点窗口

Wayland 下**没有**跨应用打字的通用办法：

| 办法 | 结论 |
|---|---|
| `wtype` / 虚拟键盘协议 | GNOME/Mutter 未暴露 `zwp_virtual_keyboard_manager_v1`（已查 `libmutter-18.so.0`、`gnome-shell` 的字符串表，均无） |
| `ydotool` / `/dev/uinput` | 需要 root（`root:root 0600`），且**打不了中文**（只发 keycode，靠键盘布局翻译） |
| XTest | 只对 Xwayland 生效，GNOME 会抢走焦点（实测注入的 Ctrl+Alt+T 也不触发，因为合成器不把合成事件当快捷键输入） |
| **`Clutter.InputMethod`（IBus 引擎同款路径）** | **采用**。GNOME 50 的 `libshell-18.so` 内置 `misc/inputMethod.js`，`Main.inputMethod.commit(text)` 就是把文本交给合成器再送进焦点输入框，任何 GTK/Qt/终端应用都收 |

所以扩展在收到 `TextRecognized` 信号后调用 `Main.inputMethod.commit(text)`。
这是 IBus 引擎之外的唯一非特权路径，也是唯一能输入中文的路径。

## 状态机

```
        ┌──── 关 ────┐
        │  off       │  模型未加载，内存已释放
        └─────┬──────┘
        打开开关│关闭开关
        ┌─────▼──────┐
        │  idle      │  模型常驻（约 250 MB RSS）
        └─────┬──────┘
        按下  │  松开
        ┌─────▼──────┐
        │ recording  │  录音中（≤60 s 硬上限）
        └─────┬──────┘
        松开   │
        ┌─────▼──────┐
        │recognizing │  解码（~17× 实时）
        └─────┬──────┘
              └──► idle，文字发给扩展上屏
```

- **关闭时释放**：`SherpaOnnxDestroyOfflineRecognizer()` 之后 onnxruntime 的权重
  和 arena 全部归还，`/proc/<pid>/status` 的 `VmRSS` 应当掉回 ~10 MB。这就是
  「关掉开关释放 NPU 资源」的落点（见下）。
- 关闭时正在录音：先 `StopRecording`（join 录音线程）再释放模型，不会野指针。
- 按下短于 250 ms 或 RMS 低于 60：丢弃，不解码（解纯噪声会出幻觉文字）。
- 录音 60 s 强制结束，避免忘松手时无限占用麦克风。

## 关于「释放 NPU 资源」

这里有一个必须讲清楚的事实：**语音识别并不跑在 NPU 上，所以它本来就不占用
NPU**。它是 sherpa-onnx + SenseVoice int8，跑在 CPU 上（npu-chat 里也是同样的
配置 `provider=cpu`）。原因：

- SenseVoice small 在 4 核上约 17× 实时，一次 5 s 的话 0.3 s 出结果，NPU 带来
  的收益抵不上它带来的问题；
- QCS6490 的 HTP 上下文是按模型整体占用的，`qwen35-npu` 在跑的时候整个 850 MB
  上下文都归它，这时再去开一个 ASR 的 HTP 会话会直接抢不到 DSP，表现为
  `Failed to create device: 14001`；
- 反过来，ASR 占着 DSP 时 LLM 的生成会明显掉速。

所以这个开关释放的是**语音模型的 CPU 侧资源**（约 250 MB RSS + 4 个推理线程），
而 NPU 留给了真正需要它的大模型。如果将来要做 NPU 版 ASR（QNN EPContext），
`pi-voice-input.service` 里加一条 `PartOf=qwen35.service` + 切换逻辑即可；现在的
结构（模型生命周期全部集中在 `recognizer_get()`/`recognizer_free()`）就是为了
这种替换不需要改别的地方。

## 为什么模型不是自己打的包

识别模型和 sherpa-onnx 的动态库都来自 `npu-llm` 配方
（`/usr/share/npu-llm/voice/asr/model.int8.onnx`、`/usr/lib/npu-llm/voice/
sherpa_onnx/lib/`）。板子上只有一份，两个前端（npu-chat 和本服务）共用，参数
（`use_itn=1`、`language=zh`、`greedy_search`、`num_threads=2`）与 npu-chat 完全一致，
所以两条路径的识别结果不会各说各话。

`libsherpa-onnx-c-api.so` 用 `dlopen()` 而不是链接：它旁边的
`libonnxruntime.so` 是 npu-llm 的私有副本（靠 `$ORIGIN` rpath 找），不在系统
链接路径上；`dlopen` 也让缺少 npu-llm 的板子得到一句清楚的错误，而不是
`main()` 之前就被 loader 打死。

## 触屏板上没有键盘时：屏幕按钮

状态栏开关仍在，但如果**没有任何键盘**（只有触屏），右 Ctrl 无从按起，扩展会在
**屏幕中部偏右**放一个圆形话筒按钮：按住说话、松手上屏，按住时变红。

判据全部来自板上实测，不是设计推测：

| 条件 | 判据 |
|---|---|
| 有没有键盘 | 守护进程的 D-Bus 属性 `keyboard`，`Properties.Get` / `GetAll` 都能读 |
| 按钮何时出现 | `enabled && !keyboard && 有文本框获得焦点` |
| 何时消失 | 焦点离开输入框（和软键盘同步），或开关关闭 |

两个容易搞错的地方，都已踩过并写进代码注释：

- **按钮不能只接 `button-press-event`**。Clutter 把手指当成 *touch* 事件，不是
  button 事件，所以只接 button 信号时，触屏按住按钮**完全没反应**，而同一坐标
  用鼠标按下能正常录音（板上 A/B 实测）。`St.Button` 自己的触摸处理在 C 里，
  在 GJS 里接不到；`Clutter.ClickAction` 在 Clutter 16 已没有
  `pressed`/`released` 信号（只有 clicked/long-press/may-recognize），接上就抛
  `No signal 'pressed'`。最终用 actor 自己的 `touch-event` 信号，配合
  `event.type()`（不是 `get_event_type()`，那个名字与 GObject 冲突）判
  `TOUCH_BEGIN` / `TOUCH_END`。
- **不能靠 `/proc/bus/input/devices` 里有没有 `kbd` 判断键盘**。GT911 触屏会
  注册 `Handlers=kbd mouse0 eventN`，按这个判据会把触屏当成键盘，按钮永远不
  会出现。真正的判据是「能不能承载右 Ctrl」，用 sysfs
  `capabilities/key` 查 KEY_Q + KEY_LEFTCTRL，与 `pi-voice-hotkey` 的
  `is_keyboard()` 同一套条件。
- **按钮不能放右下角**。文本框获得焦点后软键盘占住屏幕下方，按钮正好被盖住；
  放正中会压在正在口述的文字上。所以取垂直居中、水平 0.78 处（1280×800 上是
  x≈950、y≈352）。

`Main.inputMethod._currentFocus` 是「有文本框在编辑」的准确判据（`vfunc_focus_in`
置位、`vfunc_focus_out` 清空），它没有信号，所以扩展每 250 ms 采一次样；那不是
`Main.keyboard.visible`——本镜像没开软键盘，那个值永远是 false。

## 构建与安装

配方：`layers/meta-quectel/recipes-quectel/pi-voice-input/pi-voice-input_1.0.bb`

```bash
# 一次性把它加进镜像（或者按下面的 em 方式临时验证）
echo 'IMAGE_INSTALL:append = " pi-voice-input"' >> \
    build-qcom-wayland/conf/local.conf     # 仅本地验证用，正式应改进 quecpi-image.bb
bitbake pi-voice-input
```

装到板上（不重烧）：

```bash
bitbake pi-voice-input
./quectel_build/tools/deb-iterate.sh pi-voice-input   # 走 apt 迭代，见 deb-iterate skill
```

## 板上验证清单

每一条都要有**客观判据**，不能靠"看着像"：

```bash
# 1. 服务起来了，开关能开能关
systemctl --user status pi-voice-input pi-voice-hotkey
pi-voice-input state            # off 0 0
pi-voice-input on; sleep 2; pi-voice-input state   # idle 1 0

# 2. 开的时候模型真的加载、关的时候真的释放（这是"释放资源"的判据）
grep VmRSS /proc/$(pgrep -f pi-voice-inputd)/status   # on 之后 ~250 MB
pi-voice-input off; sleep 1
grep VmRSS /proc/$(pgrep -f pi-voice-inputd)/status   # 应当掉到 ~10 MB

# 3. 按键状态机（不需要麦克风也能跑）
/usr/libexec/pi-voice-input/hotkey-unittest           # 全部 PASS，退出码 0

# 4. 热键真的读到了键盘
journalctl --user -u pi-voice-hotkey -f   # 按下时应出现 "recording"
                                          # 松开时 "released after NNN ms"

# 5. 端到端：光标放进任意输入框，按住右 Ctrl 说"今天天气不错"，松手
#    判据：输入框里出现那句话，且 journal 里 ASR 耗时 < 1 s（对 3~5 s 语音）
```

## 已知限制

- **按键/设备热插拔不需要重启服务**。`pi-voice-hotkey` 用 inotify 盯
  `/dev/input`，并且每秒检查已打开的 fd 是否已被删除（`nlink==0` 或
  `EVIOCGNAME` 失败），所以拔插键盘、USB 掉电重连都会自动重扫。踩过的坑：
  拔掉后重新枚举的键盘是**另一个** `eventN`，旧 fd 仍可用但永远没有事件，
  服务不退出、`Restart=on-failure` 也不救——功能会静默失效到下次重启。
  按住期间设备消失会自动结束这次录音，不会卡到 60 s 上限。
- **两个 unit 挂 `graphical-session.target`，登录后才可用**。`default.target`
  不行：用户级的 systemd 实例在注销后仍然存活，`default.target` 已是 active，
  再登录不会重新拉取 wants，守护进程就一直是死的（板上实测）。
  `graphical-session.target` 每次登录重新激活、注销时停止，正是需要的生命周期。
- **焦点必须在能接受文字的输入框里**。桌面空白处按 Ctrl+Q 不会出现在任何地方
  （`Main.inputMethod.commit` 只会送到有输入上下文的应用）。这是 Wayland 的
  设计，无法绕过。
- **快捷键选的是「单独按住右 Ctrl」，因为这个组合不会撞车**。`pi-voice-hotkey`
  只读不拦：它看到按下的同时，目标程序也会收到同样的按键。Ctrl+Q 在多数程序里
  是"退出"、Ctrl+Alt+V 在 LibreOffice 里是"选择性粘贴"，按住说话会顺手触发
  它们。裸修饰键（右 Ctrl）在 GNOME 和常见应用里没有绑定，所以是唯一不会误触
  的选择；`Ctrl+Q` / `Ctrl+Alt+V` 仅为兼容旧文档而保留。
  真正要拦截需要 compositor 里的键盘 grab，那得写 C 的 GNOME Shell 扩展，
  不在本方案范围内。
- **第一次识别会慢一点**（模型加载约 1~2 s），之后常驻。开关打开时已经预热。
- 60 s 上限是有意为之；更长的语音应该分段说。
