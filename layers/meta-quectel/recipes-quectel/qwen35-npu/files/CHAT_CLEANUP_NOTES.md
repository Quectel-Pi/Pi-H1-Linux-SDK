# npu-chat 修复 + 旧 CPU 方案残留清理（任务 #8）

日期：2026-10-23
范围：只动 `npu-llm/files/npu-chat`、删 `qwen35-npu/files/` 下 9 个旧文件、重写
`qwen35-npu/.gitignore`。**没有**改 bb recipe，**没有**改 `files/runtime/`、`native/`、
`configs/`，**没有**碰板子，**没有**跑 bitbake。

背景：方案已从「CPU ONNX Runtime int8」改为「Radxa 预编译 QNN EPContext + ORT QNN EP
（Hexagon HTP v68 / NPU）」。旧的 CPU 产物（qwen08b_i8.onnx / kvc.bin / emb08_f16.npy）
已经从工程里删掉，npu-chat 里硬编码它们的地方就成了坏路径，本次修掉。

---

## A. npu-chat（`layers/meta-quectel/recipes-quectel/npu-llm/files/npu-chat`）

### 改动前的常量（坏路径）

```python
QWEN35_LAUNCH      = "/var/lib/qwen35/run/launch.sh"
QWEN35_PAYLOAD     = "/usr/share/qwen35/src/install.py"
QWEN35_NPU_LAUNCH  = "/usr/share/qwen35-npu/tools/qwen35-launch.sh"
QWEN35_DIR         = "/var/lib/qwen35"
QWEN35_MODELS      = ("qwen08b_i8.onnx", "kvc.bin")
QWEN35_ARTIFACTS   = ("emb08_f16.npy",)
...
def _qwen35_artifacts():
    return all(have(f) for f in QWEN35_ARTIFACTS) and any(have(f) for f in QWEN35_MODELS)
```

### 改动后

```python
QWEN35_LAUNCH      = "/usr/lib/qwen35/qwen35-launch.sh"
QWEN35_NPU_LAUNCH  = "/usr/lib/qwen35/qwen35-launch.sh"   # 同一个入口，保留给显式覆盖
QWEN35_PAYLOAD     = 已删除（板端不再安装 install.py）
QWEN35_DIR         = "/usr/share/qwen35"

QWEN35_MODELS = (
    "models/2k/decode_ctx.onnx",
    "models/2k/decode_ctx_qnn.bin",
    "models/2k/prefill_ctx.onnx",
)
QWEN35_ARTIFACTS = (
    "common/embedding_fp32.npy",
    "common/tokenizer.json",
    "contracts/2k/rope_ctx2048_cos_sin_fp32.bin",
    "contracts/2k/tensor_contract.tsv",
    "contracts/2k/advance_plan.tsv",
)

def _qwen35_artifacts():
    return all(have(f) for f in QWEN35_MODELS + QWEN35_ARTIFACTS)
```

要点：

1. `QWEN35_DIR` → `/usr/share/qwen35`（资产根）。`QWEN35_MODELS/ARTIFACTS` 里的路径都是
   相对它的子路径，`os.path.join` 直接拼成
   `/usr/share/qwen35/models/2k/decode_ctx.onnx` 这种形式。
2. `QWEN35_LAUNCH` → `/usr/lib/qwen35/qwen35-launch.sh`（运行时目录）。
3. `QWEN35_NPU_LAUNCH` 原来是旧 CPU 方案的 `/usr/share/qwen35-npu/tools/qwen35-launch.sh`，
   现在指向新 launcher（与 `QWEN35_LAUNCH` 同路径）。保留这个常量是为了不破坏已经通过
   环境变量覆盖它的部署脚本 / 单元文件。
4. `QWEN35_PAYLOAD`（`/usr/share/qwen35/src/install.py`）整个删掉，连带三处引用：
   - `_qwen35_present()` 里的 `os.path.exists(QWEN35_PAYLOAD)` 分支；
   - 该函数末尾的 `return not os.path.exists(QWEN35_PAYLOAD)` → 改成 `return True`
     （launcher 存在 + 资产齐全 + `systemctl cat` 失败 ⇒ 单元是随包安装的文件，可用；
     旧逻辑只是在区分「install.py 装法」和「npu 包装法」，现在只剩一种装法）；
   - 错误文案里的引用。
5. 判断逻辑 `all(ARTIFACTS) and any(MODELS)` → `all(MODELS + ARTIFACTS)`。
   原因：`qwen_server.py` 启动时对每个文件做 `is_file()` 硬检，少一个就整个服务起不来，
   所以 `any()` 会让 GUI 误以为可用，然后在第一轮对话上卡 90 s 超时。
   EPContext 是 `xxx_ctx.onnx` + `xxx_ctx_qnn.bin` 成对出现的，`.bin` 是 HTP 编译好的
   context，`.onnx` 只是包着它的壳，绝不能只判其一。
6. 注释更新：`resident ONNX Runtime QNN server` / `~3 GB of NPU context` →
   `ONNX Runtime + QNN EP`、`pre-compiled EPContext graphs`、`Hexagon HTP (V68) NPU`。
   旧的「720/764/508 MB」尺寸描述在 npu-chat 里本来就没有，只存在于 `.gitignore`
   和 `Qwen35-npu_1.0.bb`（后者未动）。
7. 错误提示：`跑 qwen35-model-install.sh <目录> 安装` →
   「模型由 qwen35-npu-model 包安装，检查 /usr/share/qwen35 是否完整，重装该包后
   systemctl restart qwen35」。
