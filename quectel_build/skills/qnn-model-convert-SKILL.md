# 把模型转换到高通 NPU 上运行（QCS6490 / HTP v68）

本文件记录 Quectel PI H1（QCS6490，Hexagon HTP v68）上把一个新的 transformer
模型从 HuggingFace 权重跑成 NPU 推理的完整链路，以及每一步实际踩到的坑。

文中两条路线都从本仓库的实际操作里来：

- **路线 A：Genie（QAIRT GenAI Transformer）** —— 官方支持的那几种图，
  只改 JSON 就行，最快。
- **路线 B：ONNX Runtime + QNN EP（自编译）** —— Genie 不支持的架构（例如
  Qwen3.5 的 DeltaNet 混合注意力）走这条。

结论先写在前面：**决定成败的不是导出，而是量化。** QNN EP 在 HTP 上只接受
完全定点的图：从 graph input 到 graph output 中间不能夹着 fp32 段。图边界
还是 fp32（`quantize_static` 的默认产物就是这样）的话，HTP 的 partitioner
切不出一个完整的子图，所有节点都会落到 CPU，报
`backendValidateOpConfig() ... error 3110` / `Failed to finalize QNN graph`。

---

## 0. 先确认芯片和栈的能力

```bash
adb shell cat /sys/devices/soc0/machine
adb shell ls /dev/fastrpc-cdsp          # NPU 的 rpmsg 设备节点，缺了什么都跑不了
adb shell ls /usr/lib/libQnnHtp*        # 运行时库，版本决定支持哪些 op
adb shell strings /usr/lib/libQnnGenAiTransformer.so | grep -i version
```

QCS6490 的 HTP 是 v68，Genie 的 `graph_type` 只有
`gpt2 / gptj / llama / qwen`。模型架构不在这个列表里，Genie 就无解，
只能走路线 B。

判定模型能不能上，先看 `config.json` 的 `architectures` 和
`layer_types`。Qwen3.5 是 `Qwen3_5ForConditionalGeneration`，
24 层里 18 层是 `linear_attention`（Gated DeltaNet），6 层是
`full_attention`——这种混合结构 Genie 完全无法表达。

---

## 1. 环境

### 1.1 导出环境（跑在 PC 上）

Python 3.12，不要用 3.13（onnx/torch 的 wheel 覆盖不全）：

```bash
uv venv pyenv312 --python 3.12
uv pip install --python pyenv312/bin/python \
    torch --index-url https://download.pytorch.org/whl/cpu
uv pip install --python pyenv312/bin/python \
    onnx onnxruntime numpy safetensors "transformers>=4.57" accelerate
```

`transformers` 的版本必须能 `from_pretrained` 你要导出的架构。检查一句：

```bash
pyenv312/bin/python -c "import transformers as t; print([n for n in dir(t) if 'Qwen3_5' in n])"
```

### 1.2 板端运行环境

板端 Python 3.13，`onnxruntime` + `onnxruntime-qnn` 必须版本匹配
（本项目实测可用：ORT 1.27.0 + onnxruntime-qnn 2.3.0 + QAIRT 2.47）：

```bash
adb shell /root/q6a/venv/bin/python -c \
  "import onnxruntime as o, onnxruntime_qnn as q; print(o.__version__, q.get_qnn_htp_path())"
```

板端系统自带的 `python3` 通常没有 onnxruntime，所有脚本必须显式用 venv 里的
解释器。

---

## 2. 路线 A：Genie（架构被官方支持时）

只需要三样东西，放在同一个目录：

```
model_dir/
  <model>.bin          # 权重（量化后的 raw binary）
  <model>_htp.json     # 图描述：tensor 名、shape、量化参数
  tokenizer.json
```

跑之前库路径要分开指：

```bash
export LD_LIBRARY_PATH=/usr/lib/npu-llm
export ADSP_LIBRARY_PATH=/usr/lib/rfsa/adsp;/dsp
genie-t2t-run -c <model>_htp.json -p "hello"
```

坑：

- `libGenie.so` 找不到 —— `LD_LIBRARY_PATH` 没指 `/usr/lib/npu-llm`。
- `Unable to find a valid system interface` —— `libQnnSystem.so` 用错了版本。
  `adsp/` 目录里那个 212KB 的是 DSP 侧 skel 用的，平台侧要 5.3MB 的那个；
  两个目录必须分开，`LD_LIBRARY_PATH` 指平台库，`ADSP_LIBRARY_PATH` 首位指 skel。
- 换 QAIRT 版本只改一处：`layers/meta-qcom-qim-product-sdk/recipes-qcom-ml/qcom-ml.inc`
  里的 `QCOM_ML_VERSION` + `SRC_URI sha256`，`qcom-qnn-sdk.bb` 是通配安装。

---

## 3. 路线 B：自编译 ONNX → QNN context

### 3.1 把模型拆成"单 token decode 图 + prefill 图"

HTP 上跑的 LLM 引擎基本都长这样：decode 图每次吃一个 token，所有循环状态
（KV cache、线性注意力的 conv/recurrent state）作为**显式的 graph input /
graph output** 进出。

```python
state_spec = []
for i, t in enumerate(cfg.layer_types):
    if t == "linear_attention":
        state_spec.append((f"layer_{i:02d}_conv_state_in", [1, conv_dim, 3]))
        state_spec.append((f"layer_{i:02d}_rec_state_in", [1, n_v, k_dim, v_dim]))
    else:
        state_spec.append((f"layer_{i:02d}_past_k_in", [1, n_kv, ctx - 1, head_dim]))
        state_spec.append((f"layer_{i:02d}_past_v_in", [1, n_kv, ctx - 1, head_dim]))
```

要点，每一条都是踩出来的：

1. **状态张量名必须 input 用 `_in`、output 用 `_out`**。同名的话
   `torch.onnx.export` 会静默改名成 `xxx.1`，量化脚本按名字喂数据就对不上。
2. **KV cache 用定长滑动窗口**。图内 `k_all = cat(zeros(1, n_kv, ctx-1, hd), k)`，
   输出 `k_all[:, :, 1:]`——丢掉最老的一格，这样 output 的形状和下一步的
   input 一致，可以原地回灌。否则每步都要在 Python 里重新拼。
3. **prefill 灌状态时尾部对齐**：`kp[:, :, -len:] = k`，不是 `kp[:, :, :len] = k`。
4. **RMSNorm 用 `weight` 直接乘，不是 `(1 + weight)`**。Qwen 系全部如此，
   写成 `1+w` 会导致 logits 完全对不上而你还以为是量化问题。
5. **DeltaNet 的 16 个 value head 批成 3-D matmul**，否则图里会有 16 倍的
   MatMul 节点，HTP 上的调度开销很难看。

### 3.2 数值对齐：必须做闭环，不能只看单步

单步 logits 对齐是**不够**的——状态漂移会被掩盖。正确的做法是只用自己的图
递推 N 步，和 `model.generate(do_sample=False)` 逐 token 比：

```python
# 先用 transformers 做一次 prefill 拿初始 state
out = inner(input_ids=ids, past_key_values=DynamicCache(config=cfg), use_cache=True)
# 然后完全用自己的图循环
for step in range(steps):
    got = graph(h, cos, sin, mask, *states)
    cur = int(got[0][0, 0].argmax())
    states = list(got[1:])
```

16/16 token 一致才算图对。本项目 2B 的实测结果：

```
2b_decode_fp32.onnx closed loop: 16/16 tokens match greedy
```

单步对齐通过、闭环不对，基本都是 mask 的窗口对齐或者 RoPE 位置算错：

- mask 在 `ctx` 定长窗口下要**尾部对齐**：
  `valid = min(pos, ctx-1) + 1; mask[..., ctx-valid:] = 0`。
  写成 `mask[..., :pos] = 0` 在窗口没满的时候全错。
- RoPE 单位置表的维度：`torch.cos(f)` 已经是 `[1, dim/2]`，
  再 `[None, None, None, :]` 会多一维，要先 `[0]` 取出来。
- 闭环 off-by-one：prefill 那一步的预测也算一个 token，
  `generated = [cur]` 然后 `range(steps - 1)`。

### 3.3 量化——这一步决定成败

#### 量化在做什么

HTP 只跑定点数。所谓"量化"，就是把 fp32 图改写成定点图：

    真实值 x  ≈  (q - zero_point) × scale

- `q`：整数，存的是权重或激活；
- `scale`：一个 fp32 标量（per-channel 时是每个输出通道一个），
  决定这一格整数代表多大的实数值；
- `zero_point`：整数，代表"真实的 0"落在哪一格。

一个量化点在图里表现为一对算子：

    QuantizeLinear(x, scale, zp)       fp32 -> int
    DequantizeLinear(q, scale, zp)     int  -> fp32

夹在中间的那段（MatMul / Conv）就会被 HTP 直接吃掉成定点的，
两端还是 fp32 —— 这就是 **QDQ 格式**。QNN EP 认的就是这个形态。

所以量化要回答三个问题：

1. **量化谁**：权重全都量化；激活量化哪些层（`op_types_to_quantize`）。
2. **用多少位**：权重几位、激活几位。这两者独立！
   本项目参考图实测是 **A16W8**（激活 uint16、权重 uint8）。
3. **scale 取多少**：这是唯一影响输出质量的参数，靠**标定**决定。

第 3 条是全流程里唯一"有技术含量"的部分，其余都是格式活。

#### 权重：静态量化，per-channel

权重是常量，直接按数值分布算，不需要跑数据：

```python
w = weight.float()                       # [out, in]
s = w.abs().amax(dim=1) / 127            # 每个输出通道一个 scale
q = clip(round(w / s[:, None]), -127, 127).to(torch.int8)
```

per-channel（`axis=1`，即输出通道各一个 scale）几乎不损失精度，
而且 HTP 原生支持。**per-tensor 会明显掉点**，别省这一步。

#### 激活：必须用真实数据标定

激活的分布取决于输入。随机数据标出来的 scale 毫无意义——
尤其这个模型有 DeltaNet，它的 `conv_state` / `rec_state` 数值范围
随序列长度单调增长，拿短 prompt 标定会把状态钳死（钳死 = 溢出后被
clamp 到端点，信息全丢）。

正确做法：**用模型自己的前向产生校准数据**，把每一步 decode 的
真实输入喂进去。

```python
# quantize_2b.py 的核心：让 torch 参考模型走闭环，产出真实 feed
def iter_steps(model, graph, tok, prompt, ctx, steps):
    ids = tok(prompt, return_tensors="pt").input_ids
    out = inner(input_ids=ids, past_key_values=DynamicCache(config=cfg), use_cache=True)
    cur = int(model.lm_head(out.last_hidden_state[:, -1:])[0, 0].argmax())
    states = _initial_states(cache, graph, ctx)
    for step in range(steps):
        pos = n + step
        feed = {
            "hidden_in":  inner.embed_tokens(torch.tensor([[cur]])).numpy(),
            "rope_cos":   cos.numpy(),
            "rope_sin":   sin.numpy(),
            "attn_mask":  mask.numpy(),
            **{name: st.numpy() for (name, _), st in zip(graph.state_spec(ctx), states)},
        }
        yield feed                      # <- 这一步的输入就是校准样本
        got = graph(...)                # 用图自己的输出推下一步
        states = list(got[1:])
```

关键是最后两行：**校准样本必须来自闭环递推**，不能只跑 prefill 然后
拿同一份状态复制 N 份。状态是会漂的，漂到哪标到哪，scale 才对。

喂给 ORT：

```python
quantize_static(
    model_input="2b_decode_fp32.onnx",
    model_output="2b_decode_w8.onnx",
    calibration_data_reader=Calib(),          # 上面那个 generator
    quant_format=QuantFormat.QDQ,
    activation_type=QuantType.QUInt8,
    weight_type=QuantType.QUInt8,
    per_channel=True,
    op_types_to_quantize=["MatMul"],
    extra_options={"ActivationSymmetric": False, "WeightSymmetric": True,
                   "CalibMethod": "MinMax"},
    use_external_data_format=True,
)
```

`CalibMethod: MinMax` 就是"取标定过程中见到的 min/max 当范围"。
简单、对长尾敏感：一条异常大的激活就把整个 scale 撑大，
其余数值全挤在中间几格里。

#### 两个必须知道的坑

