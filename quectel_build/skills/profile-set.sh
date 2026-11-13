#!/bin/sh
# ============================================================================
# profile-set.sh — 写入/更新用户能力画像（跨 agent，纯 POSIX sh + awk）
#
# 为什么要有它：引导提问时让模型"手拼 ini"很容易写错格式（丢 section、
# 重复 key、注释吃掉值）。这里只接受 key=value，格式与去重由脚本保证。
#
# 用法（任何 agent 都能直接调，不需要插件、不需要配置）：
#   sh quectel_build/skills/profile-set.sh levels.linux_kernel=2 \
#        role.primary=bsp preferences.autonomy=big-direction meta.state=complete
#
#   # 值里有空格/中文时用引号（脚本内部按行处理，不会拆词）：
#   sh quectel_build/skills/profile-set.sh 'role.target=DSI 屏点亮 + 触摸可用'
#
#   # 批量从标准输入（一行一个）：
#   sh quectel_build/skills/profile-set.sh - <<'EOF'
#   levels.c_cpp=3
#   role.target="四路摄像头"
#   EOF
#
# 规则：
#   - 参数形式 section.key=value；点号前是 section，点号后是 key
#   - 文件不存在时自动从 setting.inf.example 拷贝（保留注释与顺序）
#   - 重复写同一 key 只替换，不产生第二份
#   - meta.updated_at 自动刷新
#   - section 名写错直接报错退出，防手滑
# ============================================================================
set -u

SELF_DIR=$(cd "$(dirname "$0")" && pwd)
INF="$SELF_DIR/setting.inf"
TPL="$SELF_DIR/setting.inf.example"

VALID_SECTIONS="meta role levels topics preferences constraints derived"

die() { echo "profile-set: $1" >&2; exit 1; }

# ---- 收集参数到临时文件（按行保存，避免空格/中文被拆分） -------------------
PAIR_FILE=$(mktemp "${TMPDIR:-/tmp}/profile-pairs.XXXXXX") || die "mktemp 失败"
TMP=$(mktemp "${TMPDIR:-/tmp}/profile-inf.XXXXXX") || die "mktemp 失败"
trap 'rm -f "$PAIR_FILE" "$TMP"' EXIT INT TERM

if [ "${1:-}" = "-" ]; then
    # stdin：一行一个 section.key=value
    while IFS= read -r line || [ -n "$line" ]; do
        [ -z "$(printf '%s' "$line" | tr -d ' \t\r')" ] && continue
        printf '%s\n' "$line" >> "$PAIR_FILE"
    done
