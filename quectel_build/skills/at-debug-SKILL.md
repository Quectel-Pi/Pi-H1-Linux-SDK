---
name: at-debug
description: "Quectel QCS6490 AT指令调试: USB AT口连接、常用指令、日志查看"
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [qualcomm, qcs6490, quectel, at, modem, debug, serial]
    related_skills: [qsm565dwf-sdk, qcs6490-flash]
---

# AT指令调试

## Overview

Quectel QCS6490 开发板 AT指令调试指南，包括USB AT口连接和常用指令测试。

## USB AT口识别

### 设备列表
| 设备 | 类型 | 说明 |
|------|------|------|
| /dev/ttyACM0 | CH340/CH341 | USB转串口，非AT口 |
| /dev/ttyACM1 | CDC ACM | USB AT口 ✅ |
| /dev/ttyUSB0 | FT232R | 调试串口(登录shell) |

### 权限配置
用户需要在 `dialout` 组才能访问串口设备：
```bash
sudo usermod -aG dialout <用户名>
# 重新登录生效
```

## 连接测试

### Python脚本测试
```python
import os, time, select, termios, tty

def test_at_port():
    fd = os.open('/dev/ttyACM1', os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd, termios.TCSANOW)
    
    # 发送AT
    os.write(fd, b'AT\r\n')
    time.sleep(0.5)
    
    # 读取响应
    readable, _, _ = select.select([fd], [], [], 2)
    if readable:
        data = os.read(fd, 1024)
        print(data.decode('utf-8', errors='replace'))
    
    os.close(fd)
```

### picocom连接
```bash
# 安装picocom
sudo apt-get install picocom

# 连接AT口
picocom -b 115200 /dev/ttyACM1
```

## 常用AT指令

### 基本测试
```
AT          # 测试AT功能
ATI         # 查询设备信息
AT+CPIN?    # 查询SIM卡状态
AT+CSQ      # 查询信号强度
```

### MAC地址读写 (QMAC)
```
AT+QMAC?                    # 读取MAC地址
AT+QMAC="MACR",0            # 读取MAC (另一种格式)
AT+QMAC="MACW",0,AABBCCDDEEFF  # 写入MAC
```

**QMAC返回格式:**
- AT+QMAC? 返回: `+QMAC: AABBCCDDEEFF`
- AT+QMAC="MACR",0 返回: `MACR: "AABBCCDDEEFF"`
- AT+QMAC="MACW",0,xxx 返回: `OK`

### 网络注册
```
AT+CREG?    # 查询网络注册状态
AT+CEREG?   # 查询PS网络注册状态
AT+CGREG?   # 查询GPRS网络注册状态
```

### SIM卡信息
```
AT+ICCID     # 查询SIM卡ICCID
AT+CCID      # 查询SIM卡CCID (另一种方式)
```

## 日志查看

### 查看ATCI日志
```bash
# 通过adb查看
adb shell journalctl --no-pager -n 200 | grep -iE "ATCI|qmac"

# 实时查看
adb shell journalctl -f | grep -i ATCI
```

### 日志内容说明
```
[ATCI] readerLoop(): Command:AT+QMAC="MACR",0      # 收到的指令
[ATCI] quectel_command_hdlr(): AT+QMAC              # 指令处理
[ATCI] send_at_response:MACR: "E82404891BC3"        # 返回值
```

## 故障排查

### AT口无响应
1. 检查设备是否存在: `ls /dev/ttyACM*`
2. 检查权限: `ls -la /dev/ttyACM1`
3. 检查进程占用: `lsof /dev/ttyACM1`
4. 查看日志: `adb shell journalctl -n 50 | grep ATCI`

### 返回空数据
1. 检查波特率: 默认115200
2. 检查串口参数: 8N1, raw模式
3. 查看是否有数据发送: 用逻辑分析仪或示波器

### MAC地址读写失败
1. 检查文件权限: `ls -la /var/persist/mac_addr`
2. 查看日志: `adb shell journalctl | grep QMAC`
3. 手动测试读写: 用echo命令测试文件

## 新增AT指令

### 架构概览

AT 指令处理链路：

```
串口接收 AT 命令
  → atcid_cmd_dispatch.c 的 quectel_command_hdlr()
    → 遍历 quec_at_cmd_table[] 匹配命令名
      → 调用对应的 Handle 函数
        → 将 response 写回串口
```

### 关键文件

