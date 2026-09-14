---
name: git-commit
description: "Quectel SDK Git 提交流程: 规则、步骤、commit信息格式、测试说明"
version: 1.0.0
author: Hermes Agent
license: MIT
platforms: [linux]
metadata:
  hermes:
    tags: [git, commit, workflow, quectel]
---

# Git 提交流程

## 规则
- **只有用户明确告知要 git commit 时才执行**，切勿在 git add 后自作主张执行
- 用户可能说 "提交一下"、"commit"、"git commit" 等，收到明确指令后再操作

## 提交步骤
1. **查看当前分支名**：`git rev-parse --abbrev-ref HEAD`
2. **查看改动文件**：`git status`
3. **暂存文件**：`git add <文件>`（根据用户指定的文件，或全部改动）
4. **生成提交信息**：根据 `.gitcontent` 模板，结合实际修改内容填写
5. **执行提交**：`git commit -m "<提交信息>"`
6. **与用户确认**：展示提交内容（commit hash、改动文件列表、提交信息）
7. **提供推送指令**：`git push origin HEAD:refs/for/<当前分支名>`

## 提交信息格式

**必须使用 `.gitcontent` 模板的完整格式**，不能只写 `-m "简单描述"`。

正确做法：
1. 先读 `.gitcontent` 获取模板
2. `git log --format=full -1` 查看上一次 commit 的格式作为参考
3. 用完整模板格式提交

错误做法 ❌：
```bash
git commit -m "简单的描述"
```

正确做法 ✅：
```bash
git commit -m "<master_r02><AI><Igni>:简要描述

<修改分类>: 新增需求/客户缺陷修改/内部缺陷修改
<根本原因>: ...
<解决方案>: ...
[Jira ID]: N
[Case/CQ]: N
[安全修复]: N
<影响功能名称>: N
<适用项目>: debian
<影响项目类型>: Open项目
<风险等级>: NA
  业务影响: NA
  概率大小: NA
[关联其它修改点]: N
<AI生成代码占比>:100

<<<研发自检>>>
[ATC文档名称]: N
<是否使用运营商DM协议:Onenet/OMA/LwM2M等>: N
<是否使用第三方服务器:XTRA等>: N
<是否使用第三方软件:SSL库等>: N
<是否为音频默认参数>: N
<是否修改配置文件:rawdata分区数据修改等>: N
<是否修改MBN/NV等>: N
<是存在定时器非预期的唤醒和休眠锁未释放>: N
<否存存在参数结构变更影响前后版本的兼容性>: N
<是否存在NV/文件系统/存储介质的频繁擦写>: N
<是否存在客户敏感信息泄痛>: N
<是否存在重要参数还原失败系统不停重启或变砖>: N
<是否更新API文档>: N
[其他需要合入的分支]: N
[RN描述]: N

<<<测试说明>>>
<软件测试>: Y
  测试方法: 描述如何验证修改
  测试步骤: 1. xxx 2. xxx 3. xxx
<压力测试>: N
<硬件测试>: N
<AP侧AT指令新增或修改>: N"
```

## 测试说明填写规则

- 实际需要测试的功能填 `Y`，并写明测试方法和步骤
- 不需要测试的填 `N`
- 例如：修改 AT 指令 → `<AP侧AT指令新增或修改>: Y`，写明测试指令和预期结果
- 例如：仅文档修改 → 所有测试项填 `N`

## 注意事项

- **不要自作主张执行 `git commit` 和 `git push`**，必须先询问用户确认
- **commit 前必须打印完整的 `git commit -m` 信息**，让用户确认后再执行
- 不要执行 `git push`，只提供命令让用户自行执行
- commit 信息中的详细字段（Jira ID、测试说明等）根据实际情况填写，不确定的填 N
