# QCS6490 HDMI 音频通路完整分析

## 架构概览

```
PipeWire (pw-pal-plugin.conf)
  pal_sink_hdmi_out_ll/db, jack-name="HDMI/DP1 Jack"
      │
      ▼
PAL (resourcemanager_qcm6490_idp.xml)
  PAL_DEVICE_OUT_HDMI → back_end_name="DISPLAY_PORT-RX"
      │
      ▼
Q6 DSP AFE (q6dsp-lpass-ports.c)
  DISPLAY_PORT_RX_1: 48/96/192kHz, 2-8ch, S16/S24
      │
      ▼
DTS DAI Link (qcs6490-idp-pi.dts)
  hdmi-dai-link: cpu=DISPLAY_PORT_RX_1, codec=hdmi-audio-codec
      │
      ▼
HDMI Codec 框架 (hdmi-codec.c)
  hdmi_codec_startup → get_eld → hw_params
      │
      ▼
LT9611UXC/UXD 驱动 (lontium-lt9611uxc.c / lontium-lt9611uxd.c)
  of_node → hdmi-audio-codec 共享节点
  hw_params → 自动检测 / 参数校验
      │
      ▼
LT9611 芯片 I2S 输入 → HDMI 输出
```

---

## 1. PipeWire PAL 插件配置

**文件:** `prebuild/audio-profile/usr/share/pipewire/pipewire.conf.d/pw-pal-plugin.conf`

### HDMI sink 定义 (行 78-113)

```
pal_sink_hdmi_out_ll  → jack-name = "HDMI/DP1 Jack"  (低延迟, notification)
pal_sink_hdmi_out_db  → jack-name = "HDMI/DP1 Jack"  (深缓冲, music)
```

### DP sink 定义 (行 66-101)

```
pal_sink_dp_out_ll    → jack-name = "HDMI/DP0 Jack"  (低延迟, notification)
pal_sink_dp_out_db    → jack-name = "HDMI/DP0 Jack"  (深缓冲, music)
```

### 关键字段说明

| 字段 | 作用 |
|---|---|
| `jack-name` | 关联内核 ALSA jack 控制，决定 sink 何时可用 |
| `media.role` | `music` = 深缓冲, `notification` = 低延迟 |
| `media.class` | `Audio/Sink` = 播放, `Audio/Source` = 录音 |

**工作原理:** 有 `jack-name` 的 sink 只在对应显示器连接时可用。没有 `jack-name` 的 sink（speaker、headset）始终可用。

---

## 2. 内核 DAI Link 定义

**文件:** `sources/quectel-src/kernel/qcom-6.6/arch/arm64/boot/dts/qcom/qcs6490-idp-pi.dts`

### 共享 codec 节点 (行 49-57)

```dts
hdmi_audio: hdmi-audio-codec {
    #sound-dai-cells = <0>;
};
```

这是一个无驱动的 DTS 节点，仅作为 `of_node` 锚点。LT9611UXC 或 LT9611UXD 驱动在 probe 成功后将各自注册的 hdmi-codec 平台设备的 `of_node` 指向此节点，使 ALSA SoC 框架能正确匹配 codec。

### DP DAI Link (行 220-230)

```dts
dp-dai-link {
    link-name = "DISPLAY_PORT-RX";
    cpu {
        sound-dai = <&q6apmbedai DISPLAY_PORT_RX_0>;
    };
    codec {
        sound-dai = <&mdss_dp>;
    };
};
```

### HDMI DAI Link (行 232-242)

```dts
hdmi-dai-link {
    link-name = "DISPLAY_PORT-RX";
    cpu {
        sound-dai = <&q6apmbedai DISPLAY_PORT_RX_1>;
    };
    codec {
        sound-dai = <&hdmi_audio>;
    };
};
```

**关键点:** 两个 DAI link 都使用 `link-name = "DISPLAY_PORT-RX"`，共用同一个 ACDB 后端条目。HDMI 使用 `DISPLAY_PORT_RX_1`（Q6 端口 1），DP 使用 `DISPLAY_PORT_RX_0`（Q6 端口 0）。

---

## 3. QCM6490 Machine Driver

**文件:** `sources/quectel-src/kernel/qcom-6.6/sound/soc/qcom/qcm6490.c`

### Jack 创建 (行 51-110)