**坑一：不要跑 `quant_pre_process`。**
它做一遍 shape inference 再把整图落盘。2B 的图带 external data 有 7.5 GB，
往返一次直接把内存吃穿或者撞 protobuf 上限，产物写不出来。
图里所有 shape 都是静态的（导出时就定死了），本来也不需要它。
直接对原图 `quantize_static` 即可。

**坑二：8-bit 激活对这类模型不够。**
实测（`run_qdq_cpu.py` 闭环 16 步）：

    fp32 : ' Paris.\nA. True\nB. False\n\n<think>\nThinking Process:'
    w8   : ' Parisetaletaletaletaletaletaletaletaletaletal...'

第一个 token 对，第二个 token 开始退化成重复。原因不是权重精度，
是**状态张量被 8-bit 量化毁掉了**：`conv_state` / `rec_state` 里存的
是累加器，误差会跨 token 累积，8-bit 的 1/255 分辨率不够。

#### 解法：把激活提到 16 位（A16W8）

HTP 上"权重 8 位"决定带宽，"激活 16 位"几乎不花时间（激活量比权重小
两个数量级）。所以最划算的配置是 **权重 uint8 + 激活 uint16**。

不用重新标定——直接把已有的 uint8 QDQ 图升格，数值范围保持不变，
只是把 255 格换成 65535 格：

```python
lo = -zp * scale                       # 原来能表示的最小实数
hi = (255 - zp) * scale                # 原来能表示的最大实数
s16 = (hi - lo) / 65535
z16 = round(-lo / s16)                 # 让 0 点在同样的位置
```

`u16ify.py` 就是这个（本项目 856 个激活 Q/DQ 对全部升格）。
升格后闭环输出：

    a16w8: ' Paris.\nA. True\nB. False\n\n<think>\nThinking Process:'

**和 fp32 逐字一致**。质量关过了。

两个格式细节，都会直接报 `INVALID_GRAPH`：

- 16 位 zero_point 需要 **opset ≥ 19**（本项目升到 21）；
- `ReduceMean` 的 `axes` 在 opset 18 从属性变成了**输入**，
  79 个节点必须一起改，否则报
  `Error Unrecognized attribute: axes for operator ReduceMean`。

#### 哪些常量能升 16 位：只有"权重"不能

升格时最容易写错的一行是"被量化的常量一律留 8 位"。**错。** 参考 0.8B
图里常量的位宽是有规律的：

    算子        数量    输入组合
    Mul        4089    Q16 × Q16        ← 标量常量也是 u16
    Add         664    Q16 × Q16
    Concat     1587    Q16 × Q16
    Sub         544    Q16 × Q16
    MatMul     1033    Q16 × Q8         ← 只有它和 Conv 的权重是 u8
    Conv        864    Q16 × Q8

也就是说 **8-bit 是给权重的，不是给"常量"的**。判断标准不是"是不是
initializer"，而是"是不是被 `MatMul`/`Gemm`/`Conv` 当第二个输入吃进去"：

```python
WEIGHT = {"MatMul", "Gemm", "Conv", "ConvTranspose"}
is_weight = set()
for n in graph.node:
    if n.op_type in WEIGHT:
        is_weight.update(n.input[1:])
```

漏掉这一条会生成 `Q16 × Q8` 的 `ElementWiseMultiply` / `ElementWiseAdd` /
`Concat`，HTP 一律 3110。本项目实测：`Mul` 里有 97 个这种、`Add` 18 个、
`Concat` 230 个 —— 全是混精度，不是算子不支持。

两个连带修改：

1. 常量数据本身要跟着换位宽。`s16 = s8/257`、`zp16 = zp8*257`，
   所以同一个实数值在新格点上是精确的 `257 * v8`（uint16 乘法，无损）。
   不改就会让 `DequantizeLinear` 拿 uint8 数据配 uint16 zero point —— 非法 ONNX。
2. **per-channel（带 `axis` 属性）的 Q/DQ 永远是权重**。参考图里 per-channel
   只有 u8。本项目有 2 个 `MatMul` 的权重是 `Transpose(constant)` 后的计算
   张量，按"是不是 initializer"判断会漏，按 `axis` 判断才抓住。漏了它们
   就是 2 条 `FullyConnected` 3110。

#### HTP 唯一不接受的 unary：Softplus

用 `gen_probe_unary.py` 把 8 个 unary 逐个编（u16 输入输出，判据是 bin 体积）：

    Softplus      errors=1  bin=0      ← 唯一被拒
    Sigmoid       errors=0  bin=743
    Tanh          errors=0  bin=738
    Log           errors=0  bin=739
    Exp           errors=0  bin=739
    Neg           errors=0  bin=739
    Sqrt          errors=0  bin=740
    Reciprocal    errors=0  bin=744

所以 `Pow` / `Sqrt` / `Exp` / `Neg` / `Div` 这些**根本不是问题**，之前把它们
列成"死路算子"是被 NaN scale 误导了。`Abs` / `Relu` / `Max` 单独编也都过
（`errors=0`，bin 737/740/766 B）。但——**单独能编不代表放进图里能编**。

这条是本文里最反直觉的一条：`quantize_static` 只给 **它认识的算子** 插
Q/DQ。`Abs` / `Neg` / `Max` 不在 ORT 的 QDQ 注册表里（`Sub` 也不在），
所以它们留在 fp32；QNN 见到 fp32 输入就是 3110。实测：

    Max(x,0) + Log(1+Exp(-Abs(x)))     errors=146
      ElementWiseAbs 36 / ElementWiseNeg 36 / ElementWiseExp 36 /
      ElementWiseMaximum 36            ← 全是这条链的 4 个算子

判断办法：数一下每个算子在量化图里有几个节点是"输入全是 DQ 或常量"：

```python
tot, q = Counter(), Counter()
for n in g.node:
    if n.op_type in ("QuantizeLinear", "DequantizeLinear", "Constant"):
        continue
    tot[n.op_type] += 1
    if all(is_dequant(n, i) or i in inits for i in n.input if i):
        q[n.op_type] += 1
# Sigmoid 84/84, Log 18/18, MatMul 235/235  -> 会插 Q/DQ
# Neg 36/48, Sub 18/18, Abs/Max            -> 不会，留在 fp32
```

所以改写 Softplus 必须只挑**注册表里有的算子**：

    softplus(x) = x - Log(Sigmoid(x))        ← 用这个

`-Log(Sigmoid(x))` 就是 `Log(1 + Exp(-x))`，所以恒等：

    x = 0    -> 0 - log(0.5)      = 0.693
    x = -20  -> -20 - (-20 + eps) = 0

中间量全部有界：`Sigmoid(x) ∈ (0,1)`，`Log` 的结果落在 `(x_min, 0)`——对负
的 x 它就跟着 x 走，不会爆。`Sigmoid` / `Log` / `Mul` / `Add` 全在注册表里，
一个 fp32 孤岛都不留。

**不要**用 `Sub`（不在注册表），用 `Add` 配一个乘 `-1`；也不要用直觉的

    softplus(x) = Log(1 + Exp(x))          # 能编，但质量崩
    fp32 基准       ' Paris.\nA. True\nB. False\n\n\nThinking Process:'
    Log(1+Exp)      ' Paris.\nThe capital of France is London.\n...'

`Exp(x)` 输出跨到 `e^max`，标定后那个 `QuantizeLinear` 把格点全花在模型用
不到的大值上。**规律**：把一个不支持的算子拆成支持的算子时，不能只看数学
等价，还要看两件事——算子是否在量化器的注册表里，以及中间量的动态范围。




#### 验证：闭环，不是 logits 误差

```bash
# 质量
pyenv312/bin/python run_qdq_cpu.py --onnx 2b_decode_fp32.onnx 2b_a16.onnx --steps 16
# 体积 / 是否真量化
pyenv312/bin/python qprobe.py 2b_a16.onnx     # 数 int8 元素个数
```

`qprobe` 这步别省：本项目一开始按"const 276MB"算带宽，算出来 2.03 GB/s
对不上实测延迟；数了一遍才发现图里有 **752M 个 int8 元素**（= W8 无疑），
真实带宽 5.3 GB/s。**结论：带宽必须由图里的实际元素数算，不能看文件大小**
（external data 有压缩、有对齐、有共享）。

#### 当前状态（未解决）

A16W8 图（只量化 MatMul）在 CPU 上质量完美，但**在板子上编不过**：

    backendValidateOpConfig() failed for node ... error code 3110   × 2678
    Failed to finalize QNN graph. Error code: 1002

纯 W8 图同样编不过（同样是 2678 个节点被拒）。而第三方那张 0.8B 图
（结构同源、也是 QDQ）是能编的，说明不是"架构不支持"。

怀疑是 `op_types_to_quantize=["MatMul"]` 造成的**混合图**问题：
ORT 只在 MatMul 周围插 QDQ，DeltaNet 里的 Div/Sqrt/Exp/Pow/Softmax
仍是 fp32，于是图变成定点段与浮点段交错，HTP 的 partitioner 切不开。

实测把三张图的 IO dtype 数出来，根因就清楚了：

    图                     inputs                     outputs
    REF 0.8B（能编）       578 = 556 UINT16 + 22 FLOAT  同上
    2B w8（MatMul only）    52 = 全 FLOAT                全 FLOAT
    2B a16 all-ops          52 = 全 FLOAT                全 FLOAT

**能编的那张图，96% 的输入本身就是 uint16。** 我的两张图是 100% fp32
边界。`quantize_static` 的产物永远是"fp32 进、fp32 出，量化只在图内"，
HTP 的 partitioner 需要一个从输入到输出都定点的子图，切不出来就整图
落回 CPU 并报 3110。跟权重是 4 位还是 8 位、量化了哪些算子都无关
（`probe_bits.py` 的四个变体 W8/W4blk32/W4blk64/W4tensor 报错一模一样）。

只量化 MatMul 的图里，fp32 孤岛是**具体可见**的：

    graph inputs: 52, 边界违规: 33
      layer_19_past_v_in   -> Concat     （没有 Q/DQ，直接进 Concat）
      layer_11_past_v_in   -> Concat
      layer_08_conv_state_in -> Concat
      ...
    outputs 不来自 DQ: layer_00_conv_state_out <- Slice, layer_03_past_k_out <- Slice ...

也就是说 **33 个 state 缓存（past_k/past_v/conv_state）根本没被量化**，
它们在 fp32 里穿过 HTP 边界，而这些张量正是 DeltaNet 每步都要读写的。
`Concat` / `Slice` 这两个算子把 state 拼接和滑窗切片包住，ORT 的
QDQ 插入规则不给它们插量化节点，于是图里同时存在"全定点主干"和
"33 条 fp32 回路"，partitioner 无法生成合法的 HTP 子图。

两条修法，都不需要重新导出：

1. ORT 侧开 `offload_graph_io_quantization=1`（`probe_boundary.py` 的
   `b-fp32-io1` 测这个）。实测该键只存在于 `libonnxruntime_providers_qnn.so`，
   且默认就开着，**不是解**。
2. 直接在 ONNX 里改边界：每个「graph input → QuantizeLinear」把该 Q 的
   输出提升为新的 graph input、删掉那个 Q；输出侧对称处理。
   `io_quantize.py` 实现了这条，见 3.5。

已试过整图全量化（`--all-ops`，去掉 `op_types_to_quantize` 限制）：

    nodes 8465  (DQ 2564 / Q 2262 / Constant 1139 / Mul 452 / Cast 296)

质量代价明显：

    all-ops w8 : ' Parisa..." , and and easilynotnotly,lylyly'

因为 `Softmax`、`Sqrt`、`Div` 这些对量化极敏感的算子也被压到 8-bit 了。
all-ops **确实消掉了那 33 条 fp32 回路**（改写后输入输出 100% 定点，
`inputs_not_DQ=0`、`outputs_from_Slice=0`），代价是精度。所以更稳的路线是
"定点边界 + 手工算子白名单"，而不是"全量化图硬扛"。

#### 根因（后续实测修正）：HTP 上 fp32 算子一个都过不了

上面"定点边界"的判断只对了一半。把 2486 条 3110 按节点统计出来：

    Pow 115/115   Sqrt 115/115   Div 121/121   ReduceMean 79/79
    Mul 325/452   Add 217/223    （分母=图里的节点总数）

