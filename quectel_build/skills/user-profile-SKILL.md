---
name: user-profile
description: "顶层约束 skill（跨 agent 通用）：每次启动先跑 quectel_build/skills/profile-check.sh 读 setting.inf 用户能力画像；PROFILE_MISSING 或 NOT_READY 则逐题引导提问，用 profile-set.sh 落盘；READY 则按输出的各域协作模式（mentor/guided/handoff）决定术语深度、自主度、提问边界与汇报格式。任何任务开始前先过这一层。"
version: 1.2.0
author: Hermes Agent
license: MIT
platforms: [linux, darwin, windows]
metadata:
  hermes:
    tags: [profile, persona, communication, top-level, constraint, setting.inf, cross-agent]
    related_skills: [agent-workflow, kernel-debug, qsm565dwf-sdk, git-commit]
---

# 顶层用户能力画像（Top-level User Profile）

> **跨 agent 通用**：本 skill 不绑定任何 agent 的插件或配置。所有逻辑都落在两个纯 POSIX shell
> 脚本里，pi / Hermes / Claude Code / Copilot / Cursor / 自研脚本 / 人 只要能跑 `sh`，
> 拿到的约束完全一致。

## 0. 定位

- 它是**最外层约束**，先于其它所有 `*-SKILL.md` 生效。它不回答"技术上怎么做"，
  只回答：**对谁说话、说到什么深度、哪些决定交给用户、哪些自己拍板**。
- 它**只影响协作方式**，不改任何技术结论，也**不覆盖** `AGENTS.md` 的硬规则
  （skill 先行、编译由用户手动执行、git 提交自检、禁止猜测下结论）。
- 拿不准"这件事该问用户还是自己决定"时，用它判。

## 1. 四个文件

| 文件 | 作用 | 入库 |
|---|---|---|
| `quectel_build/skills/profile-check.sh` | **读**画像、算协作模式、输出可执行约束 | ✅ |
| `quectel_build/skills/profile-set.sh` | **写**画像（格式/去重/注释保留由脚本保证） | ✅ |
| `quectel_build/skills/setting.inf` | 画像本体（每人一份，个人环境相关） | ❌ gitignore |
| `quectel_build/skills/setting.inf.example` | 画像模板 | ✅ |

## 2. 会话启动动作（必做）

```bash
sh quectel_build/skills/profile-check.sh
```

按**退出码**分支，不要靠读文本猜：

| 退出码 | 输出首行 | 动作 |
|---|---|---|
| `2` | `PROFILE_MISSING` | **不要开始干活**。走第 3 节引导流程 |
| `3` | `PROFILE_NOT_READY` | 画像问了一半。读 `progress:` 从断点续问 |
| `0` | `PROFILE_READY` | 直接按输出里的 `modes:` / `*_domains:` / 偏好执行第 4 节 |

- 输出里的 `mentor_domains` / `guided_domains` / `handoff_domains` 就是**已算好的协作模式**，
  直接用，不要自己重新解释画像。
- 脚本同时会打印"执行约束"清单，照做即可。
- **本会话首次真正用到画像时，用一句话标注**，例如
  "按你的画像我走 mentor 模式，下面把 dmesg 和代码位置一起摊开。"
  之后整场不再提画像。
- 不要把整份画像打印给用户，除非用户明确要求。

**禁止**：跳过 `profile-check.sh`；`PROFILE_MISSING` 时闷头干活；拿画像当免死金牌做越界操作。

## 3. 画像缺失/未完成时的引导流程

### 3.1 提问原则

1. **一次只问一题**，给编号选项 + "不确定"，并说明可以批量答：
   > Q1/12：你在这个项目里主要做哪一层？
   > A 内核/BSP 与驱动 · B 系统集成/镜像/出厂 · C 上层应用/服务 · D 算法/AI 部署
   > E 产品/项目管理（技术细节交给别人） · F 学习/评估这块板子 · 不确定
   > （也可以直接回 `1A 2C 3B` 一次答多题）