```c
// qcm6490_snd_init()
case DISPLAY_PORT_RX_0:
    hdmi_pcm_id = 0;
    hdmi_jack = &data->hdmi_jack[0];    // → "HDMI/DP0 Jack"
    break;
case DISPLAY_PORT_RX_1:
    hdmi_pcm_id = 1;
    hdmi_jack = &data->hdmi_jack[1];    // → "HDMI/DP1 Jack"
    break;

// 行 92-94: 创建 jack 控制
snprintf(jack_name, sizeof(jack_name), "HDMI/DP%d Jack", hdmi_pcm_id);
snd_soc_card_jack_new(rtd->card, jack_name, SND_JACK_AVOUT, hdmi_jack);
```

`DISPLAY_PORT_RX_1` 创建 `HDMI/DP1 Jack`，与 `pw-pal-plugin.conf` 中的 `jack-name` 对应。

### hw_params fixup (行 112-136)

```c
// qcm6490_be_hw_params_fixup()
rate->min = rate->max = 48000;          // 强制 48kHz
channels->min = channels->max = 2;      // 强制 2 声道
```

### be_ops 注册 (行 221-234)

```c
// qcm6490_add_be_ops()
link->init = qcm6490_snd_init;                    // jack 初始化
link->be_hw_params_fixup = qcm6490_be_hw_params_fixup;  // 参数修正
```

---

## 4. LT9611UXC 驱动 (HDMI 桥片)

**文件:** `sources/quectel-src/kernel/qcom-6.6/drivers/gpu/drm/bridge/lontium-lt9611uxc.c`

### hw_params (行 732-741)

```c
static int lt9611uxc_hdmi_hw_params(struct device *dev, void *data,
                    struct hdmi_codec_daifmt *fmt,
                    struct hdmi_codec_params *hparms)
{
    /* LT9611UXC 自动检测采样率和位宽，无需配置 */
    return 0;
}
```

直接返回 0，不做参数校验。

### get_eld (行 767-778)

```c
static int lt9611uxc_audio_get_eld(struct device *dev,
    void *data, uint8_t *buf, size_t len)
{
    struct lt9611uxc *lt9611uxc = (struct lt9611uxc *)data;
    mutex_lock(&lt9611uxc->ocm_lock);
    memcpy(buf, lt9611uxc->connector.eld,
            min(sizeof(lt9611uxc->connector.eld), len));
    mutex_unlock(&lt9611uxc->ocm_lock);
    return 0;
}
```

从 DRM connector 拷贝 ELD 数据。ELD 来自 EDID 的音频块。

### Codec ops 注册 (行 798-804)

```c
static const struct hdmi_codec_ops lt9611uxc_codec_ops = {
    .hw_params       = lt9611uxc_hdmi_hw_params,
    .audio_shutdown  = lt9611uxc_audio_shutdown,
    .get_dai_id      = lt9611uxc_hdmi_i2s_get_dai_id,
    .get_eld         = lt9611uxc_audio_get_eld,
    .hook_plugged_cb = lt9611uxc_audio_hook_plugged_cb,
};
```

### audio_init — of_node 设置 (行 806-835)

```c
static int lt9611uxc_audio_init(struct device *dev, struct lt9611uxc *lt9611uxc)
{
    struct device_node *codec_node;
    struct hdmi_codec_pdata codec_data = {
        .ops = &lt9611uxc_codec_ops,
        .max_i2s_channels = 2,
        .i2s = 1,
        .data = lt9611uxc,
    };

    codec_node = of_find_node_by_name(NULL, "hdmi-audio-codec");

    lt9611uxc->audio_pdev =
        platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
                          PLATFORM_DEVID_AUTO,
                          &codec_data, sizeof(codec_data));
    if (!IS_ERR_OR_NULL(lt9611uxc->audio_pdev) && codec_node)
        lt9611uxc->audio_pdev->dev.of_node = codec_node;

    of_node_put(codec_node);
    return PTR_ERR_OR_ZERO(lt9611uxc->audio_pdev);
}
```

**关键:** 通过 `of_find_node_by_name(NULL, "hdmi-audio-codec")` 找到 DTS 共享节点，设置 `of_node` 使 ALSA SoC 框架能将此 codec 匹配到 `hdmi-dai-link`。

### Probe 中的 EDID 等待 (行 1147-1176)