**全灭的都是留在 fp32 的那些。** u8 图和 u16 图死在同一批节点上，说明
跟位宽无关；`enable_htp_fp16_precision=1` 加了以后日志一条没变，也无效。
HTP 没有 fp32 计算通路，留 fp32 的算子不会"自动降到 CPU"——在
`session.disable_cpu_ep_fallback=1` 下直接被 validation 拒掉。

对照实验：手写的 9 节点小图（`gen_probe.py`，u8 / u16 / i16 三种）里
**只有 u8 是真编译成功**（77824 B）。u16 / i16 那两张当时看着"也出了 bin"，
后来复查日志才发现它们照样报了 3110，而且是这两个算子：

    QNN.backendValidateOpConfig() failed for node `/MatMulAddFusionFullyConnected`
        of type `FullyConnected` with error code 3110
    QNN.backendValidateOpConfig() failed for node `Softmax5`
        of type `Softmax` with error code 3110

写出来的 `probe_u16_z32768.bin` 只有 **1274 B**、`probe_i16_z0.bin` **1272 B**
—— 相比 u8 的 77824 B，这是**空壳**，不是编译产物。

**判定标准必须是 `.bin` 的体积，不是它存不存在。** 被拒的图也会留下一个
1 KB 级 stub，`ls` 看着一切正常。这条坑值得单独记：之前就是被它骗了一轮，
误以为工具链对 16 bit 也没问题。

推论：u16 定点在这个 HTP 栈上不是"能用但慢"，而是 `FullyConnected` 和
`Softmax` 直接 validate 不过。参考 0.8B 图里那 556 个 UINT16 输入因此**不是**
ORT QNN EP 这套 u16 QDQ 语义，八成是 fp16 位模式（或者是 QAIRT converter
直编出来的、u16 另有映射）。要验证就把 dtype × op 拆开单测：
`gen_probe_mat.py` 生成六张单算子小图
（`u8_mm` / `u16_mm` / `u16_mm_sm` / `u16_mul` / `u16_lp` / `f16_mm`），
一次跑完就知道到底是 u16 整体不行、还是只有某几个算子不行。

`f16_mm`（纯 FLOAT16、无 QDQ）是这里的对照组：HTP v68 原生支持 fp16，
如果这张能编，那整场定点转换都是白折腾，直接把图转 fp16 就行——
代价是权重从 1.9G 涨到 3.8G，板子 7.3G 内存不一定吃得下。

再看第三方那张能编的 0.8B 图用了什么算子：

    Mul 4089  Slice 3019  Transpose 1758  Unsqueeze 1708  Concat 1655
    ReduceSum 1088  MatMul 1045  Sigmoid 930  Conv 864  Reshape 720
    Add 664  LpNormalization 655  Squeeze 576  Sub 544  Softmax 6
    Pow 0   Sqrt 0   Div 0   ReduceMean 0

**一个 `Pow` / `Sqrt` / `Div` / `ReduceMean` 都没有。** 它的 RMSNorm 是这么写的：

    Concat([x, sqrt(n*eps)])  ->  LpNormalization(p=2, axis=-1)
      ->  Slice(去掉尾巴)     ->  Mul(w * sqrt(n))

因为 `x/rms(x) = x*sqrt(n)/||[x, sqrt(n*eps)]||`，所以归一化本身用
`LpNormalization` 表达、常数 `sqrt(n)` 折进权重的 Mul 里，一次除法都不用
（`src/export_2b.py` 的 `rms_zero_centered` / `rms_gated` / `l2norm` 正好相反，
用的是 `rsqrt(pow(2).mean())`，导出成 Pow/ReduceMean/Sqrt/Div 四件套）。
softplus 也是 `Log`/`Exp`/`Neg` 拼的，不用 `Softplus`。

所以结论修正为：**不是"边界定点就行"，而是整图不能有任何 HTP 不接受的
fp32 算子**。两条路——

1. 保留现在的导出，把 `Div` / `ReduceMean` / `ReduceSum` / `Exp` / `Neg`
   这几类剩下的 fp32 节点补上 QDQ（ORT 的 QuantizationRegistry 不管它们，
   得自己在图上插）；
2. 改导出，把 norm 换成 `LpNormalization` 形态，让全图只出现在参考图的
   算子白名单里。

`gen_probe_norm.py` 生成两张对照小图（`probe_rms.onnx` = Pow/ReduceMean/
Sqrt/Div 形态，`probe_lpnorm.onnx` = LpNormalization 形态），两者在 u16 下
数值一致（`max|d| = 0.0023`，就是 u16 量化本身的误差），编一次就知道
路线 1 是否可行：`probe_rms` 能出 `.bin` 就不用改导出。

#### 工具：`norm_rewrite.py`（路线 2，已实现并验证）

    python norm_rewrite.py --onnx 2b_decode_fp32.onnx --out 2b_decode_lp.onnx

在 **fp32 图**上跑，匹配这个形状并整体替换：

    Mul( X, Div(1, Sqrt(Add(ReduceMean(Pow(X,2)), eps))) )      # rsqrt 惯用法
  ->
    Slice( LpNormalization( Concat([X, sqrt(n*eps)]) ), 0..n ) * sqrt(n)

推导：`x/sqrt(mean(x²)+eps) == x*sqrt(n)/||[x, sqrt(n*eps)]||`。
`ReduceSum` 版本（l2norm）同理，但 eps 不用乘 n、外提系数 `sqrt(n)` 取 1。

三个实现坑：

1. `ReduceMean` 在 opset 17 是 attribute、`ReduceSum` 在 opset 13+ 是 **input**，
   取轴要两条路都读；
2. 形状推断对穿过 `Slice(ends 是运行时张量)` 的 `Pow` 只给 `[0,0,0]`。
   这种站点只有全注意力层的 `q_norm`（6 个），head_dim 恒为 256，直接兜底；
3. `Concat` 不做广播：eps 常量的形状必须是 `base.shape[:-1] + (1,)`，
   给 `(1,1)` 会报 `All inputs to Concat must have same rank` 或
   `Can't merge shape info`。替换完还要做一次拓扑排序（新节点在末尾，
   消费者在前面）。

改写后整图 `Pow/Sqrt/ReduceMean/ReduceSum` 归零、`Div` 从 121 降到 6。
用 `cmp_lp.py` 锁步比对（同一串 token、各自独立状态链）：

    每步 max|dlogits| = 1.7e-05 ... 1.0e-04
    top-1 一致 16/16

1e-4 是浮点重排的量级，不是改写错。自由生成时可能在近乎平局的地方翻一个
token，这是可接受的。

**注意**：`--ops MatMul,Concat,Slice` 的 whitelist 在新图上会把新加的
`Concat` 一起量化，于是 `LpNormalization` 拿到的是 `DequantizeLinear` 输出
—— 这正好和参考图的形态一致（参考图 655 个 `LpNormalization` 全都有 DQ 输入）。

#### 最隐蔽的坑：校准输入的 `-inf` 掩码会毒掉 scale

先说清楚一件事：**这个坑不会导致 3110**，但它会悄悄毁掉质量，而且
`u16ify` 能一路跑完、CPU 也能出字，所以极难发现。

`--all-ops` 全量化之后，图里有 **13 个 Q/DQ 的 scale 初始化器是 NaN**：

    标定用的 attn_mask = -inf
      -> Add(scores, -inf) 的观察范围变成 [-inf, 0]
      -> scale = (hi-lo)/255 = inf -> NaN

后果不是编译失败，是**质量退化**：

    fp32 基准             ' Paris.\nA. True\nB. False\n\n\nThinking Process:'
    all-ops A16（-inf）   ' Paris. Paris is in France. Paris is in France.\n1. Paris'
    all-ops A16（-64）    ' Paris.\nA. True\nB. False\n\n\nThinking Process:'

看起来像"全量化精度不行"，其实是被一个 NaN 拖的。排查口诀：**先扫 scale，
再看算子**：

```python
import onnx, numpy as np
from onnx import numpy_helper
m = onnx.load("graph.onnx", load_external_data=False)
inits = {i.name: i for i in m.graph.initializer}
for n in m.graph.node:
    if n.op_type not in ("QuantizeLinear", "DequantizeLinear"):
        continue
    t = inits.get(n.input[1])
    if t is None:
        continue
    a = numpy_helper.to_array(t).astype(np.float64).ravel()
    if (a <= 0).any() or not np.isfinite(a).all():
        print("BAD", n.name, n.input[0], a[:3])
```

正确做法：掩码用**有限负数**，且取参考图实际用的值。参考图的
`tail_mask` 量化范围是 `[-64, 0]`（scale 9.77e-4、zp 65535），所以标定和
推理都该用 `-64`。用 `-1e4` 也不会 NaN，但会把 scores 那一段的分辨率压到
0.15、softmax 直接失真。

对应地给 `u16ify.py` 加了硬检查：promote 时遇到非有限或 ≤0 的 scale 直接
`SystemExit`，把"图能生成、CPU 能跑、上板静默 0 字节"这条路封死。





顺带记一条环境坑：在 Hermes/agent 的临时 cgroup 里跑整图量化会
**OOM 被杀**（`Memory cgroup out of memory`，上限约 8.5G）。
绕过办法：

```bash
systemd-run --user --scope --unit=quant2b -p MemoryMax=40G \
    env VIRTUAL_ENV=$PWD/pyenv312 $PWD/pyenv312/bin/python quantize_2b.py ...
```

### 3.4 编成 QNN context binary

不需要 x86 的 `qairt-converter`（QAIRT 包里通常只有 aarch64 运行时）。
在板子上用 ORT 现场编译：

```python
options = ort.SessionOptions()
options.add_session_config_entry("ep.context_enable", "1")
options.add_session_config_entry("ep.context_file_path", "model_ctx.bin")
options.add_session_config_entry("ep.context_embed_mode", "0")   # 不把权重塞进 onnx
options.add_session_config_entry("session.disable_cpu_ep_fallback", "1")
provider = {"backend_path": str(oq.get_qnn_htp_path()),
            "offload_graph_io_quantization": "0",
            "htp_arch": "68"}
options.add_provider_for_devices(devices, provider)
session = ort.InferenceSession(model_path, sess_options=options, enable_fallback=False)
```

编译一次很久（0.8B 约 20 分钟，2B 更久），产物是 `.bin`。
之后运行时只加载 `.bin`，启动就是秒级。

**检查是否真的在 NPU 上**：

```bash
adb shell dumpsys ...        # 或者看 profile 里每个 node 的 provider
```
`session.disable_cpu_ep_fallback=1` 本身就是最硬的证据——只要有节点落到 CPU，
session 创建直接抛异常。

### 3.5 把图边界改成定点（io_quantize.py）

`quantize_static` 只在图**内部**插 QDQ，边界永远是 fp32。HTP 需要的是
"输入进来就是 u8/u16、输出出去还是 u8/u16"的子图，否则切不出合法分区。
`io_quantize.py` 把最外层那一对 Q/DQ 翻到边界外：

    fp32 in -> Q -> DQ -> ...internal... -> Q -> DQ -> fp32 out     （原）
    u16  in       ->       ...internal...       ->       u16 out     （改后）

```bash
python io_quantize.py --onnx 2b_a16_all.onnx --out 2b_a16_all_io.onnx
# inputs  hoisted  52   left fp32   0
# outputs hoisted  49   left fp32   0
#   input dtypes : [4]      # UINT16
#   output dtypes: [4]
```

数值必须不变，用 `check_io.py` 验（喂原图内部量化后的同一批整数，
再用原图的 scale 反量化整数输出，和原图 fp32 输出比）：

```bash
python check_io.py --orig 2b_a16_all.onnx --io 2b_a16_all_io.onnx
# compared 49 outputs, max |diff| = 0.00000     OK
```

零误差是预期的：搬边界不动数值，下游位级一致。

改写后会多出一个 `<out>.io_quant.json`，记着每个边界张量的 `scale` /
`zero_point` / 原张量名。**这个是运行时必需品，不是测试夹具**：图边界
变成整数后，调用方要自己做最后一对 Q/DQ——喂进去的 hidden state 得先
量化，输出的 logits 得反量化。省掉它就得从图里反查 initializer，很烦。

质量实测（同一 prompt `The capital of France is`，16 步 greedy，CPU EP）：

    fp32 基准                : ' Paris.\nA. True\nB. False\n\n<think>\nThinking Process:'
    MatMul+Concat+Slice A16  : ' Paris.\nA. True\nB. False\n\n<think>\nThinking Process:'   ← 逐 token 一致
    MatMul+Concat+Slice W8   : ' Paris. \nThe\n:\nThe\nThe\nThe\nThe\n'   ← 重复
    all-ops A16              : ' Paris, which is located in the ________.\n<think>\n<think>...'
    all-ops W8               : ' Parisa..." , and and easilynotnotly,lylyly'