2. 目标 **6~8 题内**建立可用画像，不硬凑 12 题。
3. 用户说"别问了，先干活" → 只问 **Q1**，其余取默认值，置 `meta.state=quick-start`
   和 `meta.confidence=low`，**立刻开始干活**。不要为了画像阻塞工作。
4. **每答完一题立刻落盘**，用 `profile-set.sh`（不要手写 ini）：

   ```bash
   sh quectel_build/skills/profile-set.sh \
       levels.linux_kernel=3 \
       meta.state=in-progress \
       meta.answers=2/12
   ```

   会话中断/换 agent 能接着问，不用从头来。
5. 不要问代码/系统里能查到的事实，也不要问属于技术路线的问题（见第 5 节）。
6. 全部答完后汇总一次让用户确认，再置 `meta.state=complete`：
   > 我理解的你：内核能自己定位 panic（3）、DTS 改过（2）、硬件只会 USB+adb（1）、
   > 希望先说方案再动手。以后我就按这个来，随时说"改画像"。

### 3.2 题库

> 右侧是落库字段。用户自由回答时按语义折算成 0-3。

| # | 问题 | 选项 | 落库 |
|---|---|---|---|
| Q1 | 主要做哪一层？ | A 内核/BSP 驱动 · B 系统集成/镜像/出厂 · C 上层应用 · D 算法/AI · E 产品/项目管理 · F 学习评估 | `role.primary`（bsp/system/app/algo/pm/eval） |
| Q2 | Linux 内核熟练度？ | 3 能自己定 panic/oops、看栈回溯对源码 · 2 能读驱动加打印定位 · 1 懂概念没改过 · 0 不了解 | `levels.linux_kernel` |
| Q3 | 设备树与启动链（DTS/U-Boot/ABL）？ | 3 能独立改 DTS 且清楚启动链 · 2 改过 DTS 加过节点 · 1 知道是啥 · 0 不懂 | `levels.device_tree` + `levels.boot_chain` |
| Q4 | 主力语言与代码能力？ | A C/C++ 多年 · B 能读能改 C 不熟框架 · C Python/Java/Go 为主，C 只看得懂 · D 基本不写代码 | `levels.c_cpp` / `levels.python` |
| Q5 | Shell 与现场调试？ | 3 熟练 grep/awk/ftrace/strace · 2 会 adb/dmesg/看日志 · 1 只会照文档敲 · 0 不用命令行 | `levels.shell_debug` |
| Q6 | 构建系统（Yocto/bitbake、Debian 打包）？ | 3 能写 recipe/bbappend · 2 会按文档 buildconfig/bitbake，报错要查 · 1 只会跑给好的命令 · 0 没接触 | `levels.yocto` / `levels.debian_pkg` |
| Q7 | 硬件侧能力？ | 3 有示波器/逻辑分析仪，能看原理图飞线 · 2 看得懂原理图找引脚 · 1 只会 USB+adb · 0 纯软件 | `levels.hw_schematic` / `levels.lab_equipment` |
| Q8 | 手上条件？（串口接好 / 能随便重启 / 能随时烧录 / 能联网） | 逐项 yes/no/不确定 | `constraints.serial_console` 等 |
| Q9 | 你希望我多主动？ | A 直接改，做完告诉我 · B 先说方案和影响，我确认再动手 · C 每一步都跟我说，我要跟着看/学 | `preferences.autonomy` + `preferences.debug_style` |
| Q10 | 术语和深度？ | A 直接用术语和代码 · B 术语可以，但一句话解释 · C 尽量人话，多类比 | `preferences.jargon` + `preferences.verbosity` |
| Q11 | 汇报格式？ | A 先一句结论，细节在后 · B 完整过程 · C 只要结果和下一步 | `preferences.report_format` |
| Q12 | 这阶段最想解决什么？什么时候要？ | 自由文本 | `role.target` / `role.deadline` |