```c
if (lt9611uxc->hpd_supported) {
    unsigned int irq_val = 0, hpd_val = 0;
    int retry;

    /* MCU 需要时间读取 EDID，轮询等待 */
    for (retry = 0; retry < 10; retry++) {
        lt9611uxc_lock(lt9611uxc);
        regmap_read(lt9611uxc->regmap, 0xb022, &irq_val);
        regmap_read(lt9611uxc->regmap, 0xb023, &hpd_val);
        if (irq_val)
            regmap_write(lt9611uxc->regmap, 0xb022, 0);
        lt9611uxc_unlock(lt9611uxc);
        if (hpd_val & BIT(0))   // EDID ready bit
            break;
        msleep(500);
    }
    // 最多等待 5 秒
}
```

---

## 5. LT9611UXD 驱动 (备用 HDMI 桥片)

**文件:** `sources/quectel-src/kernel/qcom-6.6/drivers/gpu/drm/bridge/lontium-lt9611uxd.c`

### hw_params (行 1061-1102)

```c
static int lt9611uxd_hdmi_hw_params(struct device *dev, void *data,
                   struct hdmi_codec_daifmt *fmt,
                   struct hdmi_codec_params *hparms)
{
    switch (hparms->sample_rate) {
    case 32000: case 44100: case 48000:
    case 88200: case 96000: case 176400: case 192000:
        break;
    default:
        return -EINVAL;     // 不支持的采样率
    }
    switch (hparms->sample_width) {
    case 16: case 18: case 20: case 24:
        break;
    default:
        return -EINVAL;     // 不支持的位宽
    }
    switch (fmt->fmt) {
    case HDMI_I2S: case HDMI_SPDIF:
        break;
    default:
        return -EINVAL;     // 不支持的格式
    }
    return 0;
}
```

与 UXC 不同，UXD 会校验参数。

### Codec ops (行 1137-1142)

```c
static const struct hdmi_codec_ops lt9611uxd_codec_ops = {
    .hw_params       = lt9611uxd_hdmi_hw_params,
    .audio_shutdown  = lt9611uxd_audio_shutdown,
    .audio_startup   = lt9611uxd_audio_startup,
    .hook_plugged_cb = lt9611uxd_hdmi_audio_hook_plugged_cb,
};
```

**注意:** UXD 没有 `get_eld` 和 `get_dai_id` 回调。

### audio_init (行 1144-1170)

与 UXC 相同模式 — `of_find_node_by_name(NULL, "hdmi-audio-codec")` 设置 `of_node`。区别是 `max_i2s_channels = 8`。

### 互斥机制

UXC probe (行 1052-1055): 检查 `lcd_info.detected != 3` 则 defer，等待 UXD 先 probe。
UXD probe 失败 (行 1591): 设置 `lcd_info.detected = 3`，允许 UXC probe。
**同一时间只有一个桥片驱动注册 audio codec。**

---

## 6. HDMI Codec 框架

**文件:** `sources/quectel-src/kernel/qcom-6.6/sound/soc/codecs/hdmi-codec.c`

### startup (行 440-484)

```c
static int hdmi_codec_startup(struct snd_pcm_substream *substream,
                  struct snd_soc_dai *dai)
{
    // ...
    if (tx && hcp->hcd.ops->get_eld) {
        ret = hcp->hcd.ops->get_eld(dai->dev->parent, hcp->hcd.data,
                        hcp->eld, sizeof(hcp->eld));
        ret = snd_pcm_hw_constraint_eld(substream->runtime, hcp->eld);
        hdmi_codec_eld_chmap(hcp);
    }
}
```

获取 ELD 数据并设置 PCM 约束。

### get_ch_alloc_table_idx (行 354-377)

```c
static int hdmi_codec_get_ch_alloc_table_idx(struct hdmi_codec_priv *hcp,
                         unsigned char channels)
{
    spk_alloc = drm_eld_get_spk_alloc(hcp->eld);
    spk_mask = hdmi_codec_spk_mask_from_alloc(spk_alloc);

    for (i = 0; i < ARRAY_SIZE(hdmi_codec_channel_alloc); i++, cap++) {
        if (!spk_alloc && cap->ca_id == 0)
            return i;           // ELD 为空时回退到立体声
        if (cap->n_ch != channels)
            continue;
        if (!(cap->mask == (spk_mask & cap->mask)))
            continue;
        return i;
    }
    return -EINVAL;             // 无匹配 → 返回 -22
}
```

**这是 `-22` 错误的来源。** 如果 ELD 的 speaker allocation 与请求的声道数不匹配，返回 `-EINVAL`。

### hw_params (行 556-599)

