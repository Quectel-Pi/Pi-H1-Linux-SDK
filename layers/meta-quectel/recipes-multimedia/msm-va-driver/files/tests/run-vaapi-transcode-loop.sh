#!/usr/bin/env bash
# Regression check for the decoder -> encoder surface handoff.
#
# The encoder used to fail with "Encode failed: -5" when ffmpeg handed it a
# decoded surface whose picture was still in the decoder's reorder buffer. The
# driver now waits for that picture instead of rejecting the surface. The bug
# stopped a random run at a random frame (9, 14, 20, 44, 74, 289... depending on
# the run), so a single run proves nothing: loop, and require every run to
# deliver every frame with no rejected surface.
#
#   ./run-vaapi-transcode-loop.sh <input.mp4> [runs]
set -Eeuo pipefail

SRC="${1:?usage: run-vaapi-transcode-loop.sh <input.mp4> [runs]}"
RUNS="${2:-5}"
DRM="${DRM:-/dev/dri/renderD128}"
CODEC="${CODEC:-hevc_vaapi}"
: "${LIBVA_DRIVER_NAME:=msm}"
export LIBVA_DRIVER_NAME
export VENUS_VAAPI_LOG=1

for command in ffmpeg ffprobe; do
    command -v "${command}" >/dev/null 2>&1 || {
        echo "缺少命令: ${command}" >&2
        exit 1
    }
done
[[ -e "${SRC}" ]] || { echo "找不到输入文件: ${SRC}" >&2; exit 1; }
[[ -c "${DRM}" ]] || { echo "找不到 DRM 节点: ${DRM}" >&2; exit 1; }

SRC_FRAMES="$(ffprobe -v error -count_frames -select_streams v:0 \
    -show_entries stream=nb_read_frames -of default=nw=1:nk=1 "${SRC}")"
OUT="$(mktemp -d /var/tmp/venus-vaapi-transcode.XXXXXX)"
echo "输入：${SRC}（${SRC_FRAMES} 帧） 编码：${CODEC} 运行次数：${RUNS}"
echo "日志目录：${OUT}"

FAILED=0
for i in $(seq 1 "${RUNS}"); do
    set +e
    ffmpeg -hide_banner -loglevel verbose -y -nostdin \
        -hwaccel vaapi -hwaccel_device "${DRM}" \
        -hwaccel_output_format vaapi \
        -i "${SRC}" -c:v "${CODEC}" -b:v 4M \
        "${OUT}/run${i}.mp4" >"${OUT}/run${i}.log" 2>&1
    RC=$?
    set -e

    FRAMES="$(ffprobe -v error -count_frames -select_streams v:0 \
        -show_entries stream=nb_read_frames -of default=nw=1:nk=1 \
        "${OUT}/run${i}.mp4" 2>/dev/null || echo 0)"
    WAITS="$(grep -ac 'begin-picture wait' "${OUT}/run${i}.log" || true)"
    REJECTS="$(grep -ac 'begin-picture REJECT' "${OUT}/run${i}.log" || true)"
    echo "第 ${i} 次：rc=${RC} 帧数=${FRAMES}/${SRC_FRAMES} 等待=${WAITS} 拒绝=${REJECTS}"

    if (( RC != 0 )) || [[ "${FRAMES}" != "${SRC_FRAMES}" ]] || (( REJECTS != 0 )); then
        FAILED=1
        grep -aE 'Encode failed|Failed to (begin|end) picture|Error encoding' \
            "${OUT}/run${i}.log" | tail -n 5 || true
    fi
done

if (( FAILED == 0 )); then
    echo "PASS：${RUNS} 次转码均完整输出 ${SRC_FRAMES} 帧，无 surface 被拒"
else
    echo "FAIL"
fi
echo "日志目录：${OUT}"
exit "${FAILED}"
