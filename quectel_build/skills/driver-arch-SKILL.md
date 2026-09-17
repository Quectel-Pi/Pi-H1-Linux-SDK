# 驱动架构与源码索引

> 记录 QCS6490-IDP-PI 各子系统的设备树 (DTS) 与驱动源码映射关系，
> 作为 AI Agent 排查/修改问题时快速定位代码的参考索引。

---

## 源码查找方法

### 从 DTS compatible 字符串定位驱动源码

```bash
grep -r "<compatible-string>" sources/quectel-src/kernel/qcom-6.6/ --include="*.c"
```

### 追踪 DTS include 链

主板 DTS: `arch/arm64/boot/dts/qcom/qcs6490-idp-pi.dts`
  ├── `qcm6490.dtsi`           # SoC 级节点定义
  ├── `pm7250b.dtsi`           # PMIC 7250B
  ├── `pm7325.dtsi`            # PMIC 7325
  ├── `pm8350c.dtsi`           # PMIC 8350C
  ├── `pmk8350.dtsi`           # PMIC K8350
  └── `qcm6490-addons.dtsi`    # 扩展节点

### 整理子系统源码快照

```bash
# 将某子系统的 DTS + 驱动文件按 kernel 路径复制到 log/ 供查阅
mkdir -p log/arch/arm64/boot/dts/qcom
mkdir -p log/sound/soc/...
cp <dts-file> log/arch/arm64/boot/dts/qcom/
cp <driver-file> log/<kernel-relative-path>/

# 查看已有快照
find log/ -type f | sort
```

> log/ 目录已加入 .gitignore，不会被提交。

---

## 子系统模板

> 每整理一个子系统，按以下结构在下方新增章节。

```
## 子系统: <名称>

### DTS 文件
| 文件 | 说明 |
|------|------|
| arch/arm64/boot/dts/qcom/xxx.dts | ... |
| arch/arm64/boot/dts/qcom/xxx.dtsi | ... |

### 驱动文件
| 层级 | 文件 | 说明 |
|------|------|------|
| Machine | sound/soc/... | ... |
| Codec   | sound/soc/codecs/... | ... |
| Bridge  | drivers/gpu/drm/bridge/... | ... |

### 关键 DTS 节点拓扑
(ASCII 树形图，展示 compatible → phandle → 子节点引用链)

### GPIO 速查
| GPIO | 信号 | 用途 |
|------|------|------|

### 调试文档
- 对应的 *-SKILL.md 引用
```

---

## 编译提示

**DTS 修改** (需重新编译内核):
```bash
cd <项目根目录>
source quectel_build/compile/build.sh
buildconfig QSM565DWF QSM565DWFPARL1A01_BP01.001_Linux6.6.38_V01 <系统> <版本>
bitbake quecpi-image && buildpackage
```

**驱动修改** (仅 recompile 模块):
```bash
bitbake <包名> -c compile -f
```