else
    [ $# -eq 0 ] && die "用法: sh $0 section.key=value ...  或  ... | sh $0 -"
    for a in "$@"; do printf '%s\n' "$a" >> "$PAIR_FILE"; done
fi

[ -s "$PAIR_FILE" ] || die "没有收到任何 key=value"

# ---- 校验 section / key ----------------------------------------------------
while IFS= read -r p; do
    case "$p" in
        *=*) ;;
        *) die "参数格式应为 section.key=value，收到: $p" ;;
    esac
    lhs=${p%%=*}
    case "$lhs" in
        *.*) ;;
        *) die "参数缺少 section（应为 section.key=value），收到: $p" ;;
    esac
    sec=${lhs%%.*}
    key=${lhs#*.}
    [ -n "$key" ] || die "参数缺少 key，收到: $p"
    ok=0
    for s in $VALID_SECTIONS; do [ "$sec" = "$s" ] && ok=1; done
    [ "$ok" = 1 ] || die "未知 section: $sec（允许: $VALID_SECTIONS）"
done < "$PAIR_FILE"

# ---- 准备目标文件 ----------------------------------------------------------
if [ ! -f "$INF" ]; then
    if [ -f "$TPL" ]; then
        cp "$TPL" "$INF"
        echo "profile-set: 已从 setting.inf.example 创建 $INF"
    else
        printf '; setting.inf — 用户能力画像\n[meta]\nschema = 1\n' > "$INF"
        echo "profile-set: 已新建 $INF"
    fi
fi

printf 'meta.updated_at=%s\n' "$(date +%Y-%m-%d)" >> "$PAIR_FILE"

# ---- 逐条 upsert，输出到临时文件再原子替换 --------------------------------
awk -v INF="$INF" '
function trim(s) { gsub(/^[ \t\r]+|[ \t\r]+$/, "", s); return s }

# 读更新清单
{
    line = $0
    if (trim(line) == "") next
    eq = index(line, "=")
    if (eq == 0) next
    lhs = trim(substr(line, 1, eq - 1))
    val = trim(substr(line, eq + 1))
    dot = index(lhs, ".")
    if (dot == 0) next
    sec = substr(lhs, 1, dot - 1)
    key = substr(lhs, dot + 1)
    if (key == "") next
    if (!((sec, key) in U)) ORDER[++norder] = sec "." key
    U[sec, key] = val
}
END {
    while ((getline l < INF) > 0) L[++n] = l
    close(INF)

    for (o = 1; o <= norder; o++) {
        spec = ORDER[o]
        dot = index(spec, ".")
        sec = substr(spec, 1, dot - 1)
        key = substr(spec, dot + 1)
        val = U[sec, key]

        start = 0; endidx = n + 1
        for (i = 1; i <= n; i++) {
            t = trim(L[i])
            if (t ~ /^\[[^]]+\]$/) {
                s = t; sub(/^\[/, "", s); sub(/\].*/, "", s); s = trim(s)
                if (start > 0) { endidx = i; break }
                if (s == sec) start = i
            }
        }

        if (start == 0) {
            L[++n] = ""
            L[++n] = "[" sec "]"
            L[++n] = key " = " val
            continue
        }

        found = 0
        for (i = start + 1; i < endidx; i++) {
            t = trim(L[i])
            if (t ~ /^(;|#)/ || t == "") continue
            eq = index(t, "=")
            if (eq == 0) continue
            k = trim(substr(t, 1, eq - 1))
            if (k == key) { L[i] = key " = " val; found = 1; break }
        }
        if (!found) {
            ins = start
            for (i = start + 1; i < endidx; i++) if (trim(L[i]) != "") ins = i
            for (i = n + 1; i > ins + 1; i--) L[i] = L[i - 1]
            L[ins + 1] = key " = " val
            n++
        }
    }

    for (i = 1; i <= n; i++) print L[i]
}
' "$PAIR_FILE" > "$TMP" || die "生成失败"

mv "$TMP" "$INF" || die "写入失败: $INF"
trap - EXIT INT TERM
rm -f "$PAIR_FILE"

# ---- 刷新 [derived] 缓存 ---------------------------------------------------
# 规则只有 profile-check.sh 一份（single source of truth），这里只把它的
# derived: 行回写进 ini，不重新实现一遍判定表。
refresh_derived() {
    [ -f "$INF" ] || return 0
    DLINE=$(sh "$SELF_DIR/profile-check.sh" --quiet 2>/dev/null | awk '/^derived: /{sub(/^derived: /,""); print; exit}')
    [ -n "$DLINE" ] || return 0

    DERIVED_TMP=$(mktemp "${TMPDIR:-/tmp}/profile-derived.XXXXXX") || return 0
    printf '%s\n' "$DLINE" | tr ' ' '\n' | awk -F= '
        function trim(s) { gsub(/^[ \t\r]+|[ \t\r]+$/, "", s); return s }
        $1 != "" && NF >= 2 { print trim($1) " = " trim($2) }
    ' > "$DERIVED_TMP"
    [ -s "$DERIVED_TMP" ] || { rm -f "$DERIVED_TMP"; return 0; }

    awk -v SRC="$DERIVED_TMP" '
    function trim(s) { gsub(/^[ \t\r]+|[ \t\r]+$/, "", s); return s }
    BEGIN {
        while ((getline l < SRC) > 0) {
            if (trim(l) == "") continue
            eq = index(l, "=")
            if (eq == 0) continue
            k = trim(substr(l, 1, eq - 1))
            v = trim(substr(l, eq + 1))
            if (k == "") continue
            if (!(k in D)) ORDER[++nd] = k
            D[k] = v
        }
        close(SRC)
    }
    { L[++n] = $0 }
    END {
        # 找 [derived] 段位置
        start = 0; endidx = n + 1
        for (i = 1; i <= n; i++) {
            t = trim(L[i])
            if (t ~ /^\[[^]]+\]$/) {
                s = t; sub(/^\[/, "", s); sub(/\].*/, "", s); s = trim(s)
                if (start > 0) { endidx = i; break }
                if (s == "derived") start = i
            }
        }

        if (start == 0) {
            L[++n] = ""
            L[++n] = "[derived]"
            L[++n] = "; 本段为缓存，由 user-profile-SKILL.md 第 4 节判定表算出（每次 profile-set.sh 自动刷新，勿手改）"
            for (o = 1; o <= nd; o++) L[++n] = ORDER[o] " = " D[ORDER[o]]
        } else {
            # 纯缓存段：整段重建，保持固定顺序（注释保留一行）
            out_n = 0
            for (i = 1; i <= start; i++) { OUT[++out_n] = L[i] }
            OUT[++out_n] = "; 本段为缓存，由 user-profile-SKILL.md 第 4 节判定表算出（每次 profile-set.sh 自动刷新，勿手改）"
            for (o = 1; o <= nd; o++) OUT[++out_n] = ORDER[o] " = " D[ORDER[o]]
            for (i = endidx; i <= n; i++) { OUT[++out_n] = L[i] }
            for (i = 1; i <= out_n; i++) L[i] = OUT[i]
            n = out_n
        }

        for (i = 1; i <= n; i++) print L[i]
    }
    ' "$INF" > "$TMP" 2>/dev/null && mv "$TMP" "$INF"
    rm -f "$DERIVED_TMP"
}

# 只有画像 ready 时才有可信的 derived（未完成时 refresh_derived 内部自动跳过）
if sh "$SELF_DIR/profile-check.sh" --quiet 2>/dev/null | grep -q '^PROFILE_READY'; then
    refresh_derived
fi
trap - EXIT INT TERM
rm -f "$PAIR_FILE"

echo "profile-set: 已更新 $INF"
echo "--- 当前状态 ---"
sh "$SELF_DIR/profile-check.sh" | grep -E "^(PROFILE|state|progress|modes|handoff_|NEXT)"