```c
static int hdmi_codec_hw_params(struct snd_pcm_substream *substream,
                struct snd_pcm_hw_params *params,
                struct snd_soc_dai *dai)
{
    ret = hdmi_codec_fill_codec_params(dai, width, rate, channels, &hp);
    if (ret < 0)
        return ret;     // ← get_ch_alloc_table_idx 失败时到这里

    ret = snd_pcm_fill_iec958_consumer_hw_params(params, hp.iec.status, ...);
    cf->bit_fmt = params_format(params);
    return hcp->hcd.ops->hw_params(dai->dev->parent, hcp->hcd.data, cf, &hp);
}
```

### DAI 定义 (行 948-968)

```c
// hdmi_i2s_dai
.name = "i2s-hifi",
.playback = {
    .channels_min = 2,
    .channels_max = 8,
    .rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000,
    .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
},
```

---

## 7. Q6 AFE 端口定义

**文件:** `sources/quectel-src/kernel/qcom-6.6/sound/soc/qcom/qdsp6/q6dsp-lpass-ports.c`

### Q6AFE_DP_RX_DAI 宏 (行 77-92)

```c
#define Q6AFE_DP_RX_DAI(did) {
    .playback = {
        .stream_name = #did" Playback",
        .rates = SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000,
        .formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
        .channels_min = 2,
        .channels_max = 8,
    },
    .name = #did,
    .id = did,
};
```

### DISPLAY_PORT_RX 实例化 (行 724-731)

```c
Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_0),    // DP 用
Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_1),    // HDMI 用
Q6AFE_DP_RX_DAI(DISPLAY_PORT_RX_2),
// ... 到 DISPLAY_PORT_RX_7
```

### ops 赋值 (行 785-789)

```c
case DISPLAY_PORT_RX:
case DISPLAY_PORT_RX_1 ... DISPLAY_PORT_RX_7:
    q6dsp_audio_fe_dais[i].ops = cfg->q6hdmi_ops;
    break;
```

所有 DISPLAY_PORT_RX 端口使用 `q6hdmi_ops`。

---

## 8. DP 音频驱动 (对比参考)

**文件:** `sources/quectel-src/kernel/qcom-6.6/drivers/gpu/drm/msm/dp/dp_audio.c`

### 注册 hdmi-codec (行 523-537)

```c
int msm_dp_register_audio_driver(struct device *dev, struct msm_dp_audio *msm_dp_audio)
{
    audio_priv->audio_pdev = platform_device_register_data(dev,
                            HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
                            &codec_data, sizeof(codec_data));
    return PTR_ERR_OR_ZERO(audio_priv->audio_pdev);
}
```

**关键区别:** DP 驱动注册 hdmi-codec 时**没有设置 `of_node`**。这就是为什么 LT9611 驱动必须显式设置 `of_node`，否则 ALSA SoC 框架会 fallback 到 DAI 名字匹配，先注册的 DP codec（空 ELD）会被错误绑定。

### get_eld (行 398-421)

```c
static int msm_dp_audio_get_eld(struct device *dev, void *data, uint8_t *buf, size_t len)
{
    memcpy(buf, msm_dp_display->connector->eld,
        min(sizeof(msm_dp_display->connector->eld), len));
    return 0;
}
```

当没有 DP 显示器连接时，`connector->eld` 为空。

---

## 9. 音频配置文件

### resourcemanager_qcm6490_idp.xml

**文件:** `prebuild/audio-profile/etc/resourcemanager_qcm6490_idp.xml`

```xml
<!-- 行 848-859: DP 输出 -->
<out-device>
    <id>PAL_DEVICE_OUT_AUX_DIGITAL</id>
    <back_end_name>DISPLAY_PORT-RX</back_end_name>
    <max_channels>32</max_channels>
    <channels>2</channels>
    <snd_device_name>display-port</snd_device_name>
</out-device>

<!-- 行 860-871: HDMI 输出 -->
<out-device>
    <id>PAL_DEVICE_OUT_HDMI</id>
    <back_end_name>DISPLAY_PORT-RX</back_end_name>
    <max_channels>32</max_channels>
    <channels>2</channels>
    <snd_device_name>display-port</snd_device_name>
</out-device>
```

**两者共用 `DISPLAY_PORT-RX` 后端和 `display-port` mixer path。**

### backend_conf.xml

**文件:** `prebuild/audio-profile/etc/backend_conf.xml`