结论很清楚：

- **激活必须 16-bit**。同一张图换成 uint8 激活立刻退化成重复。
  权重可以保持 8-bit，也就是 A16W8。
- **算子白名单 `MatMul,Concat,Slice` 就够了**。不需要 all-ops：
  把 `Softmax`/`Div`/`Sqrt` 压到定点反而更差，而 `Concat`/`Slice`
  这两个必须量化——它们正是 state 回路（past_k/past_v/conv_state）
  的搬运工，不量化就留下 33 条 fp32 旁路。
- 这个配置下 16 步 greedy 与 fp32 **逐 token 相同**，可以直接当基准用。

三个实现细节，踩过才知道：

1. **`Cast` 要连删**。`hidden_in` 前面有个 FLOAT→FLOAT 的空 Cast
   （`Cast to=1`），提升 Q 时要把它一起标死，否则新图输入悬空，
   ORT 报 `Node input 'hidden_in' is not a graph input`。
2. **输出侧的 DQ 不能无脑删**。state 输出（`layer_00_conv_state_out`）
   的 DQ 输出还被图内节点消费，只能改 graph output 指向 Q 的输出，
   DQ 本身留着。
3. **uint16 需要 opset ≥ 19**，`u16ify.py` 已经升到 21，别退回 17。
   另外 ORT 1.27（板端）是否认 opset 21 的 uint16 `QuantizeLinear`
   还没验，本地 1.31 没问题。

验收标准就两条：改写后 `input dtypes` / `output dtypes` 不含 FLOAT，
且 `check_io.py` 的 `max|diff|` 为 0。

注意 **fp32 边界输入是允许的**，别追求 100% 定点。参考的 0.8B 图就是
578 入里 22 个 FLOAT。`layer_XX_rec_state_in` 和 RoPE/mask 这类张量
留在 fp32 完全没问题，只要 state 搬运链路（Concat/Slice）是定点的。

### 3.6 实测性能（QCS6490，0.8B）

| 指标 | 数值 |
|---|---|
| decode | 6.7 tok/s（约 150 ms/token） |
| prefill | 76 tok/s |
| TTFT | 1.1–3.0 s |
| context bin | 807 MB，25 张图 |
| 常驻内存 | 约 5.2 GB / 7.3 GB |

瓶颈是 DDR 带宽，不是算力：每 token 要把全部权重从 DDR 读一遍，
实测约 1.9 GB/s 有效带宽。所以 `tok/s ≈ 1.9 / 权重体积(GB)`。
2B 的 W8 权重约 690MB → 预期 2.8 tok/s；W4 约 345MB → 预期 5.5 tok/s。
但 W4 同样要过 3.3 的量化质量关。

### 3.7 大一点的模型（2B）：HTP 的 state 精度是硬墙（未解）

0.8B 那套「state 边界全部提升成 u16」搬到 2B 上，图能编、context 能加载、
HTP 能跑出 3.4 tok/s，但**输出是乱的**。同一份图在 CPU EP 上和 torch 逐 token
完全一致（10/10），所以喂数没错，是 HTP 侧丢精度。

定位手法（可复用）：单 token、pos 0，把 logits 存 npy，CPU / HTP 各跑一次算
余弦；再把全部 state 填同一个 u16 常数扫描，量化参数的混淆就排掉了：

| state u16 码值 | cos(CPU, HTP) |
|---|---|
| 0 | 0.987 |
| 256 | 0.987 |
| 30000 | 0.759 |
| 33000 | 0.604 |
| 按各自 zp 填（真实初值） | 0.17 |

本轮试过、**都不能用**的三条路，别重复：

| 改法 | 结果 |
|---|---|
| `--keep-states`（96 个 state 边界留 fp32）+ `offload_graph_io_quantization=0` | 编得出 12MB 入口 bin，execute 报 6001 |
| 同上，`enable_htp_fp16_precision=0` | context 直接 `Failed to load from EpContext model` |
| 同上，offload 用默认 1 | 能跑，cos 0.23，logits 量级对了但排序不同，10 步退化成 `279,1301,279,279,...` |
| `--state-zp-max 512`（把 48 个 state 量化零点压到 512，码值全 <1.2k） | CPU 上就崩（`279,451,451,...`）——码值小 = 有效位少，2B 吃不下 |

**结论（下一步的出发点）**：HTP 对 graph I/O 的 u16 state 实际只给约 11 bit
有效位（码值越大越少），而 2B 的 state 需要更多。缩码值和换 fp32 都只是把 11 bit
换个包装：fp32 边界 HTP 内部一样降到 fp16（11 bit）。0.8B 能用是因为它的 u16
state 零点很小（码值靠近 0，16 bit 全用上）。要真上 2B，得让 state 别走 graph I/O
——把 state 留在 QNN 图内（QNN 的 stateful/context 张量），或者用参考那套
`compile_language` 管线生成原生引擎，而不是 ORT 的 context 往返。

### 3.8 token 循环的三个喂数坑

图对了、context 编出来了，循环里还有三处会让输出变成乱码，且**CPU EP 上一样乱**
（所以必须先用 CPU 镜像排除 NPU）：

1. **量化后的「零」不是 0，是 zero point。** state 初值要 `np.full(shape, zp)`，
   不是 `np.zeros`。这条修完单 token 输出立刻正确。
2. **state 名字不能靠字符串推。** 导出器会把 19 个 state 输入自动命名成
   `/Cast_5_output_0...`，`_out_ → _in_` 替换命中不了，要拿 io_quant json 的
   `src` 字段建映射。（边界改 fp32 后这条自然消失，名字就是
   `layer_00_conv_state_out/in`。）
3. **in/out 量化参数可能不一样**（本例 48 对里 22 对）。输出直接回灌等于数值
   平移，必须 `(q-zp_out)*s_out` 反量化、再按 `s_in/zp_in` 重新量化。

还有一条算不上坑但容易误判：**打印 token id 不等于乱码**。2B 的
`' Paris.\nThe capital of France is London.'` 看着像复读，decode 出来是通顺的
（只是比 fp32 笨）。断言质量前先 `tokenizer.decode`。

### 3.9 板端工程化的几个坑

- `/dev/fastrpc-cdsp` 是 `root:system`，服务必须 root 起（systemd unit）。
- 用 `taskset -c 4-7` 把小核留给系统，否则 GUI 会卡。
- 模型常驻 5GB 以上，**不能和 Genie 的老模型同时常驻**，切换前要
  `systemctl stop`，否则 OOM。
- `flock` 防重复启动：服务重启时旧进程可能还没退，端口还在占。
- 服务端配 `p5_repair_tokens_text=1`、`full_p5_text_max_prompt_tokens=0`，
  TTFT 从 3.2–7.3s 降到 1.1–3.0s。

---

## 4. 一句话总结

导出不难（难点全在状态张量语义，闭环 16/16 就能过），
编译不难（ORT 在板子上现场编），
**难的是量化**：HTP 只吃全 uint8 QDQ，而 8-bit 激活对这个混合注意力模型
太重，需要按张量手工定 scale 才能保住质量。

---

## 5. 离线转换路线（converter 2.45 + 板端 qnn-net-run）—— 2026-10-21 实测

ORT-QNN 的 ABI 代差绕不过去时，改用固件同版本的 x86 converter 离线编 context binary。

### 5.1 环境（版本错一个都跑不动）

```bash
# py3.10 + numpy<2 + onnx<1.19，否则 Reduce 崩在垃圾 axis / onnx.version 不存在
uv venv qenv310 --python /usr/bin/python3.10
uv pip install --python qenv310/bin/python "numpy<2" "onnx==1.17.0" \
    pyyaml protobuf packaging pandas pyyaml
Q=$PWD/q245x/qairt/2.45.0.260326
PYTHONPATH=$Q/lib/python qenv310/bin/python $Q/bin/x86_64-linux-clang/qnn-onnx-converter ...
```

### 5.2 量化必须用闭环校准，且 raw 是 float32

```bash
qnn-onnx-converter -i graph.onnx -o out.cpp \
  --input_list calib/input_list.txt \
  --act_bitwidth 16 --weights_bitwidth 8 --bias_bitwidth 8 \
  --use_per_channel_quantization
```

- 校准样本的状态必须来自**闭环递推**；全零 state 会让 swish 分支 `scale = 0`。
- raw 文件必须是 float32。喂 fp16 会被当 float32 读成垃圾，表现为
  `attn_mask scale=3.4e7`、`swish ... Bad range for input`。

### 5.3 编 context binary：VTCM 只能 2MB

```json
// htp.json
{"graphs":[{"graph_names":["_08bq8"],"vtcm_mb":2,"O":3}],
 "context":{"properties":[{"weight_sharing_enabled":true}]}}
```

```bash
qnn-context-binary-generator --backend $Q/lib/x86_64-linux-clang/libQnnHtp.so \
  --model lib08bq8.so --binary_file 08bq8.bin --output_dir ctx \
  --config_file backend_ext.json      # backend_extensions -> htp.json
```

- `graph_names` 必须是真实图名（`strings lib*.so | grep initialize` 可查），`*` 不匹配。
- `vtcm_mb: 4` 板端会报 `Request feature vtcm size with value 4194304 unsupported`。
- 不要加 `devices` 段：会让 `Transpose` 校验失败（0xc26）。只给 `graphs` + `context`。

### 5.4 板端执行（固件自带 qnn-net-run 2.45）

```sh
export ADSP_LIBRARY_PATH=/usr/lib/rfsa/adsp:/dsp
/usr/bin/qnn-net-run --backend /usr/lib/libQnnHtp.so \
  --retrieve_context model.bin --input_list list.txt --output_dir out
```

- 输入文件**默认按 float32 解析**；要喂原生整型需 `--use_native_input_files`。
- 输出默认 float32。
- **每个张量的 native 布局不一致**：`conv_state_out` 声明 `[1,3,6144]` 但 buffer 是
  ONNX 顺序 `(6144,3)`；`rec_state_out` 才是 `[1,128,128,16]`。逐个按 JSON dims
  与实际数据比对，不要一刀切 transpose。
- 真跑在 NPU 上的判据：日志出现 `Executing Graphs` / `Finished Executing Graphs`
  且有输出；`Skel failed to process context binary (5005)` 则是上下文太大。

### 5.5 容量与精度（0.8B 实测）

| 配置 | 大小 | 结果 |
|---|---|---|
| fp16 整图 | 1.5GB | 拒收（skel err 5005，`--use_mmap` 也无效）→ 需拆图 |
| W8A16（act 16-bit 定点） | 729MB | 能加载能跑，但 logits cos 0.64，层 1 起跑偏 |
| 厂商 genie 0.8B 参考 | 640MB | 150 ms/token（6.7 tok/s） |

16-bit 定点激活对这类混合注意力模型不够；要精度就得 fp16，而 fp16 必须
把 decode 图拆成 2~3 段（每段 <800MB）分别编 context，再用自写 runner 串联。

### 5.6 调试环境与操作手册（2026-10-22 实测）

#### 板端登录

```sh
ssh p@192.168.3.104          # 密码 ppp，默认 22 端口
```

板端普通用户无 root 权限，所有 QNN 操作都要 `sudo -S`：

```sh
ssh p@192.168.3.104 "echo 'ppp'|sudo -S bash -c '<命令>'"
# 脚本里用环境变量传密码，避免口令出现在命令行历史/进程表里：
export SSHPASS='ppp'
sshpass -e ssh p@192.168.3.104 "..."
```

板端工作目录 `/root/q6a/q8`，暂存 `/var/tmp/x8`（rootfs 只有 ~5GB 可用，
1.8GB 的 context 推过去前先 `df -h /var/tmp`）。

#### 板端跑一张 context

```sh
echo 'ppp'|sudo -S bash -c 'cd /root/q6a/q8 && export \
  LD_LIBRARY_PATH=/usr/lib \
  ADSP_LIBRARY_PATH=/usr/lib/rfsa/adsp:/usr/lib/dsp/cdsp:/usr/lib/dsp/adsp:/dsp && \
  qnn-net-run --retrieve_context model.bin --use_mmap --backend /usr/lib/libQnnHtp.so \
    --input_list in/input_list.txt --output_dir out \
    --use_native_input_files --log_level info'
```