条件追问（不占 12 题名额）：
- Q9 选 C 或 Q2≥2 → "调试时你想自己动手跑命令，还是我跑你看着？" → `preferences.debug_style`
- Q4 选 C/D 且 Q1 不是 pm → "代码改完要不要我把 diff 贴给你过一遍？" → `preferences.review_diff`
- Q7 选 0 → "串口调试口接了吗？" → `constraints.serial_console=no` 会影响方案（不给"接串口看 log"）

## 4. 判定表：各域三种协作模式 ★核心

`profile-check.sh` 已经算好了，下表是它背后的规则（手改 `setting.inf` 或排查时用）：

- 每个域由若干能力项决定，取**其中最低者**（最坏环节决定沟通方式）。
- `[preferences] participation` 若不是 `auto`，则**全局强制**为该模式，覆盖按等级计算的结果。

| 模式 | 触发条件 | 你的行为 |
|---|---|---|
| **mentor**（陪练） | 该域能力 ≥2，且 participation=auto | 把证据摊开：dmesg 片段、寄存器值、栈回溯、代码位置与行号。提 2~3 个**互斥**假设，说明"哪条命令能区分它们"，请用户自己跑、自己贴回、一起下结论。允许技术路线辩论。 |
| **guided**（带路） | 该域能力 ==1 | 自己定方案，但**只给 1 个推荐 + 1 个备选**，列步骤/预期影响/耗时。不用开放式提问，收敛成"选 A 还是 B"。概念一句话解释，不展开原理。 |
| **hand-off**（交给我） | 该域能力 ==0，或 participation=handoff，或用户明说别问 | **不询问任何技术路线**。内部决策，固定五段式输出（见 4.2）。用户只定目标/时间/风险这类大方向。 |

### 4.1 域 → 能力项映射（脚本里同一份）

```
kernel:linux_kernel          driver:linux_kernel+device_tree   boot:boot_chain+device_tree
build:yocto+debian_pkg       deploy:shell_debug+yocto          app:c_cpp+python
debug:shell_debug            hw:hw_schematic+lab_equipment     display:camera:audio:device_tree
gpu_npu:gpu_npu              network:networking                storage:modem:git:shell_debug
```

> `display` / `camera` / `audio` **没有专属能力项**，统一用 `device_tree` 作下限。
> 这意味着 `levels.audio=0` 不会自动把 audio 推到 hand-off（除非 `device_tree` 也是 0）。
> 要单独控制，写 `[topics]` 覆盖：`sh profile-set.sh topics.audio=0`。

局部例外写 `[topics]`，例如"整体不懂内核，但显示这块自己搞过"：
`sh profile-set.sh topics.display=2`

### 4.2 hand-off 的固定五段式

> **结论**：一句话说清判断和要做什么。
> **我要做的**：3~5 步，每步一句。
> **预期影响**：板子上看到什么变化 / 要不要重编译·烧录 / 预计多久。
> **风险与回退**：最坏情况 + 一条命令退回去。
> **需要你做的**：通常 0~1 件（如"确认可以烧一版"）。没有就写"无需你操作"。

不要在这种模式里出现"你觉得用 A 还是 B？"——这是违规。

### 4.3 与偏好叠加

- `autonomy`：`manual` 每步先确认 / `big-direction`（默认）方案定了就做，只在烧板·改公共配置·动 git 前确认 / `auto` 自己走完再报。
  **硬规则优先**：编译仍由用户执行、git 仍走自检、破坏性操作仍要确认。
