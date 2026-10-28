# npu-llm — LLM on the QCS6490 NPU (Hexagon V68)

Two pre-compiled LLMs that actually run on the Hexagon, a CLI launcher, a
fullscreen GTK4 chat UI with offline speech in and out, packaged both as a deb
and as a Yocto recipe.

```
layers/meta-quectel/recipes-quectel/npu-llm/
  npu-llm_1.0.bb      Yocto recipe: pulls the bundles, installs the runtime
  build-deb.sh        same payload as a deb (for adb install, no reflash)
  README.md           this file
  files/
    npu-llm             CLI launcher  -> /usr/bin/npu-llm
    npu-chat            GTK4 chat UI  -> /usr/bin/npu-chat
    npu-chat.desktop    menu entry / autostart template
    npu-chat.svg        icon
    README.Debian       shipped in the deb as the package docs
```

## Models

| model          | ctx  | prefill   | decode     | payload |
|----------------|------|-----------|------------|---------|
| qwen2.5-0.5b   | 1024 | 160 tok/s | 20.3 tok/s | 640 MB  |
| llama3.2-1b    | 4096 | 48.6 tok/s| 7.9 tok/s  | 1.8 GB  |

Both are QAIRT context binaries compiled for V68 (`dsp_arch: v68`, `soc_id 35`)
— the only model format the Hexagon on this SoC can load. Sources:
`radxa/Qwen2.5-0.5B-v68` and `radxa/Llama3.2-1B-4096-qairt-v68` on ModelScope.

## Voice

Offline, on the CPU, independent of the NPU:

| part | model                                        | speed        | payload |
|------|----------------------------------------------|--------------|---------|
| ASR  | SenseVoice small int8, zh/en/ja/ko/yue       | ~17x RT      | 228 MB  |
| TTS  | Matcha-TTS zh+en + vocos 16 kHz, 4 threads   | ~10x RT      | 125 MB  |

TTS is one female voice that reads both Chinese and English. It replaced
`vits-zh-hf-fanchen-C`, whose lexicon is a Chinese character table with no
Latin entry at all: every English word was logged as `OOV` and dropped, so an
English answer was read out as 0.1 s of near-silence. The Matcha model needs
the 16 kHz universal vocoder (`vocos-16khz-univ.onnx`, sherpa-onnx github
release — it refuses to load without it) and `espeak-ng-data` for the Latin
side. Cost of the swap, measured on the board over the same nine sentences:
248–258 MB RSS against 220–300 MB, 0.33 s instead of 1.99 s to speak a 4 s
mixed sentence, and 2.1 s instead of 1.1 s to load. Memory went down because
the VITS allocates as it speaks; speed went up ~6x.

Both come from the `sherpa-onnx` python bindings (cp313 manylinux aarch64
wheels, unpacked into `/usr/lib/npu-llm/voice`) plus models from hf-mirror
(`csukuangfj/…`). In `npu-chat`: click the mic to start, click again to stop
and recognise, then the transcript goes through the normal send path; the
answer is spoken sentence by sentence while it streams. Audio goes through
PipeWire (`pw-record`/`pw-play`) — this image has no ALSA tools.

```bash
npu-chat --voice-selftest    # record 4 s, transcribe, speak; needs a session
```

GGUF is not an option here. The ggml-hexagon backend in geniex ships HTP
v73/v75/v79/v81 only and caps v68 to v73, which fails at session setup
(`dspqueue_read failed: 0x0000002e`). Qwen3.5 is also architecturally out of
reach (qwen3_5 has linear_attention layers; libQnnGenAiTransformer supports
GPT2/GPTJ/LLAMA/QWEN graphs only) — it stays on CPU at ~7 tok/s.

## Build the deb

```bash
./layers/meta-quectel/recipes-quectel/npu-llm/build-deb.sh
# -> downloads/npu-llm_1.0_arm64.deb   (~2.7 GB, cache in downloads/npu-llm-cache/)
```

`--no-download` rebuilds from the cache, `--output DIR` changes the target.

## Install on the board

```bash
./quectel_build/tools/adb -s <serial> push downloads/npu-llm_1.0_arm64.deb /tmp/
./quectel_build/tools/adb -s <serial> shell dpkg -i /tmp/npu-llm_1.0_arm64.deb
```

Then, from the board's GNOME session (user `p`, tty2):

```
npu-chat                                # fullscreen chat
npu-llm llama3.2-1b -p "hello"          # one shot, CLI
```

```bash
npu-llm qwen2.5-0.5b -i                 # terminal chat (serial / ssh / adb shell)
npu-llm qwen2.5-0.5b -p "写一首关于秋天的短诗"
npu-llm list
npu-chat                                # GTK4 fullscreen UI, needs a session
```

`-i` is a plain stdin/stdout REPL — no display server involved, so it works over
the serial console or `adb shell` (and it is what to use when the board sits on
the GDM greeter with no session). It carries the last 6 turns; each turn reloads
the context binary (1.2 s) because the runtime has no session API.

Smoke test without a mouse:

```bash
npu-chat --selftest                      # prompt/extract logic, no NPU
npu-chat --ask "用两句话解释天空为什么是蓝色的" --shot /tmp/x.png
```

`--ask` prints `ANSWER (4.6s): ...` plus the status line, `--shot` also writes a
PNG of the window (the only way to grab a Wayland frame here — GNOME disables
the D-Bus screenshot API for non-portal callers). Both need a session: after a
reboot the board sits on the GDM greeter, so log in as `p` on tty1 first.

Sampling is patched to top-k 40 (the bundle ships greedy top-k 1, which loops
on the 1B model); see README.Debian.

## SDK integration

```
layers/meta-quectel/recipes-quectel/npu-llm/npu-llm_1.0.bb   fetches + installs
layers/meta-quectel/recipes-products/images/qcom-multimedia-image.bbappend
                                                            IMAGE_INSTALL += npu-llm
```

The recipe takes the launchers from `files/`, which are symlinks back into this
directory, so the deb and the image ship identical scripts. The 2.3 GB of models
and QNN libraries are fetched from ModelScope by SRC_URI (sha256 pinned) and
cached in `DL_DIR` after the first build.

Payload lands in `/usr/lib/npu-llm` (genie-t2t-run, private libGenie/libQnnHtp)
and `/usr/share/npu-llm/<model>` (config, tokenizer, context binary). Both the
recipe and the deb use the same paths, so `npu-chat` behaves identically either
way.