**`--use_mmap` 不要省**：>500MB 的 context 不带它会在 host 侧整份读进匿名内存，
板子直接 OOM（`Killed`，退出 137），跟 QNN 自己的报错无关。

- 判据仍然是日志里的 `Graph <tag> execution finished with result 0`。
- 常驻/长任务用 `systemd-run --unit=q_<tag> --collect --working-directory=/root/q6a/q8
  -p MemoryMax=7G -p StandardOutput=file:.../run.log`。
- **`Killed`（退出 137）= 板端 OOM**，不是 QNN 报错。7.3GB 内存 + 4GB 已被
  GNOME/ION 占用时，1.8GB 的 w8 context **加载不进去**，必须先换更小的产物（见 5.7）。

#### 主机侧（x86）全链路

```sh
cd <scratch>            # 放 08b_q.onnx / 2b_f32.onnx 的目录
export SSHPASS='ppp'

# 单层/子图切片 → converter → model lib → context binary → 推板 → 回拉 → 对比
bash go.sh <tag> <输出张量,逗号分隔> [额外输入]

# 已有 onnx 直接走一遍（不切片）
ONNX=2b_f32.onnx LIST=/tmp/cal2b.txt bash chain.sh <tag>
```

`chain.sh` 参数：`ONNX` `LIST` `ACT`(默认16) `WB`(默认8) `EXTRA`(转给 converter 的
额外参数) `FLOAT=1`+`FARG`(不量化的浮点对照)。单层切片 2~3 分钟，0.8B 整图
13 分钟，2B（3927 节点）converter ~2.5 分钟 + context ~2 分钟。

#### 主机侧踩过的坑

- **`systemd-oomd` 会 SIGKILL 自己的进程**：内存压力 >50% 持续 20s 就杀，
  与 `free` 显示的空闲内存无关。重活必须包一层：
  `systemd-run --user --scope -p ManagedOOMPreference=avoid -p MemoryMax=12G -p MemorySwapMax=0`。
- 板端 `--config_file` 里的 `graph_names` 必须等于 context 的图名，否则
  `unable to find graphName:<tag> in provided graphsConfigInfo object`。
- converter 报 `Input tensor: x__h not found` / `Axis quantization is required to be
  symmmetric` 都是 **fp16/预量化 ONNX 的兼容问题**，先把图规整成纯 fp32
  （见 5.7），不要在 converter 参数上绕。

### 5.7 2B 模型的实际形态（2026-10-22）

| 环节 | 结果 |
|---|---|
| 原始导出 `2b_w8.onnx` | 1.8GB，24 层 / 4114 节点，**fp16 内部 + fp32 IO 包装 + int8 per-channel DQL** |
| converter 直接吃 | 失败：`hidden_in__h not found`（节点乱序）→ `Axis ... symmetric`（DQL 编码） |
| 规整 | `to32.py`（fp16→fp32 + 拓扑排序）→ `foldq.py`（DQL 折成 fp32 external data，7.0GB） |
| W8A16 转换 | 成功，`b2.bin` 1.8GB，cpp 5.7MB |
| W8A16 context | 成功，1.8GB，约 2 分钟 |
| 板端加载（不带 mmap） | **OOM 被杀**（退出 137），即使停掉桌面腾到 3.7GB 空闲也一样 |
| 板端加载（`--use_mmap`） | **成功**：`Graph b2 execution finished with result 0`，49 个输出 |
| 单次 decode 耗时 | 229~246 ms/token（24 层，w8，含 52 入 / 49 出） |
| W4（`WB=4 EXTRA="--pack_4_bit_weights"`） | converter 成功（899MB），**context 生成失败** |

### 5.8 两个硬结论

1. **1.8GB 的 context 必须带 `--use_mmap` 才加载得进去。**
   不带时 host 侧要把二进制读进匿名内存再建图，峰值 >5GB，7.3GB 的板子直接
   OOM；带 mmap 时按页映射，峰值降到 2GB 级别，桌面照常跑（本次测试没停桌面
   的第二轮也直接通过）。

2. **4-bit 权重在这套 QAIRT 2.45 / v68 上编不出 context binary。**
   converter 能出 899MB 的 `.bin`，但 `qnn-context-binary-generator` 报：

   ```
   tensor datatype 1028 not supported          # QNN_DATATYPE_UFIXED_POINT_4
   exits with 7004, tensor parameters not supported
   ```

   与 16-bit 动态权重同样的天花板（`has incorrect Value 68, expected >= 73`）：
   x86 侧 `libQnnHtp.so` 的 v68 后端不认 4-bit 张量。厂商那份 810MB 的 2B
   产物是另一条工具链打出来的，不能靠本项目这套 QAIRT 复现。

所以本项目的 2B 形态就是 **W8A16 + 1.8GB context + `--use_mmap`**。

### 5.9 2B 数值现状：执行通，精度未成立（2026-10-22 实测）

用真实前馈数据（`boot.py` 逐 token bootstrap 出收敛 state）重做校准后，
量化范围已经正确（`logits` 编码覆盖 [-10.47, 12.70]，与参考一致），板端
`Graph b2r execution finished with result 0` 正常，52 入 / 49 出全部回来。

但数值对不上，且根因不在 HTP：

| 对比 | 结果 |
|------|------|
| HTP `logits` vs 同输入的 ORT 参考 | cos 0.03 |
| `layer_00_conv_state_out` | cos ≈ 0，65% 的 code 压在编码下限 |
| **ORT(`2b_f32`) vs HF 参考（pos0, token 760）** | **cos 0.23，top1 1218 vs 2614** |
| ORT(`2b_w8`) vs ORT(`2b_f32`) | cos 0.99992 ← DQL 折 fp32 无损 |

最后一行是关键：折叠没问题，**导出的图本身就不复现 HF**。pos 0 时
rope 退化成恒等，所以跟 rope 约定无关；剩下的嫌疑是 `hidden_in` 的
embedding 取值或导出 `forward` 的 mask/计算顺序。原始 torch 权重
（`qwen35_2b`）已不在磁盘上，没有 HF 参考就无法继续收敛这个问题。

**先别拿这份 2B 的 logits 当结果用**：它能证明"2B 编译成 context binary
并在 HTP 上执行"，不能证明"算得对"。要继续，第一步是把 torch 权重取回来，
用同一 pos/同一 token 对齐 ORT 与 HF，再把导出修对之后重跑这条链。

---

## 6. 精度天花板：为什么 Qwen3.5（gated DeltaNet）在 v68 上跑不准

2026-10-21 实测结论，**这一节决定了 Qwen3.5 能不能上 NPU**。

### 6.1 现象

W8A16 上下文能加载、能执行（`Executing Graphs` → rc=0），但**第一个 token 就错**：

```
参考（HF / ORT fp32）  top1 = 2614 ' following'
HTP W8A16              top1 = 3067, logits cos 0.64, 8 个 logits 顶到 16-bit 上限
```

### 6.2 定位方法：单层探针

不要在整个 24 层模型上猜。抽出 layer 0 的 ONNX 子图（3 个输入 / 若干输出），
把中间张量加进 graph.output，转换 + 上板，逐个比对：

```
q（L2-norm 之后）        cos 0.99939
S @ q                    cos 0.96110   <- 误差在这里出现
gating 之后              cos 0.93744
attention 残差           cos 0.84774
MLP 之后 hidden          cos 0.84752
conv_state / rec_state   cos 0.99965 / 0.99935
```

状态和投影都是 6e-4 的相对精度，**只有 DeltaNet 的读出 S@q 崩了**。

### 6.3 根因：定点量化 + 相消

`|S@q|` 的最大值只有 0.015，而它的加数规模是 0.6 × 0.9 → 相消两个数量级。
16-bit 定点下操作数 6e-4 的相对误差，在相消后放大成结果上的 ~1e-2。
18 个 DeltaNet 层串起来就是垃圾输出。

判据：**误差随位宽单调变化**（A8W8 比 W8A16 更差），且校准集第 0 个样本就是
这个输入本身 → 不是标定问题，是分辨率的物理下限。改校准、加数据都无效。

> **2026-10-21 实测更正（本节根因判断有误，逃逸路线结论仍有效）**
>
> 上面"相消"的说法没有实测支撑。在 s01 样本上直接取 DeltaNet 读出的真实张量
> （`/Unsqueeze_output_0` @ `/Mul_8_output_0`）算：`|加数|max ×  |加数|max` = 0.497，
> 输出 `|max|` = 0.740，**比值 0.7×，不存在相消**。把两个操作数各自量化后重算：
>
> | 位宽 | cos vs fp32 |
> |------|-------------|
> | a16b both | +1.00000 |
> | a12b both | +1.00000 |
> | a10b both | +0.99996 |
> | a8b  both | +0.99959 |
>
> 也就是说 **8-bit 量化本身足够**，读出的 0.976 不能由位宽解释。
>
> 真正的问题在转换器：MatMul 的**第二个动态操作数会被当成权重**，做成
> `<name>_nchw_converted_UFIXED_POINT_8`（8-bit 是硬约束，见下），而它给这些张量
> **自动分配的量程可以是错的**——实测出现过 scale=3.9e-7 / offset=-134（覆盖
> [5.2e-5,1.5e-4]）以及输出 scale=1.5e-9，而张量真实范围是 ±0.9，差 4~9 个数量级，
> 整个 state 被压成 0。
>
> 用 `--quantization_overrides` 手写量程后，同一个 rank-4 动态 MatMul
> （`[1,16,1,128] @ [1,16,128,128]`，真实数据、真实板子）从 cos 0.014 变成
> **cos 0.99953**。所以算子和芯片都没问题，是自动量化的问题。
>
> 另一条硬约束（实测）：`--use_dynamic_16_bit_weights` 在 v68 上被 backend 拒绝，
> 报 `[4294967295] has incorrect Value 68, expected >= 73` → **动态权重 16-bit 需要
> v73+**，v68 上所有 MatMul 的动态操作数只能是 8-bit。这解释了"改位宽无效"，
> 但原因不是分辨率不够，而是根本给不到 16-bit。
>
> 对整体方案的影响：**6.4 的方案 A（state 残差拆分）不是必需的**——不需要多加一个
> matmul，只需要把每层 2 个 state 张量的量程写对。3 层 slice 上做定向 override 实测
> 能拿到 +0.05 cos，但全量"每个张量都自己标定"反而更差（0.28），说明转换器默认值
> 对多数张量已经接近最优，要修的是少数点，不是全图重标定。


### 6.4 三条逃逸路线，在 v68 上全部关闭

| 路线 | 结果 |
|---|---|
| IEEE fp16 运算 | v68 没有 fp16 单元。`--float_bitwidth 16` 生成 FLOAT_16 上下文，skel 直接拒收（err 5005），**拆到 504MB 也一样**，所以不是容量问题而是数据类型 |
| 16-bit 权重（A16W16） | 图校验阶段就失败：`QnnBackend_validateOpConfig failed 3110`（FullyConnected） |
| 32-bit 激活 | HTP 图不支持该数据类型 |

### 6.5 提速：瓶颈是"图输出写回"，不是权重带宽（2026-10-21 实测）

先给结论，24 层整模型：

| 配置 | ms/token |
|---|---|
| 原始（KV 在图内，float32 输出） | 1364 |
| 原始 + `--use_native_output_files` | 801 |
| KV 环形缓冲移到 CPU（float32 输出） | 532 |
| KV 移到 CPU + native 输出 | **375** |

**定位方法：砍输出做 A/B。** 同一个 12 层子图（254MB 权重）：

| 输出集合 | 耗时 |
|---|---|
| 只有残差 | **62ms**（= 4.1GB/s 权重流送，完全正常）|
| + 6 个 KV 环形缓冲（写 12MB）| +449ms |
| + 18 个 DeltaNet 状态（写 5MB）| +198ms |

两个反直觉的事实：
- **图输出写回只有 ~25MB/s，而输入读取是 GB/s 级**（同一子图 28 个输入含 4.6MB 状态，仍只有 62ms）。
- 把模型对半切，两半都是 660ms（权重 254MB vs 510MB 却一样）→ 直接排除权重带宽。