```xml
<!-- 行 44 -->
<device name="DISPLAY_PORT-RX" rate="48000" ch="2" bits="16" />
<!-- 行 45 -->
<device name="HDMI_PORT-RX" rate="48000" ch="2" bits="16" />
```

### mixer_paths_qcm6490_idp.xml

**文件:** `prebuild/audio-profile/etc/mixer_paths_qcm6490_idp.xml`

```xml
<!-- 行 487-488 -->
<path name="display-port">
</path>

<!-- 行 490-491 -->
<path name="display-port1">
</path>

<!-- 行 493-494 -->
<path name="hdmi">
</path>
```

路径为空，因为 HDMI/DP 音频不经过 Qualcomm codec mixer，直接通过 Q6 AFE DISPLAY_PORT 后端发送。

---

## 10. 完整数据流

```
┌─────────────────────────────────────────────────────────────┐
│                    用户空间 (PipeWire + PAL)                  │
├─────────────────────────────────────────────────────────────┤
│ pw-pal-plugin.conf                                          │
│   pal_sink_hdmi_out_db (jack-name="HDMI/DP1 Jack")         │
│       │                                                     │
│       ▼                                                     │
│ PAL → PAL_DEVICE_OUT_HDMI (ID 10)                          │
│       │                                                     │
│       ▼                                                     │
│ resourcemanager.xml → back_end_name="DISPLAY_PORT-RX"      │
│       │                                                     │
│       ▼                                                     │
│ backend_conf.xml → 48kHz, 2ch, 16bit                       │
│       │                                                     │
│       ▼                                                     │
│ 打开 ALSA PCM 设备 (hw:0, X)                                │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│                    内核 ALSA SoC 框架                         │
├─────────────────────────────────────────────────────────────┤
│ qcm6490.c (machine driver)                                  │
│   qcm6490_snd_init → 创建 "HDMI/DP1 Jack"                  │
│   qcm6490_be_hw_params_fixup → 强制 48kHz/2ch              │
│       │                                                     │
│       ▼                                                     │
│ hdmi-codec.c (codec 框架)                                    │
│   hdmi_codec_startup → get_eld → 设置 PCM 约束              │
│   hdmi_codec_hw_params → fill_codec_params                  │
│       │                                                     │
│       ▼                                                     │
│ lontium-lt9611uxc.c (桥片驱动)                               │
│   of_node → hdmi-audio-codec 共享节点                       │
│   hw_params → return 0 (自动检测)                            │
│   get_eld → connector.eld                                   │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│                    Q6 DSP 固件                               │
├─────────────────────────────────────────────────────────────┤
│ AFE 端口: DISPLAY_PORT_RX_1                                 │
│   → 配置 I2S 接口 (时钟、格式、声道)                          │
│   → 音频数据通过 SoC I2S 引脚发送                             │
└────────────────────────┬────────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────────┐
│                    硬件                                      │
├─────────────────────────────────────────────────────────────┤
│ SoC I2S 引脚 → LT9611UXC/UXD I2S 输入                      │
│   → LT9611 内部处理                                          │
│   → HDMI 输出到显示器                                         │
└─────────────────────────────────────────────────────────────┘
```

---

## 11. 已知问题与修复

### 问题 1: DP codec 抢占 HDMI codec

**现象:** PipeWire 崩溃，`hw_ep_info parsing failed HDMI_PORT-RX`

**原因:** DP 控制器驱动 (`dp_audio.c`) 也注册了 hdmi-codec 平台设备，但没有设置 `of_node`。ALSA SoC 框架 fallback 到 DAI 名字匹配时，先注册的 DP codec（空 ELD）被绑定到 HDMI DAI link。

**修复:** LT9611 驱动在 `audio_init` 中设置 `of_node` 指向 `hdmi-audio-codec` 共享节点。

### 问题 2: ELD 为空导致 -22 错误

**现象:** `snd_soc_dai_hw_params on i2s-hifi: -22`

**原因:** `hdmi_codec_get_ch_alloc_table_idx` 读取 ELD 的 speaker allocation，与请求的声道数不匹配时返回 `-EINVAL`。

**修复:** 确保 `get_eld` 返回有效的 ELD 数据，或在 ELD 为空时回退到立体声配置。

### 问题 3: hw_ep_info 解析失败

**现象:** `populate_hw_ep_intf: No matching intf found`，`parse_snd_card: 1005 hw_ep_info parsing failed HDMI_PORT-RX`

