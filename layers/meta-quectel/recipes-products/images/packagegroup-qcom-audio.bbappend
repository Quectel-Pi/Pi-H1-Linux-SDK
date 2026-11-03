# 屏蔽 pulseaudio(本产品用 PipeWire+pw-pal); PIPEWIRE_PKGS 不能置空:
# 置空会把 pipewire/wireplumber/pw-pal 运行时全部从 LINUX 镜像剥掉,
# AT+HEADSET_START 产测链(独立 pipewire 实例 + pal 节点)无二进制可用,
# 且本平台 raw ALSA 数据面不通, 音频只能走 PAL 通路。
PULSEAUDIO_PKGS = ""