| 文件 | 说明 |
|------|------|
| `atci/quectel/inc/ql_atcmd_info.h` | 命令注册表 `quec_at_cmd_table[]` |
| `atci/quectel/src/ql_at_factory.c` | Handler 实现（可新建独立 .c 文件） |
| `atci/quectel/inc/ql_at_factory.h` | Handler 函数声明（可新建独立 .h 文件） |
| `atci/atcid_cmd_dispatch.c` | 命令分发入口 `quectel_command_hdlr()` |
| `atci/Makefile` | 构建配置，新文件需加入 SRCS |

所有路径相对于 `layers/meta-quectel/recipes-quectel/atcid/files/`。

### 新增步骤（以 AT+QMYCMD 为例）

#### 步骤 1：编写 Handler 函数

在 `ql_at_factory.c` 中添加，或为大型命令新建独立文件（如 `ql_my_cmd.c`）。

Handler 签名固定：

```c
ATRESPONSE_t QL_AT_QMYCMD_Handle(char* cmdline, ATOP_t opType, char* response)
```

参数说明：
- `cmdline`  — 原始 AT 命令字符串（如 `"AT+QMYCMD=1,2"`）
- `opType`   — 操作类型，枚举值（见下方操作类型表）
- `response` — 输出缓冲区，填充响应内容，最大 `MAX_RESPONSE_LEN`

返回值：
- `AT_OK`    = 成功（自动回复 OK）
- `AT_ERROR` = 失败（自动回复 ERROR）

解析参数用 `at_tok.h` 提供的函数：
- `at_tok_nextint(&cmdline, &val)` — 读取下一个整数
- `at_tok_nextstr(&cmdline, &str)` — 读取下一个字符串

典型实现模板：

```c
ATRESPONSE_t QL_AT_QMYCMD_Handle(char* cmdline, ATOP_t opType, char* response) {
    LOGATCI(LOG_DEBUG, "[QL_AT_QMYCMD_Handle] cmdline %s", cmdline);
    switch(opType) {
        case AT_SET_OP: {
            int val1, val2;
            if (at_tok_nextint(&cmdline, &val1) < 0) return AT_ERROR;
            if (at_tok_nextint(&cmdline, &val2) < 0) return AT_ERROR;
            // 你的业务逻辑...
            sprintf(response, "+QMYCMD: %d,%d", val1, val2);
            return AT_OK;
        }
        case AT_READ_OP:
            sprintf(response, "+QMYCMD: %d,%d", saved_val1, saved_val2);
            return AT_OK;
        case AT_TEST_OP:
            sprintf(response, "+QMYCMD: (0-100),(0-100)");
            return AT_OK;
        default:
            break;
    }
    return AT_ERROR;
}
```

#### 步骤 2：声明头文件

在 `ql_at_factory.h` 中添加：

```c
ATRESPONSE_t QL_AT_QMYCMD_Handle(char* cmdline, ATOP_t opType, char* response);
```

如果新建了独立头文件（如 `ql_my_cmd.h`），需在 `ql_atcmd_info.h` 中 `#include` 它。

#### 步骤 3：注册到命令表

在 `ql_atcmd_info.h` 的 `quec_at_cmd_table[]` 数组末尾添加：

```c
{"AT+QMYCMD", AT_SET_OP | AT_READ_OP | AT_TEST_OP, QL_AT_QMYCMD_Handle},
```

`opType` 按需求用 `|` 组合，表示支持哪些操作。

#### 步骤 4：如果新建了独立文件

在 `Makefile` 的 `SRCS` 中添加：

```makefile
./quectel/src/ql_my_cmd.c \
```

### ATOP_t 操作类型

| 类型 | 含义 | 对应命令格式 |
|------|------|-------------|
| `AT_BASIC_OP` | 基础操作 | `ATI`, `AT` 等无 `+` 前缀命令 |
| `AT_ACTION_OP` | 无参执行 | `AT+XXX` |
| `AT_READ_OP` | 读取 | `AT+XXX?` |
| `AT_SET_OP` | 设置参数 | `AT+XXX=...` |
| `AT_TEST_OP` | 查询范围 | `AT+XXX=?` |

### 参考资料

- 代码位置: `layers/meta-quectel/recipes-quectel/atcid/files/atci/quectel/src/ql_at_factory.c`
- 命令注册: `layers/meta-quectel/recipes-quectel/atcid/files/atci/quectel/inc/ql_atcmd_info.h`
- QMAC函数: `QL_AT_QMAC_Handle`
- MAC文件路径: `/var/persist/mac_addr`