根因（**已被 §6.5b 修正**）：KV 更新是 `Slice(Concat(past, new), start=1)`，即移位一格；
KV 张量 2048×512 的 16bit = 2MB，正好等于 VTCM 上限，Concat/Slice 变成分块 DDR 往返。

> ⚠️ **修正**：下面"写回只有 25MB/s"的推断是错的。Radxa 移植在同芯片上每 token
> 输出 24MB KV 仍只要 135ms。"25MB/s"是 **qnn-net-run 的 float32 转换 + 文件 I/O +
> 每步重载 context** 造成的，不是 HTP/DDR 特性。kvc + native 输出带来的
> 1364→375 仍然真实（同一个 harness 下对比），但归因应当是"少搬文件、少转换"，
> 而不是"绕过慢速写回"。

**修法一：KV 环形缓冲搬到 CPU**（`offload_kv.py`）。让图只输出新增的那 1KB
位置，CPU 用 `buf = concat(buf[1:], new, axis=pos)` 维护缓存（每 token 全部
12 个张量约 2ms 的 memmove）。图里少掉 12 个 Concat + 12 个 Slice。

**修法二：native I/O。** `--use_native_output_files` 少一半输出字节；
`--use_native_input_files` 再省一次 float32 往返（实测 375 → 276 ms/token）。
注意 native 模式下 qnn-net-run **不再帮你把 float32 转成 uint16**：`hidden_in`、
`rope_cos/sin`、`attn_mask` 在图中也是 16bit，必须自己按
`q = round(x/scale - offset)` 量化（`mklayout.py` 会把每个输入的 scale/offset 写进
layout.json，`qwen35-run.py` 用它量化）。

**真正的天花板是 harness，不是图**：`qwen-net-run` 每 token 都要重载 764MB
context（≈900ms），还要把状态写成文件再读回来。所以交付版仍是 1351 ms/step，
而图本身 276 ms/token。要做快只能上**常驻 runner**（一次 load，状态留在 buffer/arena），
这也是 Radxa 移植的做法（C++ 常驻 + shared-memory arena）。

**逐个排除掉的可调项（都实测过，别再试）**：
- `socModel`：我们的 context 是 0，vendor 是 35。用 `soc_id: 35` 重编 → 687 vs 678ms，无变化。
- VTCM：板子拒绝 4MB（`Request feature vtcm size with value 4194304 unsupported`, 0x138d）；不指定时的默认**就是** 4MB。2MB 是硬上限。加 `devices` 块要更多 → 编译器拒收整图（`Transpose hidden_in_ncf`, 0xc26）。
- `perf_profile`：default/burst/low_balanced/high_performance → 6040/5176/5139/5685ms，仅 ~5%。
- 8bit 激活：1385 vs 1419ms，不提速还更差。
- 输出前的 `Cast`：24 个全去掉 → 678 vs 660ms，无关。
- 文件 I/O：输入输出全放 tmpfs → 无变化。
- `--shared_buffer`：无变化。

### 6.5b 同芯片对标：Radxa Q6A 的 Qwen3.5-0.8B（2026-10-21）

同一颗 QCS6490、同样 2MB VTCM 上有一个公开移植
（`PrismPhi/...-Radxa-Dragon-Q6A-...`，QAIRT 2.47，ORT-QNN EP 2.3.0）。
它的实测数字（README + docs/en/benchmarks.md）：

| 指标 | 2K | 4K |
|---|---|---|
| Decode | **7.41 tok/s = 135 ms/token**（含采样/协议/状态搬运的端到端） | 7.88 tok/s |
| Chunk16 Prefill | 82.3 tok/s（≈195ms / 16 token） | 62.7 tok/s |
| TTFT 新请求 | 10.5 s | 13.7 s |
| context | 846.7 MB，25 graph 共享权重 | 847.6 MB |

我们同板：**图 276 ms/token、交付 harness 1351 ms/step、无预填**。
⇒ 图差 2.0×，端到端差 10×，TTFT 差 16–100×。

**最重要的纠正**：对方 decode 也是**输出整个 2MB KV**（`compact_kv`，k/v 合一），
每 token 写 24MB，端到端只要 135ms。所以本文档前面"图输出写回只有 25MB/s"
**是 qnn-net-run 的假象**（float32 转换 + 文件 I/O + 每步重载 context），
不是 HTP/DDR 的特性。真实带宽 ≥267MB/s。

其余可借鉴项：
- 状态**按 head 拆成小张量**（16 个 `1,1,128,128` uint16）；只对 layer 1/6 再拆到
  `1,1,128,16` / `128,8`（`selective_lane8_refinement`）——细分量化粒度，按需使用。
- 输入输出状态放在**同一块对齐 shared-memory arena 的互不相交切片**，
  "避免上千次 FastRPC 注册"（这是 qnn-net-run 做不到的）。
- embedding 在 CPU 用 fp32 表；**logits 直接输出 fp32**。
- 修了 logits 量化范围（他们记录 18/30 → 24/30）。
  我们也踩了同一个坑：8 个 code 顶在 65535，top-5 全部 =14.81，top1 在 8 个并列值里随机。
- 分层的是 **prefill**（24 个 graph），decode 仍是单图。

注意：对方也明确承认精度不对（`TOTAL 42` 读成 `10` 等）。但它 top1 可用，
我们不可用——先把 logits 范围修掉，这是独立于 DeltaNet 相消的一个真实 bug。

**2.47 的 context 在本板跑不了**：把 `/usr/lib/rfsa/adsp` 的 skel 换成 2.47 后仍报
`Stub lib id mismatch: expected v2.47.0.260601114230, detected v2.45.0.260326154327`
（skel 在 DSP 镜像里）。只能读它的契约/结构，不能直接借它的 bin。

### 6.5c 常驻执行器：1337 → 173 ms/token（2026-10-22）

上一节把瓶颈定位到 harness（qnn-net-run 每 token 重载 764MB context +
float32 往返 + 文件 I/O），这一节把它去掉。

**做法**：`qnnlib.py` 用 ctypes 直接调 QNN 2.45 的 C API（板子没有 QNN 头文件，
C++ 要交叉编译；一步只有一次 graphExecute，ctypes 开销在微秒级，而且纯 Python
改起来不用重编）。`qwen35rt.py` 是解码循环：context 只加载一次（350ms），
36 个 state 输出→输入直接在**图自己的缓冲区**里回写。

**实测（单实例，同板）**

| 路径 | ms/token |
|---|---|
| qnn-net-run，每步重载 context | 1337 |
| qnn-net-run + native I/O + KV 外置 | 276 |
| **常驻执行器，纯图时间** | **137** |
| **常驻执行器，端到端（含 HTTP、采样、tokenizer）** | **173** |

对照：Radxa Q6A 公开移植（同芯片、C++ host）在**本板**跑自己的 server，
实测 6.62 tok/s = 151 ms/token（其 README 标称 7.41 tok/s）。我们 5.77 tok/s。

**踩坑清单（ctypes 绑定 QNN）**

1. **结构体 sizeof 必须对**：`Qnn_QuantizeParams_t=40`、`Qnn_TensorV1_t=112`、
   `Qnn_TensorV2_t=136`、`Qnn_Tensor_t=144`、`QnnSystemContext_GraphInfo_t=88`、
   `QnnSystemContext_BinaryInfo_t=144`。用 5 行 C++ 打印 `sizeof` 对一遍，
   在 Python 里 assert，错一个字段就是往 DSP 喂垃圾。
2. **接口表偏移不一样**：backend 的 `QnnInterface_t` 里 `apiVersion` 是
   `Qnn_ApiVersion_t`（**两个** `Qnn_Version_t`，24 字节）→ 函数表在偏移 **40**；
   system 库用的是单个 `Qnn_Version_t` → 偏移 **32**。搞错的表现是
   `logCreate failed 0x44c / provider isn't initialized`，或
   `systemContextCreate failed 0x7530`。
3. **tensor 描述符要整块拷贝**：backend 按 **tensor id** 匹配，不按名字。
   自己拼一个描述符会报 `Expected Tensor ID: 1 not found`。从
   `systemContextGetBinaryInfo` 拿到的 `Qnn_Tensor_t` 原样复制，只把
   `memType=RAW`、`clientBuf.data/dataSize` 换成自己的 buffer。
4. **context 里的图已经 finalized**：再调 `graphFinalize` 返回 `0x3e8`，不是错误。
5. **`backendCreate` 必须有 logger**，但 `logCreate` 的回调参数是 `va_list`，
   Python 无法格式化；回调里直接丢弃，日志另开 `--verbose` 打印格式串。
6. **binary info 版本**：本板是 `VERSION_3`，布局和 V1/V2 不同（V3 去掉了
   `hwInfoBlob*`，加了 `contextMetadata`/`socModel`）。GraphInfo 的 V1 前缀
   仍然有效，但数组 stride 必须用 V3 的 88 字节。
7. **数据类型枚举不是 0x0402**：QNN 把类别和位宽编进枚举，
   `QNN_DATATYPE_UFIXED_POINT_16 = 0x416`（不是 0x402）。C++ 的 log 里会
   打印十进制的 1046。

**KV 做成真环**：注意力对 key 顺序不变，所以新 slice 直接写进
`slot = pos % 2047`，mask 只放行"已写入的 slot + 最后一条 lane"。不用每 token
memmove 2MB。

**还没吃下的 24ms**：`graphExecute` 137ms，`rt.step()` 162ms。拆开看
carry 3.7ms、top1 0.3ms、写 12 个 2MB KV 输入各 1KB——剩下 ~19ms 是
**每次 execute 的 FastRPC buffer 注册/失效**。参考移植的做法是把所有状态放进
**一块注册一次的对齐 shared-memory arena**（"avoiding thousands of small
FastRPC registrations"），QNN 侧要 `QnnMem_register` + `QNN_TENSORMEMTYPE_MEMHANDLE`
加 `memory.mem_type=shared_buffer`。这是下一步最大的单点收益。

**只剩一个 QNN context**：同一时刻跑两个 context（比如手动起一个 + systemd 一个）
会让每步从 162ms 掉到 300ms+，测速前先 `systemctl stop qwen35`。

### 6.5d 打包：模型权重随固件走（2026-10-22）

**结论**：`quecpi-image` 默认安装 `qwen35-npu-model`，固件自带权重，开机
`qwen35.service` 自动起，`npu-chat` 里 qwen3.5 就是默认模型。

**分层**

| 包 | 内容 | 大小 |
|---|---|---|
| `qwen35-npu` | 运行器、server、tokenizer、layout、systemd unit | ~3.6 MB |
| `qwen35-npu-model` | `/var/lib/qwen35/{qwen08b_i8.onnx,qwen08b_pf64_i8.onnx,emb08_f16.npy}` | 1.9 GB |

默认（ORT 后端）装的是 decode 图 + prefill 图 + embedding 表，**不带 `kvc.bin`**；
只有 HTP 镜像把 `kvc.bin` 加进 `QWEN35_MODEL_FILES`。

**为什么必须拆两个包**：权重是产物不是源码，`files/model/` 写进 `.gitignore`，
由 recipe 从 GitHub release 下载并按 sha256 钉版本；
只装 `qwen35-npu` 的镜像仍然可用（权重可以事后用
`qwen35-model-install.sh` 装）。

**recipe 关键点**

- `QWEN35_MODEL_IN_IMAGE ??= "1"`；`QWEN35_MODEL_FILES` 列出要装的文件名，
  `QWEN35_MODEL_ARTIFACTS` 给出 `label:file:sha256` 表，`__anonymous()` 把两者
  展开成 `SRC_URI` + `SRC_URI[...sha256sum]`。缺文件时 `bbfatal` 说清楚缺哪个、
  怎么关掉，而不是打出 1.9 GB 空包。
- **校验和必须用 `d.setVarFlag("SRC_URI", "<name>.sha256sum", sha)`**。
  datastore 没有方括号语法，`setVar("SRC_URI[x.sha256sum]", sha)` 只会建一个
  名字里带方括号的普通变量，而 `bb/fetch2/__init__.py` 查的是
  `getVarFlag("SRC_URI", "x.sha256sum")`；写错的症状是三个文件全部下载成功
  （1.9 GB）之后才报 `Missing SRC_URI checksum`。
- `QWEN35_MODEL_URL ??= "https://github.com/super617/Qwen3.5-for-qcs6490/releases/download/${QWEN35_MODEL_TAG}"`，
  `QWEN35_MODEL_TAG ??= "latest"`。`latest` 是移动目标，真正锁版本的是上表的
  sha256；要可复现就把 tag 换成不可变的。离线构建把它覆盖成
  `file:///data/qwen35-model` 即可。