**根因:** `hw_ep_info` 不是来自 ACDB，而是来自**内核 DPCM 后端的 mixer 控制**。PAL 通过 mixer 控制名（如 `HDMI_PORT-RX rate ch fmt`）读取后端的硬件端点信息。如果内核没有为 `HDMI_PORT-RX` 创建 DPCM 后端 mixer 控制，PAL 就找不到 `hw_ep_info`。

**验证:** 在设备上执行 `amixer controls | grep -i "DISPLAY_PORT\|HDMI_PORT"`，如果两个都返回空，说明 DPCM 后端没有正确初始化。

**DPCM 后端 mixer 控制缺失的原因:** LT9611UXC 的 hdmi-codec 组件没有被 ALSA SoC 框架绑定到 DAI link。PCM 设备信息显示 `i2s-hifi`（默认名）而不是具体的 codec 名，说明 codec 绑定失败。

**注意:** 两个 DAI link 使用相同的 `link-name`（如都用 `DISPLAY_PORT-RX`）会导致 AGM 初始化失败：
```
device_init: 1112 no valid snd device found
session_obj_init: 1396 Error:-11 initializing device
```
内核 ALSA 框架不允许两个 DAI link 同名。

### 问题 4: Codec 绑定失败（待解决）

**现象:** LT9611UXC 的 hdmi-codec 设备 `of_node` 正确设置为 `/hdmi-audio-codec`，但 ALSA SoC 框架没有将其绑定到 HDMI DAI link。PCM 设备显示 `i2s-hifi-4`（默认 codec 名），而非 LT9611UXC codec 名。

**验证方法:**
```bash
# 检查 codec 的 of_node
cat /sys/devices/platform/soc@0/9c0000.geniqup/980000.i2c/i2c-0/0-002b/hdmi-audio-codec.4.auto/uevent
# 应显示 OF_FULLNAME=/hdmi-audio-codec

# 检查 PCM 设备的 codec 信息
cat /proc/asound/card0/pcm4p/info
# 如果 id 显示 "i2s-hifi-4" 而非具体 codec 名，说明绑定失败

# 检查 mixer 控制
amixer controls | grep -i "DISPLAY_PORT\|HDMI_PORT"
# 如果返回空，说明 DPCM 后端未初始化
```

**ALSA SoC 框架匹配逻辑:** `soc-core.c` 中的 `snd_soc_is_matching_component` 函数（行 825-851）通过指针比较 `component->dev->of_node` 和 `dlc->of_node`。`soc_component_to_node`（行 800-810）先检查 `component->dev->of_node`，为空则 fallback 到 `component->dev->parent->of_node`。

**调试方法:** 在 `snd_soc_is_matching_component` 中添加日志：
```c
if (dlc->of_node) {
    pr_info("snd_soc: matching component '%s' dlc_of_node=%pOF component_of_node=%pOF match=%d\n",
        component->name, dlc->of_node, component_of_node,
        component_of_node == dlc->of_node);
}
```

**可能原因:**
- hdmi-codec 组件注册时 `of_node` 尚未设置（时序问题）
- `of_node` 指针比较失败（不同实例）
- ALSA SoC 框架的 component 匹配逻辑有其他前置条件未满足

---

## 12. 调试经验总结

### hw_ep_info 来源

`hw_ep_info` **不是**来自 ACDB 二进制文件，而是来自内核 DPCM 后端的 mixer 控制。PAL 通过 mixer 控制名读取后端配置。验证方法：
```bash
amixer controls | grep -i "BACKEND_NAME"
```

### ACDB 切换验证

rb3gen2 的 ACDB（`QCS6490_RB3Gen2/acdb_cal.acdb`）也不包含 `HDMI_PORT-RX` 的 hw_ep_info。ACDB 切换命令：
```bash
cp /etc/acdbdata/QCS6490_RB3Gen2/acdb_cal.acdb /etc/acdbdata/qcm6490_idp/acdb_cal.acdb
cp /etc/acdbdata/QCS6490_RB3Gen2/workspaceFileXml.qwsp /etc/acdbdata/qcm6490_idp/workspaceFileXml.qwsp
```

### ACDB 来源

ACDB 文件来自高通 `audioreach-conf` 仓库：
```
SRCPROJECT = "git://git.codelinaro.org/clo/le/platform/vendor/qcom-opensource/audioreach-conf.git"
SRCBRANCH  = "audio-core.lnx.1.0.r1-rel"
```
Yocto recipe: `layers/meta-qcom-hwe/recipes-multimedia/audio/qcom-acdbdata_git.bb`

