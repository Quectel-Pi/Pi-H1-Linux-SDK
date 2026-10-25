// Optional standard-QNN Add for the measured LM-head zero offset.
#pragma once
struct LogitOffset {
  std::unique_ptr<Ort::Session> session;
  Ort::MemoryInfo info{"QnnHtpShared", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeCPU};
  std::unique_ptr<Ort::Allocator> allocator;
  std::unique_ptr<Ort::MemoryAllocation> output;
  Ort::Value value{nullptr};
  std::unique_ptr<Ort::IoBinding> binding;
  float* data{};
  size_t runs{};
  double create_seconds{}, compute_seconds{};

  LogitOffset(Ort::Env& env, const std::vector<Ort::ConstEpDevice>& devices,
                       const fs::path& model, const fs::path& backend, const Ort::Value& input) {
    Ort::SessionOptions options;
    options.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
    options.SetIntraOpNumThreads(1);
    options.SetInterOpNumThreads(1);
    options.DisableCpuMemArena();
    options.DisableMemPattern();
    options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
    options.AddConfigEntry("ep.share_ep_contexts", "1");
    options.AddConfigEntry("ep.context_enable", "0");
    if (const char* path = std::getenv("QWEN35_LOGIT_PROFILE")) options.EnableProfiling(path);
    options.AppendExecutionProvider_V2(env, devices, {
      {"backend_path", backend.string()}, {"offload_graph_io_quantization", "0"},
      {"htp_performance_mode", "burst"}, {"enable_htp_shared_memory_allocator", "1"}});
    const auto started = Clock::now();
    session = std::make_unique<Ort::Session>(env, model.c_str(), options);
    create_seconds = Seconds(started, Clock::now());
    Ort::AllocatorWithDefaultOptions names;
    const std::vector<int64_t> shape{1, 1, 248320};
    if (session->GetInputCount() != 1 || session->GetOutputCount() != 1 ||
        std::string(session->GetInputNameAllocated(0, names).get()) != "logits_in" ||
        std::string(session->GetOutputNameAllocated(0, names).get()) != "logits_out" ||
        GetShape(*session, true, 0) != shape || GetShape(*session, false, 0) != shape ||
        GetElementType(*session, true, 0) != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        GetElementType(*session, false, 0) != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        input.GetTensorTypeAndShapeInfo().GetShape() != shape)
      throw std::runtime_error("logit correction ABI mismatch");
    allocator = std::make_unique<Ort::Allocator>(*session, info);
    output = std::make_unique<Ort::MemoryAllocation>(allocator->GetAllocation(248320 * sizeof(float)));
    data = static_cast<float*>(output->get());
    value = Ort::Value::CreateTensor(info, data, 248320 * sizeof(float), shape.data(), shape.size(),
                                    ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
    binding = std::make_unique<Ort::IoBinding>(*session);
    binding->BindInput("logits_in", input);
    binding->BindOutput("logits_out", value);
  }
  void ResetStats() { runs = 0; compute_seconds = 0.0; }
  const float* Run() {
    const auto started = Clock::now();
    binding->SynchronizeInputs();
    Ort::RunOptions options;
    session->Run(options, *binding);
    binding->SynchronizeOutputs();
    ++runs;
    compute_seconds += Seconds(started, Clock::now());
    return data;
  }
};