8. **HTTP 调用逻辑一行没动**：`QWEN35_URL`、`ask_server()`、SSE 解析、`/v1/models` +
   `/v1/chat/completions` 的请求构造、超时值全部原样保留。`start_qwen35()` 里
   `systemctl start qwen35` 优先、直接 exec launcher 兜底的结构也没变，只是兜底循环里
   参与比较的两个常量现在同路径（等价、无副作用）。
9. `--selftest` 里没有调用任何旧脚本（只测 prompt 拼装 / SSE 提取 / TTS 文本转换 /
   VAD rms），无需改动。

### 验证

```
$ python3 -c "import py_compile;py_compile.compile('npu-llm/files/npu-chat',doraise=True)"
PY_COMPILE_OK
$ ./npu-llm/files/npu-chat --selftest
selftest ok
$ grep -n "QWEN35_PAYLOAD\|qwen08b_i8\|kvc.bin\|emb08_f16\|/var/lib/qwen35/run\|qwen35-model-install" npu-llm/files/npu-chat
(无输出)
```

diffstat：`1 file changed, 34 insertions(+), 25 deletions(-)`（纯常量/注释/判断逻辑）。

---

## B. 删除的 9 个旧 CPU 方案文件（`qwen35-npu/files/`）

```
qwen35-convert.sh          4,150 B   转模型（CPU int8 量化流水线入口）
qwen35-model-install.sh    4,745 B   板端安装模型资产
qwen35-ready.py              706 B   就绪探测（写 /run/qwen35.ok）
qwen35-rt.py                 258 B   旧 runtime 入口
qwen35-run.py             10,268 B   旧逐 token runner
qwen35-run.sh                492 B   qwen35-run.py 的 exec 包装
qwen35-serve.py           18,961 B   旧 OpenAI HTTP server（CPU/ORT 后端）
reference.json               606 B   qwen35-serve.py --selftest 的黄金参考
tokenizer.json.gz      3,516,051 B   旧 tokenizer（新方案用 common/tokenizer.json）
```

互引链 `qwen35-model-install.sh → qwen35-ready.py → qwen35-run.sh → qwen35-run.py`
整条一起删；`qwen35-serve.py` 引用的 `reference.json` / `tokenizer.json.gz` 也一起删。

删除前 grep 验证（源码范围内已无引用）：

```
$ grep -rn "qwen35-model-install\|qwen35-ready\.py\|qwen35-run\.sh\|qwen35-run\.py\|\
qwen35-serve\.py\|qwen35-convert\.sh\|qwen35-rt\.py\|reference\.json\|tokenizer\.json\.gz" \
  --include="*.bb" --include="*.bbappend" --include="*.sh" --include="*.py" \
  --include="*.service" --include="*.conf" --include="*.inc" .
```

命中只出现在两处，都**不是**源码引用：

- `build-qcom-wayland/tmp-glibc/work/.../quecpi-image/1.0/rootfs/usr/share/qwen35-npu/tools/`
  —— 上一次构建的 rootfs 产物（workdir 缓存，下次 bitbake 会被替换），
- `qwen35-npu/files/CLEANUP.md` / `PORT_NOTES.md` —— 就是记录「这些文件要删」的
  历史文档，保留其原文。

`qwen35-npu_1.0.bb` 的 `SRC_URI` / `do_install` 完全没有引用这 9 个文件（未改动，已确认）。

### 保留

```
CLEANUP.md  LICENSE  NOTICE  PORT_NOTES.md  README.md
qwen35-launch.sh  qwen35-mkconfig.py  qwen35.service
configs/2k.json
native/  (qwen_text_engine.cpp, qnn_memory.cpp, *.h, ort_include/)
runtime/ (qwen_server.py + 11 个模块)
```

---

## C. `.gitignore` 重写

旧内容说的是 CPU 方案产物（`qwen08b_i8.onnx`、725 MB prefill、764 MB QNN context、
508 MB fp16 embedding、`files/model/` 由 GitHub release 下载）。新内容说明：

- 1.8 GB 资产是上游构建产物，**永不进本目录**，由 `qwen35-npu-model` recipe 在
  `do_fetch` 阶段按 commit 从 HuggingFace
  （`PrismPhi/Qwen3.5-0.8B-Radxa-Dragon-Q6A-QCS6490-QNN-NPU`）下载并逐文件校验 sha256，
  只缓存于 `DL_DIR`，运行时安装到 `/usr/share/qwen35`；
- 明确写了「不再有本地转换 / 手工放 model 的路径」，CPU int8 方案已移除；
- `files/model/` 忽略规则保留，但注明只是防止陈旧工作区误提交（已废弃）；
- 保留 `__pycache__/` 和 `*.pyc`。

---

## 未做的事（按任务约束）

- 没有改 `qwen35-npu_1.0.bb`（任务 A 只需匹配 bb 已经确定的布局；
  `do_install` 里 `qwen35-launch.sh`/`runtime.json`/`qwen35.service` 的位置与新常量一致）。
- 没有跑 `bitbake` / `buildall` / `buildpackage` / `buildconfig`。
- 没有上板验证（板子不稳，板端联调由主控统一安排）。上板后请看：
  `ls /usr/share/qwen35/{common,models/2k,contracts/2k}` 与
  `/usr/lib/qwen35/qwen35-launch.sh` 是否齐全，再跑 `qwen35.service`。