- `DPKG_BUILDCMD = "dpkg-deb -Znone"`：ONNX 图本身是密实的，压缩只花时间不省
  空间（1.9 GB 的 deb 压缩要几分钟，`-Znone` 秒级）。
- `SYSTEMD_AUTO_ENABLE = "enable"` + `ConditionPathExists=/var/lib/qwen35/qwen08b_i8.onnx`
  与 `emb08_f16.npy`：缺权重时 unit 是 skipped 不是 failed，不会
  `Restart=on-failure` 打十次。
- `do_configure` / `do_compile` 都是 `noexec`：**编译期不重新训练、不重新量化**，
  只解压 ORT wheel + 拷贝文件。

**npu-chat 的判定**（`qwen35_installed()`）：

1. 权重在 `/var/lib/qwen35`
2. launcher 存在
3. `/run/qwen35.ok` 存在 **或** `systemctl is-enabled qwen35` 成功

第 3 条是关键：context 加载要 ~20 s，GUI 通常比 server 先起来，如果死等 marker，
正常开机时 qwen3.5 反而当不了默认。判据改成"unit 已 enable 就认为它在路上"，
第一轮对话在 `start_qwen35()` 里等（最多 90 s）。手动装、unit 没 enable 的情况
仍然要 marker 才认。

实测（板端 192.168.3.104）：

    models()  -> ['qwen3.5-0.8b', 'llama3.2-1b', 'qwen2.5-0.5b']
    default   -> qwen3.5-0.8b

**权重怎么再生**

四个产物都不进 git，放在 GitHub release（`super617/Qwen3.5-for-qcs6490`，tag `latest`）：

| 文件 | 字节数 | 用途 |
|---|---|---|
| `qwen08b_i8.onnx` | 754,554,881 | decode 图（52 in / 57 out），ORT 默认 |
| `qwen08b_pf64_i8.onnx` | 760,231,420 | 批量 prefill 图（S=64，53 in / 49 out） |
| `emb08_f16.npy` | 508,559,488 | fp16 embedding 表 |
| `kvc.bin` | 763,400,192 | QNN context，仅 HTP 镜像需要 |

`qwen08b_i8.onnx` / `qwen08b_pf64_i8.onnx`：`quantize_dynamic` 从 fp32 图转出。
prefill 图由 `export_prefill.py` 导出（左 pad 到 64 的倍数，状态零时严格等价）。
`kvc.bin`：转换流程的产物。

`emb08_f16.npy` 是官方 Qwen3.5-0.8B 的
`model.language_model.embed_tokens.weight`，BF16 `[248320,1024]` → fp16。
  safetensors 里可以直接按 header 的 `data_offsets` 用 HTTP Range 只拉这一段
  （508 MB，省掉 1.7 GB 全量下载）：

      header = 8 bytes len + json
      byte range = 8 + len + data_offsets[0] .. +data_offsets[1]

  重新生成的表与旧表行为逐 token 一致（selftest token 序列完全相同）。

### 6.5f 顺带测到的一件事：8 位激活不提速

为了看看能不能"拿精度换速度"，把整网重建为 A8W8（激活 8bit）并计时：

| 构建 | 上下文 | 加载 | 每 token | 精度（layer0 attn_res cos） |
|---|---|---|---|---|
| W8A16 | 764MB | 939ms | **1419ms** | 0.847 |
| A8W8  | 764MB | 1083ms | **1385ms** | 0.809 |

**激活降到 8bit 一点没快**（1385 vs 1419，在噪声内），精度还更差。
所以这条轴上没有可交易的空间。

`--perf_profile default/burst/low_balanced/high_performance` 对 3 次推理总时长：
6040 / 5176 / 5139 / 5685 ms，只有 ~5% → 不是主频问题。

真实瓶颈：每 token 要搬 800MB 权重，1.4s 内只有 **570MB/s 有效带宽**，
比 LPDDR 峰值低约 20 倍 → 卡在 HTP 跑 2045 个小 op + 只有 2MB VTCM。
想加大 VTCM 必须在 backend config 里加 `devices` 块，而加了之后编译器直接
拒收整图（`Transpose hidden_in_ncf` 校验失败 0xc26），这条路也堵死。

### 6.5e 精度复盘：tokenizer bug 与测错的三次教训（2026-10-22）

**板上真实对话验证**（这是唯一算数的验收）：

    prompt "Hi" -> 板端 "眼圈\u00acindrahltatricesnageмот..."   129.5 ms/token, 5.0 tok/s
    HF 参考     -> "Hello! How can I help you today? 😊"

**1. tokenizer 真 bug（已修）**

`Tokenizer.SPLIT` 把参考实现的 `[^\r\n\p{L}\p{N}]?` 写成了
`[^\r\n\W\d_]?`——空格本身是 `\W`，被这个字符类排除掉了，于是
"The capital of France is" 被切成 `['The',' ','capital',' ',...]`，
每个单词都丢掉了 `Ġ` 前缀，token id 全错（`[760,220,62865,...]` 而不是
`[760,6511,314,...]`）。改成 `[^\r\n\w]?` 后与 HF 逐 id 一致。

另外 `decode()` 依赖模块级 `_ID2TOK`，没人调用 `_fill_rev()` 时静默返回
空字符串；改成在 `Tokenizer.__init__` 里自己填。

**2. 三次测量陷阱（都让我追过假线索）**

| 陷阱 | 症状 | 正确做法 |
|---|---|---|
| `onnx.utils.extract_model` | 同一个张量、同样输入，加个 tap 就变 2.07 倍 | 只在**全图**上 append output，绝不 extract 后测 |
| ORT fp16 对 live set 敏感 | 多 tap 一组输出，`/Mul_14_output_0` 从 0.917 变 1.899 | 参考值要么用 HF，要么用固定 49 输出的原生 IO |
| 状态没有回灌 | 名字是 `layer_XX_rec_state_out` vs `_in`，`if k in state` 静默不匹配，层输出 |max| 只到 HF 的 1/4 | 按 `_out`→`_in` 显式映射 |

修掉这三条后，**fp16 ONNX 与 HF 的 logits cos 0.83–0.97，top-1 在多数位置一致**
⇒ 导出是对的，问题在量化。

**3. 板上逐层定位（pos 0、单 token、状态全零）**

    层 0  conv_state cos 0.99965   rec_state cos 0.99931      <- 设备算对了
    层 1  conv_state cos 0.549     rec_state cos 0.181        <- 立刻崩
    logits cos 0.588（fp16 ONNX 是 1.0，相对 HF 0.83）

所以不是"误差随位置累积"，是**每层都在掉**。而层 0 的状态对、层 1 的错，
说明层 0 的 **hidden 输出**（RmsNormGated + out_proj + MLP）才是掉精度的地
方——它的 state 输出不经过那段。

**4. 量化 override 的两组失败实验**

| 方案 | 49 输出平均 cos | logits cos |
|---|---|---|
| 基线（converter 自带标定） | **0.819** | **0.738** |
| 全部 2827 个激活用我测的区间的 | 0.157 | 0.299 |
| 全量 + 保留 converter 位宽 | 0.157 | 0.299 |
| 只覆盖 18 个 state 读出 MatMul | — | 0.565 |

结论：**拿 6 个样本测出来的 min/max 去覆盖全图，比 converter 用 33 个样本
自己标定更糟**。单张量覆盖看起来能把一个 tap 从 0.717 救到 0.9988，但那是在
同一个样本上量的——是过拟合，不是改进。

**5. 上下文用的是原生 HTP 算子，不是 LUT 拆分**

    ElementWiseBinary 510  Reshape 397  Transpose 325  FullyConnected 187
    ElementWiseNeuron 102  RmsNorm 79   StridedSlice 78  ElementWiseUnary 66
    ReduceSum 54  Concat 54  MatMul 48  Convert 48  Cast 37  Split 18  Tile 12  Softmax 6

没有 Div/Sqrt/Exp 之类的 LUT 近似，所以掉精度是 scale/位宽问题，不是算子
实现问题。

**下一步**：把层 0 的输出路径（RmsNormGated + out_proj + MLP）单独切出来、
在板上与 ONNX 对比，确认是 gate 的 `silu(z)`、`Mul_12`（normed*weight）还是
MLP 的 `silu(gate)*up` 乘积掉的。那一处的数值尺度也解释了为什么
converter 给 `Div_3` 的覆盖到 993（1/sqrt(var+eps)）——小方差区域的定点表示
本来就吃紧，需要把状态在进 norm 前先缩放到 O(1)。

### 6.7 真正可行的架构（若还要做）

**方案 A：状态残差拆分（最省，全部留在 NPU）**

每个 DeltaNet 层的 S 喂两份：`S_hi`（粗尺度量化）+ `S_lo = (S - S_hi) * 256`
（同样尺度量化），图内做

```
out = S_hi @ q + (S_lo @ q) / 256
```

第二个 matmul 的 16 bit 全部用在残差上 → 读出等效 ~24 bit。
代价是每 head 每层多一个 128×128 matmul（2.6e5 MACs，可忽略）。
状态必须在图外保持 fp32，所以要常驻 QNN runner + 重新导出带额外输入的图。

**方案 B：CPU/NPU 混合**

**DeltaNet 放 CPU（fp32），MLP + 6 个全注意力层放 NPU**：

- MLP 没有相消（SiLU 输出单边），W8A16 下 cos > 0.999；
- 每层一个 QNN graph，层间只传 4KB hidden；
- CPU 侧约 2e8 MACs/token，8 核上 ~40ms；
- 需要**常驻 runner**（QNN C API），`qnn-net-run` 做不到（一次进程一个 context、
  一轮 input_list 一次推理）。

### 6.8 不要踩的坑（本轮新增）

- 校准 raw 必须是 float32，且必须是**闭环递推**出来的状态。
- 给 converter 传 `--input_list` 会触发量化，此时 `--float_bitwidth 16` 不再生效
  （会静默变成 A8W8）。要 A16W16 必须显式 `--act_bitwidth 16 --weights_bitwidth 16`。
- `--bias_bitwidth` 只接受 8 或 32（16 会直接报错）。
- 子图校准的 input_list 每行**必须正好是该子图的输入个数**，否则报
  `Graph contains N inputs, but only found input data for M inputs from user`。
- 抽子图时重命名内部张量（`/Add_110_output_0` → `hidden_11`），否则 converter
  找不到带 `/` 名字的 raw 文件；重命名要同时改 node.input 和 node.output。
- `qnn-context-binary-generator` 的 `graph_names` 要和 cpp 里
  `XXX.initialize(..., "graphName", ...)` 完全一致（有时带下划线有时不带）。
- **张量名会被 sanitize**：`/Concat_6_output_0` 在 context 里叫
  `_Concat_6_output_0`（斜杠替换成下划线，不是加前缀）。按 ONNX 名字查 QNN
  张量永远查不到，`mklayout.py` 会同时写 `qnn_name`。
- **同一张量进/出布局可能不同**：`conv_state_out` 是 `[1,6144,3]`（ONNX 序），
  `conv_state_in` 是 `[1,3,6144]`（native 序）。必须搬轴，否则等于把卷积历史
  转置后喂回去——不回退、不报错，只是结果错。
- **16bit logits 要解码**：`real = scale * (q + offset)`。当 float32 直接读会得到
  随机 top1；本模型的 scale/offset 使 8 个 code 顶在 65535，top-5 全并列。
- **不要同时跑两个 QNN context**：会互相抢 DSP，单步从 160ms 变 300ms+。
- **`.gz` 源文件会被 bitbake 自动解压**：`SRC_URI` 里的 `file://x.json.gz` 在
  workdir 里变成 `x.json`（`bb.fetch2.unpack` 对非 tar 的 `.gz` 直接
  `gzip -dc`），`do_install` 再找 `.gz` 就是 `cannot stat`。要么装 `.json`，
  要么写 `file://x.json.gz;unpack=0`。
- **大文件包的 deb 关压缩**：`DPKG_BUILDCMD = "dpkg-deb -Znone"`。1.9 GB 的
  权重压缩要几分钟且几乎不减小体积。