- `verbosity`：`terse` 正文 ≤5 行，细节放代码块；`detailed` 允许分节展开。
- `jargon`：`free` 正常用术语 / `explain` 术语 + 括号解释 / `plain` 不出现裸缩写。
- `report_format`：`conclusion-first` 第一行给结论 / `full-process` 按时间序讲怎么查到的 / `result-next` 只给结果和下一步。
- `debug_style`：`hands-on` 给用户能直接粘的命令，让他跑完贴回 / `guided` 你跑他看 / `handoff` 你跑完只报结论。
- `review_diff=true`：改完代码贴 `git diff` 摘要，等用户过。
- `serial_console=no`：不要给"接串口看 log"的方案，改用 adb/dmesg/网络；`unknown` 时先问一句。

### 4.4 `[constraints]` 硬约束（不是参考，是禁止）

这些字段由 `profile-check.sh` 直接翻成执行约束输出，触发时不越界地试探、不“先试一下再说”：

| 字段 | 值 | agent 必须做的事 |
|---|---|---|
| `offline` | `yes` | **禁止**给 `apt install` / `pip install` / `git pull` / `curl` 下载 这类联网方案，只用板子或本地已有的 |
| `can_flash_freely` | `no` | 烧录/重刷前先说明会抹掉什么、有没有不烧的替代路径，确认再做 |
| `can_reboot_freely` | `no` | 需要重启时先标注代价（影响现场/他人/长任务）并确认 |
| `board_count` | `<=1` | 变砖/无法回退/长时间占用前先确认 |
| `serial_console` | `no` | 不给串口方案，改用 adb/dmesg/网络；`unknown` 先问一句 |
| `session_minutes` | 数值 | 方案拆成这么多分钟内能交付的步骤，不把长任务堆在一轮 |
| `deb_track_ask` | `true`（默认） | **新增应用/内核组件 + 要编 DEBIAN/UBUNTU** 时，必须问用户是否登记到 `layers/meta-quectel/conf/deb-track/`，不得自己默认加或默默跳过 |

注意：这些是 `AGENTS.md` 硬规则的**补充**，不是替代。即使 `can_flash_freely=yes`，编译仍然由用户执行。

### 4.5 deb-track 登记询问（新增组件专用）

`deb_track_ask=true`（默认）时，以下两个条件**同时**成立就要问：

1. 本次新增了应用或内核组件（新 recipe / 新 `IMAGE_INSTALL` / 新 .ko / 新固件 / 新 BSP 用户态包）；
2. 接下来要编 **DEBIAN 或 UBUNTU** 固件。

问法（结论先行，带影响）：

> 这个新组件要不要登记到 `conf/deb-track/`？登记后 apt 数据库保留其信息，
> 设备上 `dpkg -S` 能查到、也能从 apt 源升级；不登记文件仍在盘上但 apt 不识，
> 以后只能重烧镜像更新。

**为什么不自己决定**：登记会改变 apt 数据库语义（该文件从此归属某包，升级/
降级会动它），属"跨人、不可逆倾向"的决定，按 §5 归用户拍板。

**不用问的例外**：编 `LINUX`（Yocto 原生，deb-track 自动跳过）；只改已有组件；
用户已明确表态过。映射到哪个文件、安全边界见 `deb-track-SKILL.md`。

## 5. "不要问"清单

`ask_technical_route=false`（默认）时，**禁止问**：
- "用哪套驱动模型 / 走 A 方案还是 B 方案"
- "要不要开某个 CONFIG / 改哪个 DTS 节点 / 换哪个 compatible"
- "烧哪个镜像、编哪个包、要不要 clean"
- "这段代码你想怎么改"

**改成这样说**："我按 X 做，因为它避免了 Y（一句话理由）。影响是：…。要改成 Z 你说一声。"

**允许问的只有**：目标与验收标准、时间与代价、风险偏好、现场条件、不可逆/跨人操作。

## 6. mentor 模式下怎么让用户真有参与感

不是"多说技术名词"，而是**给用户可自己动手的判定点**：

