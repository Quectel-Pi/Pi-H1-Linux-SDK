// npu_daemon - 常驻 SNPE 会话, 消除每次点击的进程启动/图初始化 (~280ms -> 0)
// 协议: stdin 连续喂 float32 NHWC raw (640*640*3*4 = 4915200 字节),
//       stdout 每帧回一行 "OK <ms>", 输出张量写到 <outdir>/<name>.raw
// 用法: npu_daemon <model.dlc> <outdir> [perf_profile] [shm_path] [dsp|gpu|cpu]
#include <SNPE/SNPE.hpp>
#include <SNPE/SNPEBuilder.hpp>
#include <SNPE/SNPEFactory.hpp>
#include <DlContainer/IDlContainer.hpp>
#include <DlSystem/ITensorFactory.hpp>
#include <DlSystem/TensorMap.hpp>
#include <DlSystem/StringList.hpp>
#include <DlSystem/RuntimeList.hpp>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string>
#include <vector>

static const char* OUT_NAMES[] = {"boxes", "scores", "class_idx"};

static bool writeFile(const std::string& path, const void* data, size_t bytes) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t n = fwrite(data, 1, bytes, f);
    fclose(f);
    return n == bytes;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <model.dlc> <outdir> [perf_profile]\n", argv[0]);
        return 2;
    }
    std::string outdir = argv[2];

    auto container = zdl::DlContainer::IDlContainer::open(argv[1]);
    if (!container) { fprintf(stderr, "ERR open container\n"); return 3; }

    zdl::DlSystem::StringList outNames;
    for (const char* n : OUT_NAMES) outNames.append(n);

    zdl::DlSystem::StringList outLayers;
    zdl::DlSystem::RuntimeList runtimeList;
    // 第 5 个参数选后端, 默认 dsp. 三种各起一个常驻进程, 才好在同一口径下
    // 比端到端延迟 (GPU 每次新建会话要花 1.35s 编 OpenCL kernel, 摊销掉才公平).
    const char* rt = (argc > 5) ? argv[5] : "dsp";
    zdl::DlSystem::Runtime_t primary = zdl::DlSystem::Runtime_t::DSP;
    if (!strcmp(rt, "gpu")) primary = zdl::DlSystem::Runtime_t::GPU;
    else if (!strcmp(rt, "cpu")) primary = zdl::DlSystem::Runtime_t::CPU;
    runtimeList.add(primary);
    if (primary != zdl::DlSystem::Runtime_t::CPU)     // CPU 已是兜底, 不重复加
        runtimeList.add(zdl::DlSystem::Runtime_t::CPU);

    // DVFS 上不来时单帧只有 ~0.8GHz; burst 让 DSP 一上来就冲高频
    const char* prof = (argc > 3) ? argv[3] : "burst";
    zdl::DlSystem::PerformanceProfile_t perf = zdl::DlSystem::PerformanceProfile_t::BURST;
    if (!strcmp(prof, "default")) perf = zdl::DlSystem::PerformanceProfile_t::DEFAULT;
    else if (!strcmp(prof, "sustained")) perf = zdl::DlSystem::PerformanceProfile_t::SUSTAINED_HIGH_PERFORMANCE;
    else if (!strcmp(prof, "high")) perf = zdl::DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE;

    auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<zdl::SNPE::SNPE> snpe = zdl::SNPE::SNPEBuilder(container.get())
        .setRuntimeProcessorOrder(runtimeList)
        .setOutputTensors(outNames)
        .setPerformanceProfile(perf)
        .setInitCacheMode(true)
        .build();
    if (!snpe) { fprintf(stderr, "ERR build snpe\n"); return 4; }
    double init_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    auto inNames = snpe->getInputTensorNames();
    if (!inNames) { fprintf(stderr, "ERR input names\n"); return 5; }
    const char* inName = (*inNames).at(0);
    auto inDims = snpe->getInputDimensions(inName);
    if (!inDims) { fprintf(stderr, "ERR input dims\n"); return 6; }

    auto& factory = zdl::SNPE::SNPEFactory::getTensorFactory();
    std::unique_ptr<zdl::DlSystem::ITensor> input(factory.createTensor(*inDims));
    size_t inBytes = input->getSize() * sizeof(float);

    // 第 4 个参数: 共享内存输入路径. 4.9MB float32 走管道要 25ms 的
    // write+copy, 走 mmap 只付一次 memcpy (~2ms). 没有该参数就退回管道.
    void* mapped = MAP_FAILED;
    if (argc > 4) {
        int fd = open(argv[4], O_RDWR);
        if (fd >= 0) {
            mapped = mmap(nullptr, inBytes, PROT_READ, MAP_SHARED, fd, 0);
            if (mapped == MAP_FAILED) { perror("mmap"); return 7; }
        }
    }
    bool useShm = (mapped != MAP_FAILED);

    fprintf(stdout, "READY %.1f %.zu %d\n", init_ms, inBytes, useShm ? 1 : 0);
    fflush(stdout);

    std::vector<char> buf(inBytes);
    while (true) {
        if (useShm) {
            // GUI 填好共享内存后只发 1 个字节当门铃
            unsigned char bell;
            if (fread(&bell, 1, 1, stdin) != 1) return 0;
            std::copy((const char*)mapped, (const char*)mapped + inBytes,
                      reinterpret_cast<char*>(&*input->begin()));
        } else {
            size_t got = 0;
            while (got < inBytes) {
                size_t n = fread(buf.data() + got, 1, inBytes - got, stdin);
                if (n == 0) return 0;          // GUI 关闭了管道
                got += n;
            }
            std::copy(buf.data(), buf.data() + inBytes,
                      reinterpret_cast<char*>(&*input->begin()));
        }

        zdl::DlSystem::TensorMap inMap, outMap;
        inMap.add(inName, input.get());

        auto t1 = std::chrono::steady_clock::now();
        bool ok = snpe->execute(inMap, outMap);
        double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t1).count();
        if (!ok) { fprintf(stdout, "ERR execute\n"); fflush(stdout); continue; }

        for (const char* name : OUT_NAMES) {
            auto t = outMap.getTensor(name);
            if (!t) { fprintf(stdout, "ERR tensor %s\n", name); fflush(stdout); continue; }
            writeFile(outdir + "/" + name + ".raw", &*t->begin(),
                      t->getSize() * sizeof(float));
        }
        fprintf(stdout, "OK %.2f\n", ms);
        fflush(stdout);
    }
}