- **动态校验和必须 `setVarFlag`**：`d.setVar("SRC_URI[x.sha256sum]", sha)` 不会
  建 varflag，只会造一个名字带方括号的普通变量；bb.fetch2 查的是
  `getVarFlag("SRC_URI", "x.sha256sum")`，于是症状是三个文件全部下载成功
  （1.9 GB）之后才报 `Missing SRC_URI checksum`。
- **`ConditionPathExists` 比 `Restart=on-failure` 好**：可选的大文件没装时，
  unit 应该是 skipped 而不是 failed，否则开机打十次重启。

---

## 7. 最终结论：这模型不要上 NPU，ORT int8 更快也更准（2026-10-22 实测）

第 6 节把 HTP 路线的精度问题查到了底：**16-bit per-tensor 激活量化在 1621 个
张量上累积，误差全图弥散，没有可修的局部点**。tokenizer 修好之后又在板上复核
一次，输出依然是乱码，所以 HTP 路线正式放弃。最终落地的是**纯 CPU 的 ONNX
Runtime int8 动态量化**：

| | HTP（W8A16 context） | ORT int8（权重 int8 + 激活 fp32） |
|---|---|---|
| logits cos（对 fp32） | 0.9688 | **0.9925** |
| 单 token | 126 ms 图 / 5.3 tok/s | **139 ms / 7.2 tok/s**（4 线程） |
| HTTP 端到端 | — | 6.4 tok/s |
| 中文 | 乱码 | 完全正确 |

同芯片对标：Radxa Q6A 的 Qwen3.5-0.8B 移植是 151 ms/token（其服务 6.62 tok/s），
我们 139 ms/token，**持平或略快**，而且文本可读。

### 7.1 为什么权重 int8 就够、激活不能量化

- 复现设备数值：只量化权重 → cos 0.9991；打开激活量化 → 0.959（设备 0.9688）。
- 离线模拟（`simfull.py`，对 1621 个张量做 per-tensor round-trip）得 cos 0.9586、
  top-5 与设备有 4 位相同 → 确认就是量化累积，不是布局/标定/导出问题。
- 按误差排名消融无效：top-5 张量占误差方差 51.8%，但跳过它们结果只差 3e-5；
  跳过所有 `x*x`（RmsNorm 的 Pow）和所有 `q^T @ S`（`__prod`）也一样。
  误差是 24 层 × 1621 次 round-trip 摊开的，**没有局部修点**。

结论：**在 HTP 上做 LLM，先算一下"全图 per-tensor 激活量化"的等效位宽，
不要指望调标定能救。** 权重 8bit per-channel 损失远小于激活 16bit per-tensor。

### 7.2 量化操作（本机，onnxruntime 1.31）

```python
from onnxruntime.quantization import quantize_dynamic, QuantType
import onnx
m = onnx.load("qwen08b_fp32.onnx")          # 3.0 GB，但内存里能装
quantize_dynamic(m, "qwen08b_i8.onnx", weight_type=QuantType.QInt8)
# → 754,554,881 B，3677 节点，435 初始化器，int8 权重 + fp32 激活
```

坑：**必须传 ModelProto，不能传路径**。传路径时 ORT 先 `onnx.save` 一个
2.9 GB 的中间模型，触发 `google.protobuf.message.EncodeError: Failed to
serialize proto`（protobuf 2 GB 上限）。

### 7.3 线程数：4 比 8 快 25%（QCS6490 是 4 大 + 4 小）

`intra_op_num_threads` 扫描（48 步、长 prompt，实测 ms/token）：

| 线程 | 8 | 6 | 4 | 3 | 2 |
|---|---|---|---|---|---|
| ms/token | 188.3 | 192.5 | **138.6** | 141.0 | 140.6 |

小核参与 int8 MatMul 会在 barrier 上拖住大核。`IOBinding` 无收益（142 ms vs 139）。
服务里默认 `QWEN35_THREADS=4`、`inter_op_num_threads=1`、`ORT_ENABLE_ALL`。

### 7.4 板端 Python 只有 Debian 的 3.13：装 wheel 到 dist-packages

固件的 rootfs 是 `prebuild/debian-gnome-rootfs.tar.xz`，由 `quecpi-image` 用
`rsync -aHAX`（**没有 `--delete`**）盖在 Yocto rootfs 上，所以 `/usr/bin/python3`
是 Debian 3.13.5，`sys.path` 里是 `/usr/lib/python3/dist-packages`，不是 Yocto 的
`site-packages`。因此：

- 不要写 `python3-onnxruntime`（oe-core 那是从源码编，几个 GB）；
- 直接 `SRC_URI` 拉 manylinux wheel（`cp313`/`aarch64`，21 MB），
  `do_install` 里 `python3 -m zipfile -e ... ${D}/usr/lib/python3/dist-packages`；
- 运行期只需要 `numpy` + `packaging`（Debian rootfs 自带），
  **不需要** `flatbuffers`/`protobuf`（那是 quantization/transformers 才用的）。

### 7.5 端到端形态

- `qwen35rt.py`（HTP）/ `qwen35ort.py`（ORT）暴露同一组方法
  （`reset/step/top1/logits_f32/close/pos/stats`），`qwen35-serve.py --backend ort|htp`
  切换后端，unit 里 `QWEN35_BACKEND=ort` 默认。
- ORT 后端**不需要** `layout.json`：图就是 ONNX 模型本身，输入本来就是 ONNX 序；
  state/KV 直接是图的输入数组，一步 = 一次 `session.run` + 把 state 输出拷回去。
- `ConditionPathExists=/var/lib/qwen35/qwen08b_i8.onnx`，权重 720 MB + 嵌入 508 MB。
- 自检 `reference.json` 记 8 个贪心 token，`quality: pass/MISMATCH` 写进
  `/run/qwen35.quality`，不作为启动门槛（`/run/qwen35.ok` 仍由 launch 脚本写）。

### 7.6 这一轮最贵的三个教训

1. **tokenizer 的 GPT-2 byte-map 错一个字节，输出就"像坏了"**：`¡..¬` 是
   `0xA1..0xAC`，写成 `range(0xA1, 0xAC)` 漏掉 `0xAC`，导致 `0xAD`
   （CJK 最常见的 UTF-8 续字节）整体错位，所有"语/读/计"类汉字解码成前一个、
   编码成 token 0。**先确认 tokenizer round-trip，再怀疑量化。**
2. **对比精度必须喂同一份数据**：两个参考用不同 feed，cos 会差到 0.63，看着像
   精度灾难，其实全是假象。
3. **先量化算预期**：predict 8bit 权重组合模拟 cos 0.997，实测 0.9688 —— 相差
   50 倍以上的时候，问题不在量化，去查映射/布局/标定。

### 7.7 试过但没用的优化（都是板上实测，别再重复）

| 想法 | 结果 | 结论 |
|---|---|---|
| 权重压到 4bit（ORT `MatMulNBits`，block 32） | 472 MB，**153 ms/token**（int8 是 139），cos 0.9925→0.9868 | 更慢更差。解包 nibble + 每 block 重缩放的开销 > 省下的带宽；ARM 的 dot product 只有 int8 |
| 绑大核 `taskset -c 4-7`（2.4 GHz vs 小核 1.96 GHz） | 144.2 ms（4 线程）；全核 4 线程 136.8 ms | 小核真的有用，别绑 |
| `allow_spinning=0` | **222 ms** | 默认必须开着；关掉直接 1.6× 慢 |
| `enable_mem_pattern=0` / `enable_cpu_mem_arena=0` | 140 ms / 222 ms | 无收益 |
| IOBinding | 142 ms | 无收益 |
| 8 / 6 线程 | 188 / 192 ms | 4 线程最优 |

带宽账：754 MB int8 权重 / 137 ms = **5.4 GB/s**，已经等于板子 memcpy 实测
5.5 GB/s。int8 路线是撞内存墙，想再快只能减字节，而 4bit 在 CPU 上不划算。

### 7.8 落地后的默认配置（照抄即可）

```
qwen35.service:  QWEN35_BACKEND=ort   QWEN35_THREADS=4
                 ConditionPathExists=/var/lib/qwen35/qwen08b_i8.onnx
qwen35ort.py:    intra_op=4  inter_op=1  ORT_ENABLE_ALL  CPUExecutionProvider
                 权重 int8 + 激活 fp32，state/KV 直接放在图的输入数组里
qwen35-serve.py: --backend ort|htp 共用一个 OpenAI 接口
                 /run/qwen35.quality = pass/MISMATCH（对 reference.json 的 8 个贪心 token）
                 /run/qwen35.ok 仍由 launch 脚本在自检通过后写
```

板上端到端实测（HTTP + SSE 流式）：

```
中文 3 句自我介绍     51 tok  10.30 s  4.95 tok/s（含首 token 2.6 s）
比喻解释神经网络      161 tok  26.39 s  6.10 tok/s
英文 haiku            19 tok   5.70 s  3.33 tok/s（短答被 TTFT 摊薄）
1506 tok 长文        233.6 s  decode 141.7 ms/tok = 7.06 tok/s（稳定不退化）
```

（上面这组数据是批量 prefill 上线前测的：当时 prefill 逐 token 跑，短 prompt
TTFT 2.6–4.0 s。现在 prefill 走 64 token 一块的图，见 7.9。）

### 7.9 批量 prefill：TTFT 从 13 s 掉到 1.5 s（2026-10-22 实测）

问题：decode 图是 batch=1，prompt 每个 token 都要把 754 MB int8 权重读一遍，
所以 prefill 和 decode 同速（132 ms/token），100 token 的 prompt 光首 token 就
13.2 s。

解法：**同一份权重再导一张 64 token 批量图**（`export_prefill.py`，host 侧），
不是新模型，只是多一个 batch 维：

* 投影 / 短卷积 / 两种 attention / MLP 全部按 S 批量 → 每层权重一个 block 只读一次
* 只有 gated DeltaNet 的递推保留 token 循环（串行依赖），在图中展开 64 份 →
  42872 个节点，加载 5.4 s
* block 固定 64 lane，短 prompt 用 **前置** pad（不是后置）：短卷积要读前 3 个
  token 的 state，前置 pad 看到的永远是零历史，正好等于新序列的初始状态，所以
  带 pad 的 block 是**精确**的。fp32 导出对 HF 真值 cos **1.0000000**
  （L=1/8/23/64/100/128/200，argmax 全同）
* block 结束把 conv/rec state 和 KV 环搬进 decode 图的输入数组即可接着解码；
  两张图的 KV 环宽度不同（2047 vs 2048）但**按绝对位置写入**，位置 < 2047 时
  布局一致，所以 >2040 token 的 prompt 仍走逐 token 老路

板上实测（4 线程，int8 动态量化）：

```
pf64 一个 block（64 token）      947 ms   = 14.8 ms/token
逐 token prefill 同样 64 token   8640 ms  = 132 ms/token      → 9.1×
HTTP 首 token（1 block prompt）  1.4–1.5 s（原来 2.6–4.0 s）
HTTP 首 token（~150 token prompt）3.4 s（原来 20 s）
decode 不受影响                  139.0 vs 137.9 ms/token（同一 A/B）
代价：加载 +5.4 s、RSS +720 MB（服务总 3.1 GB）
```

剩下 947 ms 的大头不是权重而是**递推 state 的搬运**：18 层 × 64 步 ×
[1,16,128,128] fp32 ≈ 5 GB/block，而权重只有 754 MB。要再快得把 gated delta
rule 换成 chunked（WY/UT）算法让 state 每 block 只碰一次，那是下一个量级的活。

坑（都在板上踩过）：

* **`pad` 输入不能 squeeze**：要 [1,S,1]，广播到 `n_v=16` 时 [1,S] 会 shape 冲突
* **pad lane 不许写回 KV 环**：图的输出是整段 shifted window，新 lane 在尾部，
  只拷 `[padn:padn+L]` 这几条真 lane，否则环里留零 key 的洞
* **量化精度**：64 lane 一起量化，ORT 每个 MatMul 只有一组激活范围（decode 图
  是每 token 一组），所以 int8 prefill 对 fp32 是 cos 0.938，int8 decode 是
  0.971–0.990（fp32 两张图之间 0.999996）。top-1 在探针上一致、回答正常，但
  贪心续写会和逐 token 路径分叉 → 自检只报 `prefill: cosine 0.9xxx`，阈值 0.85
  以下才算 state 搬错
* QWEN35_PREFILL=0 关掉；图缺失/读不了自动退回逐 token，`prefill: absent`
