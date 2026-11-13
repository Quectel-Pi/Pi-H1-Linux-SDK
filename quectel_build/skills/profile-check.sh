#!/bin/sh
# ============================================================================
# profile-check.sh — 用户能力画像检查器（跨 agent，纯 POSIX sh + awk）
#
# 目的：把"画像 → 协作模式"的解释逻辑放在脚本里，而不是放进每个 agent 的
#       插件/配置。任何 agent（pi / Hermes / Claude Code / Copilot / Cursor /
#       自研脚本 / 人）只要能执行它，就拿到同一份可执行约束。
#
# 用法：
#   sh quectel_build/skills/profile-check.sh          # 人看 / agent 读
#   sh quectel_build/skills/profile-check.sh --quiet  # 只输出结论块
#
# 退出码（agent 用它做分支判断，不要靠猜）：
#   0  画像就绪（state=complete 或 quick-start）→ 可以开始实际工作
#   2  画像缺失 → 必须先引导提问，不得开始工作
#   3  画像未完成（state 非 complete/quick-start）→ 继续引导提问
#
# 产物：quectel_build/skills/setting.inf（格式见 setting.inf.example）
# 规范：quectel_build/skills/user-profile-SKILL.md
# ============================================================================
set -u

SELF_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$SELF_DIR/../.." && pwd)
INF="$ROOT/quectel_build/skills/setting.inf"
QUIET=0
[ "${1:-}" = "--quiet" ] && QUIET=1

# --quiet 时只输出结论块，不打印空行/执行约束
export PROFILE_QUIET="$QUIET"

# ---------------------------------------------------------------------------
# 缺失：直接给结论，不打印任何别的
# ---------------------------------------------------------------------------
if [ ! -f "$INF" ]; then
    [ "$QUIET" = 0 ] && echo "== user-profile =="
    echo "PROFILE_MISSING"
    echo "path: $INF"
    echo "NEXT: 引导提问（题库见 quectel_build/skills/user-profile-SKILL.md 第 2.2 节，当前 0/12）"
    exit 2
fi

# ---------------------------------------------------------------------------
# 解析 + 计算模式，全部在 awk 里，避免 shell 侧到处 eval
# ---------------------------------------------------------------------------
awk -v quiet="$QUIET" -v inf="$INF" '
function trim(s) { gsub(/^[ \t\r]+|[ \t\r]+$/, "", s); return s }

