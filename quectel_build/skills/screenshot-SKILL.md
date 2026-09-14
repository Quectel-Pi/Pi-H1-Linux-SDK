# 屏幕截图工具 (screenshot.sh)

## 工具位置

```
quectel_build/tools/screenshot.sh
```

## 使用方法

```bash
# 截图并保存到 log/screen_shot/ 目录（自动命名）
quectel_build/tools/screenshot.sh

# 指定文件名
quectel_build/tools/screenshot.sh hdmi_demo.png
```

## 截图保存位置

所有截图统一保存到 **SDK 根目录下的 `log/screen_shot/` 目录**：

```
<SDK根目录>/log/screen_shot/
├── screenshot_20260714_141945.png
├── hdmi_demo.png
└── ...
```

## 工作原理

1. 通过 `pgrep gnome-shell` 找到 gnome-shell 进程 PID
2. 从 `/proc/<PID>/environ` 读取正确的 D-Bus 地址
3. 用 `su -s /bin/sh <user> -c 'DBUS_SESSION_BUS_ADDRESS=... gnome-screenshot -f ...'` 调用截图
4. 自动保存到 `log/screen_shot/` 目录

## 支持的场景

| 场景 | 用户 | D-Bus 地址 | 状态 |
|------|------|-----------|------|
| GDM greeter | gnome-initial-setup | /tmp/dbus-* 或 /run/user/990/bus | ✅ |
| 正常用户会话 | 登录用户 | /run/user/<uid>/bus | ✅ |

## 前提条件

- 设备上已安装 gnome-screenshot（`apt install -y gnome-screenshot`）
- gnome-shell 正在运行（有图形界面）
- D-Bus session bus 可访问

## 注意事项

- gnome-shell 的 D-Bus 地址可能不是标准的 `/run/user/<uid>/bus`，脚本会自动从进程环境获取
- GDM greeter 会话禁止截图（AccessDenied），需要正常用户会话
- 截图文件统一存放在 `log/screen_shot/` 目录，为文档 skill 服务