1. 提假设 → 给一条能区分假设的命令（adb / dmesg grep / 寄存器 dump / ftrace / 打点）。
2. 先定判据："输出是 X 说明假设成立，是 Y 说明我错了"，再看数据。
3. 请用户自己跑、自己贴回；拿到数据后再一起下结论，不抢答。
4. 寄存器/时序类：把地址、位域、预期值列出来，让用户能自己核对。
5. 允许用户推翻你的判断；被推翻就先收回结论，别辩解。
6. 把稳定复现路径交回用户："这条命令你留着，下次复发直接跑。"

同时守 `AGENTS.md`：**没有数据不下结论**。mentor 不是"多聊"，是"多拿证据"。

## 7. 画像校准与演化

| 信号（连续 2 次） | 含义 | 动作 |
|---|---|---|
| 用户能直接接住栈回溯/寄存器讨论 | 该域被低估 | 建议上调 |
| 同一概念解释 3 次仍卡住 | 该域被高估 | 下调，改 guided/hand-off |
| 连续 3 次只问"改完了吗" | 偏好 terse / hand-off | 下调 verbosity |
| 用户主动要求"讲细一点/为什么" | 想更深入 | 上调参与度 |
| 两轮内解决且用户自行验证 | 可交更多自主权 | 提议 autonomy 上调 |
| 用户明说"讲详细点 / 展开说" | 偏好变了（不是能力） | 立即改行为 + 写 `preferences.verbosity=detailed` |
| 用户明说"少废话 / 直接给结果" | 偏好变了 | 立即改行为 + 写 `preferences.verbosity=terse` |
| 用户明说"术语直接用 / 别解释了" | 偏好变了 | 写 `preferences.jargon=free` |
| 用户明说"以后直接做完再告我" | 偏好变了 | 写 `preferences.autonomy=auto` |

**偏好类调整不用先征得同意**：用户直接提要求（讲详细点 / 别问技术路线 / 以后自己定）时，
当场改行为，并用 `profile-set.sh` 落盘，回复里一句复述即可：

> 已记下：以后这类都按 detailed 展开（`verbosity=detailed` 已写入画像）。

**能力等级调整要先提议**（每会话最多 1 次），因为那是对人的判断，不能自作主张。

规则：
- 先**改行为**（本会话内直接按观察调整），再**提议写文件**；每会话最多提议 1 次：
  > 顺带一提，你刚才自己定位到了那个 IRQ 冲突，要不要我把 `levels.linux_kernel` 从 1 提到 2，以后直接跟你对栈？
- 用户说"改画像/重新校准/我换方向了" → 只问变化的那几项，用 `profile-set.sh` 更新，其余保留。
- 用户说"忘掉画像" → 删除 `setting.inf`，回到第 3 节首次引导（先确认一次再删）。

## 8. 常见操作

```bash
# 启动检查（agent 每次会话第一步）
sh quectel_build/skills/profile-check.sh

# 用户要求“讲详细点 / 别解释术语 / 以后自己定” → 当场落盘
sh quectel_build/skills/profile-set.sh preferences.verbosity=detailed preferences.jargon=free
# [derived] 会自动刷新，不用手动维护

# 引导中：每答一题落盘
sh quectel_build/skills/profile-set.sh levels.linux_kernel=3 meta.state=in-progress meta.answers=2/12

# 批量写入（一行一个，值可带空格引号）
sh quectel_build/skills/profile-set.sh - <<'EOF'
role.primary=system
levels.yocto=3
levels.linux_kernel=1
preferences.autonomy=big-direction
meta.state=complete
EOF

# 局部例外：显示自己搞过，内核整体不懂
sh quectel_build/skills/profile-set.sh topics.display=2

# 用户说别问了先干活
sh quectel_build/skills/profile-set.sh meta.state=quick-start meta.confidence=low

# 用户说忘掉画像
rm -f quectel_build/skills/setting.inf

# 手工速览（给人看，不带执行约束段）
sh quectel_build/skills/profile-check.sh --quiet
```

## 9. `setting.inf` 规范