### PipeWire PAL 插件调试

PipeWire 启动时的关键日志：
```
parse_snd_card: 991 buffer: 00-04: HDMI_PORT-RX i2s-hifi-4 :  : playback 1
populate_hw_ep_intf: 184 No matching intf found      ← hw_ep_info 缺失
parse_snd_card: 1005 hw_ep_info parsing failed HDMI_PORT-RX
setDeviceMediaConfig: 1057: invalid mixer control: HDMI_PORT-RX  rate ch fmt  ← mixer 控制缺失
open: 586: invalid mixer control: HDMI_PORT-RX  metadata
pal_set_param: 1271: Failed to set global parameter 7, status -22
```

### Codec 绑定验证

检查 codec 是否绑定到 DAI link：
```bash
# 查看 PCM 设备的 codec 信息
cat /proc/asound/card0/pcm*/info | grep -i "id:"
# 成功: id: CODEC_DMA-LPAIF_RXTX-RX-0 multicodec-0
# 失败: id: HDMI_PORT-RX i2s-hifi-4  (显示默认 codec 名)

# 查看 jack 控制（jack 创建说明 DAI link 初始化成功）
dmesg | grep "HDMI/DP.*Jack"
# 成功: input: qcm6490-idp-snd-card HDMI/DP1 Jack

# 查看 codec 设备的 of_node
cat /sys/devices/platform/soc@0/9c0000.geniqup/980000.i2c/i2c-0/0-002b/hdmi-audio-codec.4.auto/uevent
# 应显示 OF_FULLNAME=/hdmi-audio-codec
```

### 三个 hdmi-codec 设备来源

| 设备 | 来源 | of_node | 用途 |
|---|---|---|---|
| `.2.auto` | eDP 控制器 (`aea0000.edp`) | 无 | eDP 音频 |
| `.3.auto` | DP 控制器 (`ae90000.displayport-controller`) | 无 | DP 音频（空 ELD） |
| `.4.auto` | LT9611UXC (`i2c-0/002b`) | `hdmi-audio-codec` | HDMI 音频 |

确认方法：
```bash
find /sys/devices -name "hdmi-audio-codec*" -type d
```

---

## 13. 调试过程关键发现

### 发现 1: `hw_ep_info` 来自内核 mixer 控制，不是 ACDB

PAL 框架的 `populate_hw_ep_intf` 函数通过读取内核 mixer 控制获取 `hw_ep_info`，而不是从 ACDB 文件读取。

**证据：** rb3gen2 的 ACDB 替换到我们的板子后，`HDMI_PORT-RX` 的 `hw_ep_info` 仍然解析失败。说明 ACDB 不是 `hw_ep_info` 的来源。

**mixer 控制来源：** 内核 ALSA SoC DPCM 框架在声卡注册时为每个后端创建 mixer 控制。如果后端的 codec 未正确绑定，mixer 控制不会被创建。

### 发现 2: DP 控制器也注册了 hdmi-codec 设备

DP 控制器驱动 (`dp_audio.c`) 也注册了一个 hdmi-codec 平台设备，但没有设置 `of_node`。

**三个 hdmi-codec 设备：**

| 设备 | 来源 | of_node |
|---|---|---|
| `.2.auto` | eDP 控制器 | 无 |
| `.3.auto` | DP 控制器 | 无 |
| `.4.auto` | LT9611UXC | `hdmi-audio-codec` |

DP 控制器的 codec 没有 `of_node`，不会匹配到 HDMI DAI link。但如果 LT9611UXC 的 `of_node` 也没设置，ALSA SoC 框架会 fallback 到 DAI 名字匹配，先注册的 DP codec 会被错误绑定。

### 发现 3: `DISPLAY_PORT_RX_1` 在 `q6afe_dai_prepare` 中未处理

`q6afe_dai_prepare` 函数的 switch 语句只处理 `DISPLAY_PORT_RX`（值 104 = `DISPLAY_PORT_RX_0`），不处理 `DISPLAY_PORT_RX_1`（值 129）。`DISPLAY_PORT_RX_1` 会走到 `default: return -EINVAL`。

```c
// q6afe-dai.c:372
switch (dai->id) {
case HDMI_RX:
case DISPLAY_PORT_RX:          // 只处理 DISPLAY_PORT_RX_0
    q6afe_hdmi_port_prepare(...);
    break;
// 没有 case DISPLAY_PORT_RX_1
default:
    return -EINVAL;
}
```

