require mesa.inc

SUMMARY += " (OpenGL only, no EGL/GLES)"

PROVIDES = "virtual/libgl virtual/mesa"

S = "${WORKDIR}/mesa-${PV}"

# At least one DRI rendering engine is required to build mesa.
# When no X11 is available, use osmesa for the rendering engine.
PACKAGECONFIG ??= "opengl ${@bb.utils.contains('DISTRO_FEATURES', 'x11', 'x11', 'osmesa gallium', d)}"
PACKAGECONFIG:class-target = "opengl ${@bb.utils.contains('DISTRO_FEATURES', 'x11', 'x11', 'osmesa gallium', d)}"

# 禁用 GLX 和 X11 相关配置
PACKAGECONFIG:remove:pn-mesa-gl = "x11"
# 确保启用至少一个 Gallium 驱动（即使不需要硬件加速）
PACKAGECONFIG:append:pn-mesa-gl = " gallium-llvm llvmpipe"
