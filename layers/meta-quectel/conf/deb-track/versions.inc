# 包版本（PR）的集中管理。由 quectel_build/tools/deb-iterate.sh 自动递增。
#
# 为什么要集中放这里：迭代一个组件时，必须让它产出的 deb 版本号变大，否则
# apt 认为"已是最新"，什么都不会做。手改每个 recipe 的 PR 容易漏、也容易
# 忘记。deb-iterate.sh 改这个文件，配方通过下面的约定取到值。
#
# 格式（冒号语法，pn- 后面是 recipe 的 PN）：
#     DEB_PR:pn-<recipe> = "rN"
#
# 配方侧只需要一行：
#     PR = "${@d.getVar('DEB_PR') or 'r0'}"
# 这样：有对应条目就用它，没有就退回 Yocto 默认的 r0。
#
# 新增一个可迭代的组件：在附表里加一行，并给对应 recipe 加上面那行 PR。
#
# 已接入的组件（recipe 里已有 PR = "${@d.getVar('DEB_PR') or 'r0'}"）：
#   - msm-va-driver           VA-API 硬解驱动
#   - linux-qcom-custom       内核（连带 684 个 kernel-module-*）
#   - linux-firmware          firmware 包
#
# 递增只会让版本变大，永远不会回退（dpkg 版本必须单调递增才能升级）。

DEB_PR:pn-msm-va-driver = "r0"
DEB_PR:pn-linux-qcom-custom = "r0"
DEB_PR:pn-linux-firmware = "r0"
