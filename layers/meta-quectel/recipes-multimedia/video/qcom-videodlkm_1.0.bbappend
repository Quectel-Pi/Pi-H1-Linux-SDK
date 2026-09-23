FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# 客户端在流还在跑的时候退出就会留下一个处于 CLOSE(4) 或 ERROR(5) 的会话：
# 固件清不掉它（日志 `Memory leak found`），而负载统计依旧把它算进去。真正的
# 闸门是 msm_vidc_check_core_mbpf（不是会话计数那道）：4K 一帧 = 240 x 135 =
# 32400 宏块，MAX_MBPF = 77522 只够 2 个 4K 负载，所以两个这种会话之后，之后
# 每个 4K 会话都被 -ENOMEM 拒绝：
#
#   msm_vidc_check_core_mbpf: video overloaded. needed 97200, max 77522
#   msm_vidc_streamon: vb2_streamon(10) failed, -12
#   msm_v4l2_reqbufs: inst in error state
#
# 触发条件不是"只有被强杀"，**播放中关窗口（SIGTERM）一样漏**——播放器收到
# SIGTERM 就退，不做流级收尾。所以这是日常操作，不是极端情况：
#   自然播完(EOS) → 实例 0        不漏
#   关窗口(SIGTERM) → 实例 1 CLOSE 漏
#   kill -9         → 实例 1 CLOSE 漏
#
# 实测（干净启动，hwdec=vaapi，4K H.264，播放中取样，判词来自客户端自己）：
#   死会话 0 → 1 个解码实例 → mpv: Using hardware decoding (vaapi)
#   死会话 1 → 2 个解码实例 → mpv: Using hardware decoding (vaapi)
#   死会话 2 → 3 个解码实例 → mpv 无硬解字样，回落软解，
#                              "Failed to create decode context: 2 (resource allocation failed)"
# +15/+30/+60/+120s 重试全部被拒，且每次被拒又留下一个新的 ERROR 会话。
#
# 恢复手段：unbind/rebind 能把死会话清干净，额度是真还回来（实测 2 → 0 之后
# 4K 恢复硬解），不必重启整机。**但必须先停掉播放器、确认没有客户端持有 VPU**
# ——播放中做 unbind 会当场把板子打挂（硬复位）。
#
# 治本：泄漏是可以避免的，关闭路径上有两处独立缺陷，补丁各治一处。固件要的是
# "按序收摊"：
#
# ① 会话关闭时队列还在 streaming。msm_vidc_close() 直接把 HFI_CMD_SESSION_CLOSE
#    发给固件，此时两个队列可能都还在 streaming；而停队列只发生在
#    msm_vidc_close_helper() 的队列释放里，那时固件会话已经没了，太晚。客户端自己
#    先做 streamoff 就不会漏——这正是"自然播完(EOS)干净 / 播放中退出不干净"的原因。
#    补丁在 session_close 之前先停两个队列（先输出/帧，后输入/码流，与队列释放同
#    序），已经停过队列的客户端不付代价（vb2_is_streaming() 为假）。
#
# ② 队列释放发生在引用计数归零之后（msm_vidc_close_helper()），而 buffer 自己持有
#    inst 的引用：msm_vb2_alloc() 拿引用、msm_vb2_put() 放引用，msm_vb2_put() 又只在
#    队列释放时跑——一个引用环。客户端没做 VIDIOC_REQBUFS(0) 就被杀（上面的日常操
#    作就是）→ 实例永远留在 core->instances 的 CLOSE 态、继续吃额度。补丁在最后
#    put_inst() 之前先释放队列；msm_vidc_vb2_queue_deinit() 幂等（m2m_dev 为 NULL
#    直接返回），所以 close_helper() 里那次调用退化成空操作，自己收过摊的客户端也
#    不付代价（此时 vb2 队列已释放，等于空跑）。
#
# 两条都要打：只打①，固件侧干净了但实例照样留；只打②，固件堆照样漏。
#
# ⚠️ 不要改成"统计时跳过死会话"：死会话确实占着固件堆（泄漏是真的），跳过它会
# 让下一个会话在真正 OOM 的固件上启起来。实测（每次 kill -9 多漏一个）：
#   死会话 0..4 → 4K 硬解正常，streamon 失败 0
#   死会话 5    → 播放中板子硬复位（主机侧抓 /dev/kmsg 无 oops/panic，是固件侧）
SRC_URI += "file://0001-vidc-stop-streaming-and-release-the-queues-when-a-session-is-closed.patch"