# ---- 1. 解析 ini -----------------------------------------------------------
{
    line = $0
    if (line ~ /^[ \t\r]*$/) next
    if (line ~ /^[ \t]*(;|#)/) next
    if (line ~ /^[ \t]*\[[^]]+\]/) {
        sec = line
        sub(/^[ \t]*\[/, "", sec)
        sub(/\].*/, "", sec)
        sec = trim(sec)
        next
    }
    eq = index(line, "=")
    if (eq == 0) next
    k = trim(substr(line, 1, eq - 1))
    val = trim(substr(line, eq + 1))
    sub(/[ \t]*;.*/, "", val)          # 行内注释
    sub(/[ \t]*#.*/, "", val)
    val = trim(val)
    gsub(/^"|"$/, "", val)
    if (k != "") V[sec SUBSEP k] = val
}

END {
    # ---- 2. 状态 ----------------------------------------------------------
    state = V["meta" SUBSEP "state"]
    if (state == "") state = "complete"          # 有文件但没写 state，视为可用
    progress = V["meta" SUBSEP "answers"]; if (progress == "") progress = "?"
    confidence = V["meta" SUBSEP "confidence"]; if (confidence == "") confidence = "?"

    ready = (state == "complete" || state == "quick-start")

    # ---- 3. 能力等级（默认 1 = 能读/懂概念） ------------------------------
    split("linux_kernel device_tree boot_chain c_cpp python shell_debug yocto debian_pkg hw_schematic lab_equipment gpu_npu audio camera networking", keys, " ")
    for (i in keys) {
        k = keys[i]
        raw = V["levels" SUBSEP k]
        L[k] = 1
        if (raw != "" && raw + 0 == raw) {
            n = raw + 0
            if (n < 0) n = 0
            if (n > 3) n = 3
            L[k] = n
        }
    }

    # ---- 4. 偏好 ----------------------------------------------------------
    part = V["preferences" SUBSEP "participation"]; if (part == "") part = "auto"
    auto = V["preferences" SUBSEP "autonomy"];     if (auto == "") auto = "big-direction"
    verb = V["preferences" SUBSEP "verbosity"];    if (verb == "") verb = "normal"
    jarg = V["preferences" SUBSEP "jargon"];       if (jarg == "") jarg = "explain"
    rep  = V["preferences" SUBSEP "report_format"];if (rep  == "") rep  = "conclusion-first"
    dbg  = V["preferences" SUBSEP "debug_style"];  if (dbg  == "") dbg  = "guided"
    askr = V["preferences" SUBSEP "ask_technical_route"]; if (askr == "") askr = "false"
    rev  = V["preferences" SUBSEP "review_diff"];  if (rev  == "") rev  = "true"
    serial = V["constraints" SUBSEP "serial_console"]; if (serial == "") serial = "unknown"
    cboot  = V["constraints" SUBSEP "can_reboot_freely"];  if (cboot  == "") cboot  = "yes"
    cflash = V["constraints" SUBSEP "can_flash_freely"];   if (cflash == "") cflash = "yes"
    offl   = V["constraints" SUBSEP "offline"];           if (offl   == "") offl   = "no"
    boards = V["constraints" SUBSEP "board_count"];       if (boards == "") boards = "1"
    sess   = V["constraints" SUBSEP "session_minutes"];   if (sess   == "") sess   = ""
    dtask  = V["constraints" SUBSEP "deb_track_ask"];     if (dtask  == "") dtask  = "true"

    role = V["role" SUBSEP "primary"]; if (role == "") role = "system"
    target = V["role" SUBSEP "target"]

    # ---- 5. 域 → 协作模式 -------------------------------------------------
    # 每个域取构成它的能力项里最低者（最坏环节决定沟通方式）
    # 注意：没有专属能力项的域（display/camera/audio）用 device_tree 作下限；
    #       想单独调整就写 [topics] 覆盖。
    n = split("kernel:linux_kernel driver:linux_kernel+device_tree boot:boot_chain+device_tree build:yocto+debian_pkg deploy:shell_debug+yocto app:c_cpp+python debug:shell_debug hw:hw_schematic+lab_equipment display:device_tree camera:device_tree audio:device_tree gpu_npu:gpu_npu network:networking storage:shell_debug modem:shell_debug git:shell_debug", defs, " ")

    mode_str = ""; mentor = ""; handoff = ""; guided = ""
    for (i = 1; i <= n; i++) {
        split(defs[i], pair, ":")
        dom = pair[1]
        srcs = pair[2]

        # [topics] 显式覆盖优先
        ov = V["topics" SUBSEP dom]
        if (ov != "" && ov + 0 == ov) {
            level = ov + 0
            if (level < 0) level = 0
            if (level > 3) level = 3
        } else {
            m = split(srcs, sk, "+")
            level = 3
            for (j = 1; j <= m; j++) if (L[sk[j]] < level) level = L[sk[j]]
        }

        if (part == "handoff" || part == "guided" || part == "mentor") mode = part
        else if (level >= 2) mode = "mentor"
        else if (level == 1) mode = "guided"
        else mode = "handoff"

        mode_str = mode_str (mode_str == "" ? "" : " ") dom "=" mode
        Mode[dom] = mode
        cnt[mode]++
        if (mode == "mentor")  mentor  = mentor  (mentor  == "" ? "" : "/") dom
        if (mode == "guided")  guided  = guided  (guided  == "" ? "" : "/") dom
        if (mode == "handoff") handoff = handoff (handoff == "" ? "" : "/") dom
    }

    # ---- 6. 输出 ----------------------------------------------------------
    # overall_mode：取最多的那个模式；平票时保守优先 handoff > guided > mentor
    Overall = "guided"
    if (cnt["handoff"] >= cnt["guided"] && cnt["handoff"] >= cnt["mentor"]) Overall = "handoff"
    else if (cnt["mentor"] > cnt["guided"]) Overall = "mentor"

    if (ready) print "PROFILE_READY"
    else       print "PROFILE_NOT_READY"

    # 机器可读段：profile-set.sh 用它刷新 [derived] 缓存
    print "derived: kernel_mode=" Mode["kernel"] " driver_mode=" Mode["driver"] \
          " boot_mode=" Mode["boot"] " build_mode=" Mode["build"] \
          " deploy_mode=" Mode["deploy"] " app_mode=" Mode["app"] \
          " debug_mode=" Mode["debug"] " hw_mode=" Mode["hw"] \
          " display_mode=" Mode["display"] " camera_mode=" Mode["camera"] \
          " audio_mode=" Mode["audio"] " gpu_npu_mode=" Mode["gpu_npu"] \
          " network_mode=" Mode["network"] " storage_mode=" Mode["storage"] \
          " modem_mode=" Mode["modem"] " git_mode=" Mode["git"] \
          " overall_mode=" Overall

    print "path: " inf
    print "state: " state
    print "progress: " progress
    print "confidence: " confidence
    print "role: " role
    if (target != "") print "target: " target
    print "modes: " mode_str
    print "mentor_domains: " (mentor  == "" ? "-" : mentor)
    print "guided_domains: " (guided  == "" ? "-" : guided)
    print "handoff_domains: " (handoff == "" ? "-" : handoff)
    print "autonomy: " auto
    print "participation: " part
    print "verbosity: " verb
    print "jargon: " jarg
    print "report_format: " rep
    print "debug_style: " dbg
    print "ask_technical_route: " askr
    print "review_diff: " rev
    print "serial_console: " serial
    print "can_reboot_freely: " cboot
    print "can_flash_freely: " cflash
    print "offline: " offl
    print "board_count: " boards
    print "deb_track_ask: " dtask

    if (!ready) {
        print "NEXT: 引导提问（题库见 quectel_build/skills/user-profile-SKILL.md 第 2.2 节，当前 " progress "）"
        exit 3
    }

    # ---- 7. 给 agent 的可执行约束（照做即可，不需要自己解释画像） ---------
    if (quiet == 0) {
        print ""
        print "== 执行约束（照做，不要自行解释画像）=="
        if (askr == "false")
            print "- 禁止询问技术路线：不问\"用哪套驱动/开哪个 CONFIG/改哪个节点/烧哪个镜像/代码怎么改\"。"
        else
            print "- 允许询问技术路线，但仍应给出推荐方案。"
        if (handoff != "")
            print "- 这些域完全代劳（handoff）：" handoff " → 内部决策，输出【结论 / 我要做的 / 预期影响 / 风险与回退 / 需要你做的】五段式。"
        if (guided != "")
            print "- 这些域带路（guided）：" guided " → 1 个推荐 + 1 个备选 + 步骤/影响/耗时，概念一句话解释。"
        if (mentor != "")
            print "- 这些域陪练（mentor）：" mentor " → 先摊证据（dmesg/寄存器/栈回溯/代码行号），提 2~3 个互斥假设 + 一条能区分的命令，让用户自己跑、自己贴回、一起下结论。"
        print "- 允许问的只有：目标与验收标准、时间代价、风险偏好、现场条件、不可逆或跨人的操作。"
        print "- autonomy=" auto "：manual=每步先确认 / big-direction=方案定了就做，烧板·改公共配置·动 git 前确认 / auto=自己走完再报（编译仍由用户执行）。"
        if (serial == "no")
            print "- serial_console=no：不要给\"接串口看 log\"的方案，改用 adb / dmesg / 网络。"
        if (serial == "unknown")
            print "- serial_console=unknown：需要串口时先问一句有没有串口线，不要假定。"
        if (offl == "yes")
            print "- offline=yes：这块环境不联网。禁止给 apt install / pip install / git pull / curl 下载 这类需要联网的方案；只能用板子或本地已有的东西。"
        if (cflash == "no")
            print "- can_flash_freely=no：不能随便烧板。任何需要重新刷机/重烧镜像的方案，先说明会抹掉什么、有没有不烧的替代路径，得到确认再做。"
        if (cboot == "no")
            print "- can_reboot_freely=no：不能随便重启（会影响现场/他人/长任务）。需要重启的方案先标注代价并确认。"
        if (boards != "" && boards + 0 == boards && boards + 0 <= 1)
            print "- board_count=" boards "：只有一块板。凡是会让板子不可用（变砖/无法回退）或需要长时间占用板子的操作，必须先确认。"
        if (dtask == "true")
            print "- deb_track_ask=true：新增应用/内核组件且要编 DEBIAN/UBUNTU 时，必须问用户是否登记到 layers/meta-quectel/conf/deb-track/（登记后 apt 数据库保留其信息）；不要自己默认加或默默跳过。"
        if (sess != "" && sess + 0 == sess && sess + 0 > 0)
            print "- session_minutes=" sess "：单次会话时间有限，方案要能拆成 " sess " 分钟内可交付的步骤，不要把长任务堆在一轮。"
        print "- 画像只约束协作方式：不改技术结论，不松绑 AGENTS.md 硬规则（编译由用户跑、git 自检、先拿数据再下结论）。"
        print "- 不要每轮复述画像。"
    }
    exit 0
}
' "$INF"
