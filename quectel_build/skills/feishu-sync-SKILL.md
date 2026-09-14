---
name: feishu-sync
description: "飞书文档同步: 使用 lark-cli 将 markdown 技术文档同步到飞书知识库"
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [feishu, lark, docs, sync, wiki, markdown]
    related_skills: [qsm565dwf-sdk]
---

# 飞书文档同步

## Overview

使用 `lark-cli` 将项目中的 markdown 技术文档同步到飞书知识库。

## 官方参考文档

- lark-cli 配置指南: https://open.feishu.cn/document/mcp_open_tools/feishu-cli/set-up-lark-cli-for-ai-agents-in-openclaw_hermes.md
- lark-cli GitHub: https://github.com/larksuite/cli
- 飞书开放平台: https://open.feishu.cn

## 前置条件

### 安装 lark-cli
```bash
npm install -g @larksuite/cli
npx -y skills add https://open.feishu.cn --skill -y
```

### 绑定飞书凭证
```bash
# 以用户身份绑定（推荐，可读写个人文档）
lark-cli config bind --identity user-default --force

# 登录授权
lark-cli auth login --recommend --no-wait
# 在浏览器中完成授权后：
lark-cli auth login --device-code <device_code>
```

### 验证连接
```bash
lark-cli auth status
# 确认 identity == "user" 且 tokenStatus 为 "valid"
```

## 知识库结构

QuectelPi_Docs 知识库 (space_id: 7659695071058660538):
```
QuectelPi_Docs
├── 生态文档 (DdH2weO7GiCsu9k4zmBcr0YInEg)
│   ├── Quectel Pi H1 (VJp5wu2jTiEOR5kU6lBcoqwOnOh)
│   ├── Quectel Pi M1 (C4xrwMJpriJ8uskFDbUcpiJ5nfh)
│   │   ├── 中文 (KL4OwKkDni8pZTkdFuscYuvCnQe)
│   │   └── 英文 (Pbcrwbbc9iLaS5kCV8CcfkPrnec)
│   └── Quectel Pi L1 (LBYjwREl2i9UHAkLxHfcftxYnOf)
└── 文档规范 (OeiGwyn63iRctIkbUeGcOSKPnVg)
```

## 使用方法

### 读取飞书文档
```bash
lark-cli docs +fetch --doc "<飞书文档URL>" --doc-format markdown
```

### 在知识库中创建文档
```bash
# 1. 创建 wiki 节点
lark-cli wiki +node-create \
  --space-id 7659695071058660538 \
  --parent-node-token <父节点token> \
  --obj-type docx \
  --title "文档标题"

# 2. 写入内容
lark-cli docs +update \
  --doc <document_id> \
  --command overwrite \
  --content "<p>内容</p>" \
  --doc-format xml
```

### 插入图片
```bash
lark-cli docs +media-insert \
  --doc <document_id> \
  --file <本地图片路径>
```

## Markdown 同步流程

将本地 markdown 文件同步到飞书：

1. 读取 markdown 文件
2. 创建 wiki 节点
3. 转换为飞书格式并写入

### 示例：同步单个文件
```bash
# 读取本地 markdown
CONTENT=$(cat quectel_build/skills/at-debug-SKILL.md)

# 创建 wiki 节点
NODE=$(lark-cli wiki +node-create \
  --space-id 7659695071058660538 \
  --parent-node-token Pbcrwbbc9iLaS5kCV8CcfkPrnec \
  --obj-type docx \
  --title "AT指令调试" | jq -r '.data.obj_token')

# 写入内容
lark-cli docs +update \
  --doc $NODE \
  --command overwrite \
  --content "$CONTENT" \
  --doc-format markdown
```

### 示例：批量同步
```bash
# 遍历 skills 目录下的所有 md 文件
for f in quectel_build/skills/*.md; do
  TITLE=$(head -1 "$f" | sed 's/^# //')
  CONTENT=$(cat "$f")
  
  NODE=$(lark-cli wiki +node-create \
    --space-id 7659695071058660538 \
    --parent-node-token Pbcrwbbc9iLaS5kCV8CcfkPrnec \
    --obj-type docx \
    --title "$TITLE" | jq -r '.data.obj_token')
  
  lark-cli docs +update \
    --doc $NODE \
    --command overwrite \
    --content "$CONTENT" \
    --doc-format markdown
  
  sleep 1  # 避免频率限制
done
```

## 群聊管理

### 搜索群聊
```bash
lark-cli im +chat-search --query "群聊名称"
```

### 添加机器人到群聊
```bash
# 1. 搜索群聊获取 chat_id
lark-cli im +chat-search --query "Quectel Pi"

# 2. 添加机器人
lark-cli im chat.members create \
  --chat-id <chat_id> \
  --member-id-type app_id \
  --data '{"id_list":["cli_aac4914766381be8"]}'
```

### 创建新群聊
```bash
lark-cli im +chat-create \
  --name "群聊名称" \
  --user-ids "ou_d35ca7ea239dae8605b2095a273253bb" \
  --bot-manager
```

### 查看群成员
```bash
lark-cli im chat.members get --chat-id <chat_id>
```

### 发送消息
```bash
lark-cli im +messages-send \
  --chat-id <chat_id> \
  --content "消息内容"
```

## 注意事项

- lark-cli 会自动处理 token 刷新
- 图片需要先上传到飞书，再在文档中引用
- 批量操作时注意 API 频率限制
- 文档权限由飞书侧管理，需确保应用有访问权限
- wiki 节点创建后需要单独写入内容

## 故障排查

### 权限不足 (code: 3380004)
- 确认已用用户身份登录（非 bot-only）
- 确认文档已授权给当前用户

### Token 过期
```bash
lark-cli auth status
# 如果 expired，重新登录
lark-cli auth login --recommend --no-wait
```

### 命令不存在
```bash
# 确认 lark-cli 已安装
lark-cli --version

# 重新安装 skill
npx -y skills add https://open.feishu.cn --skill -y
```