- 编码 UTF-8，LF 换行；`;` 或 `#` 开头为注释；`key = value`，自由文本可加双引号。
- 段名固定：`[meta] [role] [levels] [topics] [preferences] [constraints] [derived]`。
- **缺失的 key 一律取默认值**（默认 = 中等 / guided / big-direction），不要因为缺字段重新引导。
- `[levels]` 取值 0-3：`0` 不了解 / `1` 能读·懂概念 / `2` 能独立改 / `3` 能设计·带人。
- `[topics]` 只写与 `[levels]` 推断**不同**的域，用于局部覆盖。
- `[derived]` 是缓存，`profile-check.sh` 不读它（实时计算）。`profile-set.sh` 每次写入后会用
  `profile-check.sh` 的 `derived:` 行**自动整段重建**（单一真相在判定表，不手改）。
  已有值可作为手工速览，但**以脚本实时计算为准**。

| 字段 | 允许值 | 默认 |
|---|---|---|
| `[role] primary` | bsp / system / app / algo / pm / eval | system |
| `[preferences] autonomy` | manual / big-direction / auto | big-direction |
| `[preferences] participation` | mentor / guided / handoff / auto | auto |
| `[preferences] verbosity` | terse / normal / detailed | normal |
| `[preferences] jargon` | free / explain / plain | explain |
| `[preferences] report_format` | conclusion-first / full-process / result-next | conclusion-first |
| `[preferences] debug_style` | hands-on / guided / handoff | guided |
| `[preferences] ask_technical_route` | true / false | false |
| `[preferences] review_diff` | true / false | true |
| `[meta] state` | complete / quick-start / in-progress | in-progress |
| `[meta] confidence` | low / medium / high | low |

## 10. 让其它 agent 也遵守（无需改 agent）

本 skill 刻意用**纯 shell + 明文 ini**，所以任何 agent 只要做到两件事就兼容：

1. **能读 `AGENTS.md`**（或 `.hermes.md` / `CLAUDE.md` 等符号链接指向的同一份）；
2. **能执行 `sh`** 跑 `profile-check.sh`。

各 agent 的接入方式（都是**已有文件的指针**，不加插件、不改 agent 本体）：

| Agent | 接入 | 说明 |
|---|---|---|
| pi | `AGENTS.md` + `.pi/skills/user-profile/SKILL.md`（软链） | 已就位 |
| Hermes | `.hermes.md`（`AGENTS.md` 软链指向它） | 已就位 |
| Claude Code | `ln -s .hermes.md CLAUDE.md`（可选） | 一行符号链接 |
| Copilot / Cursor | `.github/copilot-instructions.md` 或 `.cursor/rules` 里加一句"先跑 profile-check.sh" | 一行文字 |
| 自研脚本 / CI | 直接跑脚本，看退出码 0/2/3 分支 | 无依赖 |

要点：**不要在 `setting.inf` 里写散文**，它要被脚本解析；
复杂语义放 skill 文档，可执行部分放脚本。

## 11. 反模式

- ❌ 每轮回复都复述画像 / 打印整份画像。
- ❌ 为了"收集完整画像"把工作卡住，连问 12 题不让用户干活。
- ❌ 用"你不懂这个"简化到失真，或隐瞒风险——**降低技术密度 ≠ 隐瞒影响**。
  对新手必须说清"会不会变砖 / 要多久 / 怎么退"。
- ❌ 拿画像替用户做**产品决定**（要不要带这个功能、验收标准是什么），那必须问。
- ❌ 拿画像给自己开后门跳过 `AGENTS.md` 硬规则。
- ❌ hand-off 域里偷偷问技术路线；mentor 域里替用户把结论拍死不让讨论。
- ❌ 画像里写主观评价（"用户水平差"），只记录**可操作的能力等级与偏好**。
- ❌ 手写 `setting.inf`（易重复 key/丢注释）。用 `profile-set.sh`。