### 发现 4: QCS6490 的 I2S 引脚配置

QCS6490 的 Primary MI2S 引脚定义在 `qcm6490-addons-idp.dts` 中：

```
mi2s0_mclk  = gpio96  (MCLK)
mi2s0_sclk  = gpio97  (BCLK)
mi2s0_data0 = gpio98  (SD0 - I2S 数据)
mi2s0_data1 = gpio99  (SD1)
mi2s0_ws    = gpio100 (LRCLK)
```

LT9611 的 I2S 输入连接到 SD0（gpio98）。

### 发现 5: ACDB 拓扑的 `sd_line_idx` 问题

ACDB 拓扑中 `MI2S-LPAIF-RX-PRIMARY` 的 `sd_line_idx = 3`（SD3），但 QCS6490 的 Primary MI2S 只支持 SD0 和 SD1。Q6 DSP 拒绝了 SD3 的配置。

**AGM 日志证据：**
```
configure_i2s_ep: 422 i2s intf cfg lpaif 0 indx 0 sd_ln_idx 3 ws_src 0
gsl_graph_set_custom_config:1598 Graph set custom cfg failed 1
configure_i2s_ep: 429 custom_config for module failed with error -131
```

**修复：** 在 `audioreach.c` 中用 DTS 的 `qcom,sd-lines` 属性覆盖 ACDB 的 `sd_line_idx`。

### 发现 6: `msm_stub_codec` 的作用

`msm_stub_codec` 是高通提供的空壳 codec 驱动，提供基础 I2S DAI 接口但不配置任何硬件寄存器。用于 I2S 音频测试或不需要 codec 初始化的场景。

与 LT9611UXC 的区别：`msm_stub_codec` 没有 HDMI ELD、热插拔通知等功能。LT9611UXC 虽然也不配置 I2S 寄存器，但提供 HDMI 特有的功能。

### 发现 7: rb3gen2 的 ACDB 来源

ACDB 文件来自高通 `audioreach-conf` 仓库：
```
SRCPROJECT = "git://git.codelinaro.org/clo/le/platform/vendor/qcom-opensource/audioreach-conf.git"
SRCBRANCH  = "audio-core.lnx.1.0.r1-rel"
```

Yocto recipe: `layers/meta-qcom-hwe/recipes-multimedia/audio/qcom-acdbdata_git.bb`

安装的 ACDB 目录：
- `qcm6490_idp` — 我们使用的
- `QCS6490_RB3Gen2` — rb3gen2 使用的
- `qcs6490_rb3gen2_video` / `qcs6490_rb3gen2_vision` / `qcs6490_rb3gen2_ia` — 其他 rb3gen2 变体

---

## 14. 当前方案状态

### 已解决的问题
- ✅ Codec 绑定：UXC/UXD 的 `of_node` 设置为 `hdmi-audio-codec` 共享节点
- ✅ EDID 读取：LT9611UXC probe 中添加轮询等待（最多 5 秒）
- ✅ I2S 引脚配置：DTS 中添加 MI2S 引脚定义
- ✅ SD line 配置：`audioreach.c` 用 DTS 的 `sd_line_mask` 覆盖 ACDB 的 `sd_line_idx`

### 待解决的问题
- ❌ ACDB 拓扑的 `sd_line_idx` 需要验证 DTS 覆盖是否生效
- ❌ PipeWire/PAL 的 HDMI 音频路径需要端到端验证
- ❌ HDMI 热插拔音频切换需要测试

### 当前配置总结

| 文件 | 配置 |
|---|---|
| DTS HDMI DAI link | `MI2S-LPAIF-RX-PRIMARY` / `PRIMARY_MI2S_RX` / `hdmi_audio` |
| DTS I2S 引脚 | `mi2s0_data0/data1/mclk/sclk/ws` (gpio96-100) |
| DTS msm_stub_codec | `qcom,sd-lines = <0>` (SD0) |
| DTS hdmi_audio 节点 | `#sound-dai-cells = <0>` (of_node 锚点) |
| resourcemanager | `PAL_DEVICE_OUT_HDMI` → `MI2S-LPAIF-RX-PRIMARY` |
| backend_conf | `MI2S-LPAIF-RX-PRIMARY` 存在 |
| mixer_paths | `hdmi` 空 path |
| 内核 audioreach.c | `sd_line_mask` 覆盖 `sd_line_idx` |
