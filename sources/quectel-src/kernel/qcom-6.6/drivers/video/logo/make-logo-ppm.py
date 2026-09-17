#!/usr/bin/env python3
"""把 logo 源图转成内核启动 logo 需要的 224 色 PPM (plain P3 ASCII)。

为什么需要:
  - 内核 pnmtologo 只吃 **ASCII** PNM (P3); 二进制 P6 会被它直接
    die("Binary PNM is not supported")。见 drivers/video/logo/pnmtologo.c:145。
  - 内核启动 logo 必须是 CLUT224 (224 色上限), 源图带抗锯齿灰阶, 颜色远超
    224, 必须先量化。

关于尺寸 (重要, 踩过):
  内核 fbmem.c:648 有硬约束
      if (fb_logo.logo->height > yres) { fb_logo.logo = NULL; return 0; }
  logo 高度超过屏幕高度会被**整个丢弃**, 一帧都不画。所以源图必须是
  <= 800 高 (本板 yres=800)。

源图来源:
  厂商文件 RK3576_Linux6.1_BL01_v1.2.0/kernel-6.1/quectel_720p.bmp
  720x1280 黑底竖版, 实际内容为居中一条 600x88 横幅
  (非黑像素包围盒 x 60..659, y 596..683)。
  已按该包围盒裁出 quectel_logo.png (600x88) 存本目录, 用 --find-bbox
  可在原图上复算这个包围盒。

用法:
  python3 make-logo-ppm.py quectel_logo.png <输出.ppm>
  python3 make-logo-ppm.py 原图.bmp --find-bbox          # 打印内容包围盒
"""
import sys
from PIL import Image

MAXCOLORS = 224


def find_bbox(im):
    px = im.load()
    W, H = im.size
    minx, miny, maxx, maxy = W, H, -1, -1
    for y in range(H):
        for x in range(W):
            if px[x, y] != (0, 0, 0):
                minx, maxx = min(minx, x), max(maxx, x)
                miny, maxy = min(miny, y), max(maxy, y)
    return (minx, miny, maxx + 1, maxy + 1)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]

    if "--find-bbox" in sys.argv:
        print("非黑内容包围盒 (left, top, right, bottom):", find_bbox(Image.open(src).convert("RGB")))
        return

    im = Image.open(src).convert("RGB")
    w, h = im.size
    if h > 800:
        sys.exit(f"拒绝: 源图高 {h} > 800, 内核会丢弃 logo (见文件头)")

    # 文字类图形不要抖动, 否则边缘出噪点
    q = im.quantize(colors=MAXCOLORS, method=Image.Quantize.MEDIANCUT,
                    dither=Image.Dither.NONE)
    rgb = q.convert("RGB")
    raw = rgb.tobytes()          # 用 tobytes 而不是 getdata(): 类型干净且更快
    data = [(raw[i], raw[i + 1], raw[i + 2]) for i in range(0, len(raw), 3)]
    used = len(set(data))

    with open(dst, "w") as f:
        f.write(f"P3\n# Quectel boot logo {w}x{h}\n{w} {h}\n255\n")
        for y in range(h):
            # 每行一个源图行, 便于人眼 diff
            f.write("".join("%d %d %d\n" % p for p in data[y * w:(y + 1) * w]))
    print(f"{dst}: {w}x{h}, 用色 {used} (上限 {MAXCOLORS})")


if __name__ == "__main__":
    main()
