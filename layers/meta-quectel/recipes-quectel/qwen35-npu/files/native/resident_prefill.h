#include "exact_prefix.h"
// Standard QNN sessions and host state handoff; no learned CPU operations.
#pragma once
#include <cstdlib>

struct SharedPrompt {
  MappedFile file;
  size_t sequence{};
  int32_t rope_delta{};
  const int64_t* ids{};
  const float *hidden{}, *cosine{}, *sine{};

  static uint64_t U64(const std::byte* p) {
    return uint64_t(ReadLittle32(p)) | (uint64_t(ReadLittle32(p + 4)) << 32);
  }
  static size_t Align(size_t value) { return (value + 63) & ~size_t(63); }
  explicit SharedPrompt(const fs::path& path) : file(path) {
    constexpr size_t header_bytes = 80;
    if (file.bytes < header_bytes || std::memcmp(file.data, "R20VLM1\0", 8) != 0)
      throw std::runtime_error("native Prefill prompt header magic/size mismatch");
    auto u32 = [&](size_t offset) { return ReadLittle32(file.data + offset); };
    sequence = u32(16);
    rope_delta = static_cast<int32_t>(u32(20));
    if (u32(8) != 1 || u32(12) != header_bytes || sequence < 1 || sequence >= 2048 ||
        u32(24) != 1024 || u32(28) != 64 || u32(32) != 248320 || u32(36) != 0 ||
        int64_t(sequence) + rope_delta < 0 || int64_t(sequence) + rope_delta >= 2048)
      throw std::runtime_error("native Prefill prompt dimensions/version mismatch");
    const size_t io = Align(header_bytes);
    const size_t ho = Align(io + sequence * sizeof(int64_t));
    const size_t co = Align(ho + sequence * 1024 * sizeof(float));
    const size_t so = Align(co + sequence * 64 * sizeof(float));
    const size_t total = so + sequence * 64 * sizeof(float);
    if (U64(file.data + 40) != io || U64(file.data + 48) != ho ||
        U64(file.data + 56) != co || U64(file.data + 64) != so ||
        U64(file.data + 72) != total || file.bytes != total)
      throw std::runtime_error("native Prefill prompt offsets mismatch");
    ids = reinterpret_cast<const int64_t*>(file.data + io);
    hidden = reinterpret_cast<const float*>(file.data + ho);
    cosine = reinterpret_cast<const float*>(file.data + co);
    sine = reinterpret_cast<const float*>(file.data + so);
    for (size_t i = 0; i < sequence; ++i)
      if (ids[i] < 0 || ids[i] >= 248320) throw std::runtime_error("native Prefill invalid prompt ID");
    for (size_t i = 0; i < sequence * 1024; ++i)
      if (!std::isfinite(hidden[i])) throw std::runtime_error("native Prefill nonfinite embedding");
    for (size_t i = 0; i < sequence * 64; ++i)
      if (!std::isfinite(cosine[i]) || !std::isfinite(sine[i]))
        throw std::runtime_error("native Prefill nonfinite rotary input");
  }
};

struct ResidentPrefill {
  struct Tensor {
    std::string name;
    std::vector<int64_t> shape;
    size_t elements{}, offset{};
    float* data{};
    Ort::Value value{nullptr};
  };
  std::unique_ptr<Ort::Session> session;
  Ort::MemoryInfo memory_info{"QnnHtpShared", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeCPU};
  std::unique_ptr<Ort::Allocator> allocator;
  std::vector<Ort::MemoryAllocation> arenas;
  std::vector<Tensor> inputs, outputs;
  std::unordered_map<std::string, size_t> in_index, out_index;
  std::unique_ptr<Ort::IoBinding> binding;
  std::unique_ptr<SharedPrompt> prompt;
  // Retain only the previous input map, not another copy of model states.
  std::unique_ptr<SharedPrompt> cached_prompt;
  size_t cached_prefix{}, reused_prefix{};
  double cache_match_seconds{};
  bool prefix_cache_enabled = [] {
    const char* value = std::getenv("QWEN35_EXACT_PREFIX_CACHE");
    if (!value || std::string_view(value) == "1") return true;
    if (std::string_view(value) == "0") return false;
    throw std::runtime_error("QWEN35_EXACT_PREFIX_CACHE must be 0 or 1");
  }();
  std::string pending_id;
  fs::path debug_directory;
  size_t prefix{};
  double create_seconds{}, compute_seconds{}, preparation_seconds{}, seed_seconds{};
  size_t clipped{}, quantized{}, input_bytes{}, output_bytes{};
  double max_local_clip_fraction{};

  static std::string Layer(size_t n) {
    return "layer_" + std::string(n < 10 ? "0" : "") + std::to_string(n);
  }
  static void CheckRequestId(const std::string& id) {
    if (id.empty() || id.size() > 128 ||
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
      throw std::runtime_error("native Prefill request ID is invalid");
  }
  static size_t IndexInName(const std::string& name, const std::string& marker) {
    const auto position = name.find(marker);
    if (position == std::string::npos) throw std::runtime_error("missing state selector: " + name);
    size_t consumed = 0;
    const size_t result = std::stoull(name.substr(position + marker.size()), &consumed);
    if (consumed == 0) throw std::runtime_error("invalid state selector: " + name);
    return result;
  }
  float* In(const std::string& name) { return inputs.at(in_index.at(name)).data; }
  const Tensor& Out(const std::string& name) const { return outputs.at(out_index.at(name)); }

  ResidentPrefill(Ort::Env& env, const std::vector<Ort::ConstEpDevice>& devices,
                     const fs::path& model, const fs::path& backend) {
    Ort::SessionOptions options;
    options.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
    options.SetIntraOpNumThreads(1);
    options.SetInterOpNumThreads(1);
    options.DisableCpuMemArena();
    options.DisableMemPattern();
    options.AddConfigEntry("session.intra_op.allow_spinning", "0");
    options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
    options.AddConfigEntry("ep.share_ep_contexts", "1");
    options.AddConfigEntry("ep.context_enable", "0");
    if (const char* profile = std::getenv("QWEN35_PREFILL_PROFILE")) options.EnableProfiling(profile);
    const std::unordered_map<std::string, std::string> providers = {
        {"backend_path", backend.string()}, {"offload_graph_io_quantization", "0"},
        {"htp_performance_mode", "burst"}, {"enable_htp_shared_memory_allocator", "1"}};
    options.AppendExecutionProvider_V2(env, devices, providers);
    const auto started = Clock::now();
    session = std::make_unique<Ort::Session>(env, model.c_str(), options);
    create_seconds = Seconds(started, Clock::now());
    if (session->GetInputCount() != 67 || session->GetOutputCount() != 72)
      throw std::runtime_error("native Prefill expects the full24 67/72 ABI");
    allocator = std::make_unique<Ort::Allocator>(*session, memory_info);
    if (allocator->GetInfo().GetAllocatorName() != "QnnHtpShared")
      throw std::runtime_error("native Prefill allocator is not QnnHtpShared");
    std::unordered_map<std::string, std::vector<int64_t>> expected_in, expected_out;
    expected_in["hidden_in"] = {1, 16, 1024};
    for (size_t layer = 0; layer < 24; ++layer) {
      const auto name = Layer(layer);
      expected_out[name + "_hidden_out"] = {1, 16, 1024};
      if (layer % 4 != 3) {
        expected_in[name + "_conv_state_in"] = expected_out[name + "_conv_state_out"] = {1, 6144, 3};
        expected_in[name + "_recurrent_state_in"] = expected_out[name + "_recurrent_state_out"] = {1, 16, 128, 128};
      } else {
        for (const auto* kind : {"k", "v"}) {
          expected_in[name + "_past_" + kind + "_in"] = {1, 2, 2032, 256};
          expected_out[name + "_present_" + kind] = {1, 2, 2048, 256};
        }
        expected_in[name + "_rope_cos"] = expected_in[name + "_rope_sin"] = {1, 1, 16, 64};
        expected_in[name + "_tail_mask"] = {1, 1, 16, 2048};
      }
    }
    Ort::AllocatorWithDefaultOptions names_allocator;
    auto allocate = [&](bool is_input, auto& tensors, auto& index, const auto& expected) {
      const size_t count = is_input ? session->GetInputCount() : session->GetOutputCount();
      size_t bytes = 0;
      tensors.reserve(count);
      for (size_t i = 0; i < count; ++i) {
        auto allocated_name = is_input ? session->GetInputNameAllocated(i, names_allocator)
                                       : session->GetOutputNameAllocated(i, names_allocator);
        Tensor tensor;
        tensor.name = allocated_name.get();
        tensor.shape = GetShape(*session, is_input, i);
        if (GetElementType(*session, is_input, i) != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
            !expected.contains(tensor.name) || expected.at(tensor.name) != tensor.shape || index.contains(tensor.name))
          throw std::runtime_error("native Prefill tensor ABI mismatch: " + tensor.name);
        tensor.elements = ShapeElements(tensor.shape);
        tensor.offset = SharedPrompt::Align(bytes);
        bytes = tensor.offset + tensor.elements * sizeof(float);
        index.emplace(tensor.name, i);
        tensors.push_back(std::move(tensor));
      }
      auto arena = allocator->GetAllocation(bytes);
      auto* base = static_cast<std::byte*>(arena.get());
      if (!base || reinterpret_cast<uintptr_t>(base) % 64)
        throw std::runtime_error("native Prefill arena alignment mismatch");
      std::memset(base, 0, bytes);
      for (auto& tensor : tensors) {
        tensor.data = reinterpret_cast<float*>(base + tensor.offset);
        tensor.value = Ort::Value::CreateTensor(memory_info, tensor.data, tensor.elements * sizeof(float),
                                               tensor.shape.data(), tensor.shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT);
      }
      arenas.push_back(std::move(arena));
      return bytes;
    };
    input_bytes = allocate(true, inputs, in_index, expected_in);
    output_bytes = allocate(false, outputs, out_index, expected_out);
    binding = std::make_unique<Ort::IoBinding>(*session);
    for (const auto& tensor : inputs) binding->BindInput(tensor.name.c_str(), tensor.value);
    for (const auto& tensor : outputs) binding->BindOutput(tensor.name.c_str(), tensor.value);
  }

  void Invalidate() {
    pending_id.clear(); prompt.reset(); prefix = 0; debug_directory.clear();
    cached_prompt.reset(); cached_prefix = reused_prefix = 0;
  }
  void Advance() {
    for (size_t layer = 0; layer < 24; ++layer) {
      const auto name = Layer(layer);
      if (layer % 4 != 3) {
        for (const auto* kind : {"conv", "recurrent"}) {
          const auto& source = Out(name + "_" + kind + "_state_out");
          std::memcpy(In(name + "_" + kind + "_state_in"), source.data, source.elements * sizeof(float));
        }
      } else {
        for (const auto* kind : {"k", "v"}) {
          const float* source = Out(name + "_present_" + kind).data;
          float* destination = In(name + "_past_" + kind + "_in");
          for (size_t head = 0; head < 2; ++head)
            std::memcpy(destination + head * 2032 * 256, source + (head * 2048 + 16) * 256,
                        2032 * 256 * sizeof(float));
        }
      }
    }
  }
  void Run(const std::string& id, size_t count, const fs::path& bundle, const fs::path& debug) {
    // A pending handoff is single-use. The cache is a separate completed prefix.
    pending_id.clear(); prompt.reset(); prefix = 0; debug_directory.clear();
    CheckRequestId(id);
    auto input_prompt = std::make_unique<SharedPrompt>(bundle);
    if (count < 16 || count > 2032 || count % 16 || count >= input_prompt->sequence ||
        input_prompt->sequence - count > 64)
      throw std::runtime_error("native Prefill prefix/suffix length mismatch");
    if (debug != "-") {
      const auto target = fs::absolute(debug).lexically_normal();
      if (target.string().rfind("/dev/shm/new_Qwen_35_npu", 0) != 0 || fs::exists(target))
        throw std::runtime_error("native Prefill debug directory must be new project scratch");
      fs::create_directories(target);
      debug_directory = target;
    }
    const auto match_start = Clock::now();
    reused_prefix = 0;
    if (prefix_cache_enabled && debug == "-" && cached_prompt &&
        PrefixPrefixMatches(cached_prefix, count,
          cached_prompt->sequence, cached_prompt->ids, cached_prompt->hidden,
          cached_prompt->cosine, cached_prompt->sine,
          input_prompt->sequence, input_prompt->ids, input_prompt->hidden,
          input_prompt->cosine, input_prompt->sine)) reused_prefix = cached_prefix;
    cached_prompt.reset(); cached_prefix = 0;  // No stale state if Run/Seed fails.
    cache_match_seconds = Seconds(match_start, Clock::now());
    if (!reused_prefix)
      for (auto& tensor : inputs) std::fill_n(tensor.data, tensor.elements, 0.0f);
    compute_seconds = preparation_seconds = seed_seconds = 0.0;
    clipped = quantized = 0;
    max_local_clip_fraction = 0.0;
    for (size_t start = reused_prefix; start < count; start += 16) {
      const auto preparation_start = Clock::now();
      if (start) Advance();
      std::memcpy(In("hidden_in"), input_prompt->hidden + start * 1024, 16 * 1024 * sizeof(float));
      for (size_t layer = 3; layer < 24; layer += 4) {
        const auto name = Layer(layer);
        std::memcpy(In(name + "_rope_cos"), input_prompt->cosine + start * 64, 16 * 64 * sizeof(float));
        std::memcpy(In(name + "_rope_sin"), input_prompt->sine + start * 64, 16 * 64 * sizeof(float));
        float* mask = In(name + "_tail_mask");
        std::fill_n(mask, 16 * 2048, -64.0f);
        for (size_t row = 0; row < 16; ++row)
          std::fill(mask + row * 2048 + 2032 - start, mask + row * 2048 + 2032 + row + 1, 0.0f);
      }
      preparation_seconds += Seconds(preparation_start, Clock::now());
      const auto run_start = Clock::now();
      binding->SynchronizeInputs();
      Ort::RunOptions run_options;
      session->Run(run_options, *binding);
      binding->SynchronizeOutputs();
      compute_seconds += Seconds(run_start, Clock::now());
      for (size_t layer = 0; layer < 24; ++layer) {
        const auto& value = Out(Layer(layer) + "_hidden_out");
        for (size_t i = 0; i < value.elements; ++i)
          if (!std::isfinite(value.data[i])) throw std::runtime_error("native Prefill nonfinite hidden");
      }
    }
    for (const auto& tensor : outputs)
      for (size_t i = 0; i < tensor.elements; ++i)
        if (!std::isfinite(tensor.data[i])) throw std::runtime_error("native Prefill nonfinite final state");
    prompt = std::move(input_prompt);
    prefix = count;
    pending_id = id;
    if (!debug_directory.empty()) {
      std::ofstream values(debug_directory / "prefill_outputs.bin", std::ios::binary);
      std::ofstream layout(debug_directory / "prefill_outputs.tsv");
      size_t offset = 0;
      layout << "name\toffset\tbytes\n";
      for (const auto& tensor : outputs) {
        const size_t bytes = tensor.elements * sizeof(float);
        values.write(reinterpret_cast<const char*>(tensor.data), bytes);
        layout << tensor.name << '\t' << offset << '\t' << bytes << '\n';
        offset += bytes;
      }
      if (!values || !layout) throw std::runtime_error("native Prefill diagnostic output failed");
    }
  }

  uint16_t Quantize(float value, const AdvanceRecord& plan) {
    if (!std::isfinite(value) || !(plan.input_scale > 0.0f))
      throw std::runtime_error("native Prefill invalid state encoding");
    const float scaled = value / plan.input_scale;
    const float rounded = std::nearbyintf(scaled);
    const double code = static_cast<double>(rounded) + static_cast<double>(plan.input_zero_point);
    ++quantized;
    if (code < 0.0) { ++clipped; return 0; }
    if (code > 65535.0) { ++clipped; return 65535; }
    return static_cast<uint16_t>(code);
  }

  size_t Seed(const std::string& tag, const std::string& id, size_t position,
              const std::vector<int64_t>& history, const std::vector<TensorRecord>& contract,
              const std::vector<AdvanceRecord>& plans, const std::vector<void*>& pointers) {
    if (!prompt || pending_id.empty() || id != pending_id || tag != "shared-resident:" + id ||
        position != prefix || history.size() != prompt->sequence ||
        !std::equal(history.begin(), history.end(), prompt->ids))
      throw std::runtime_error("native Prefill stale/mismatched resident state");
    pending_id.clear();  // one use, including on an encoding error
    const auto started = Clock::now();
    std::unordered_map<std::string, const AdvanceRecord*> by_destination;
    for (const auto& plan : plans) by_destination.emplace(plan.destination, &plan);
    for (size_t index = 0; index < kExpectedInputs; ++index) {
      const size_t clipped_before = clipped, quantized_before = quantized;
      const auto& tensor = contract.at(index);
      void* destination = pointers.at(index);
      const std::string& name = tensor.name;
      if (name == "hidden_in") std::memcpy(destination, prompt->hidden + prefix * 1024, 1024 * sizeof(float));
      else if (name == "rope_cos") std::memcpy(destination, prompt->cosine + prefix * 64, 64 * sizeof(float));
      else if (name == "rope_sin") std::memcpy(destination, prompt->sine + prefix * 64, 64 * sizeof(float));
      else if (name == "tail_mask") {
        auto* mask = static_cast<float*>(destination);
        std::fill_n(mask, 2048, -64.0f);
        std::fill(mask + 2048 - prefix - 1, mask + 2048, 0.0f);
      } else {
        const size_t layer = IndexInName(name, "layer_");
        if (layer >= 24) throw std::runtime_error("native Prefill invalid layer");
        const std::string base = Layer(layer);
        if (name == base + "_conv_state_in") {
          const auto& source = Out(base + "_conv_state_out");
          if (tensor.dtype != "float32" || tensor.elements != source.elements)
            throw std::runtime_error("native Prefill convolution state shape mismatch");
          std::memcpy(destination, source.data, tensor.bytes);
        } else if (name == base + "_past_k" || name == base + "_past_v") {
          const auto& plan = *by_destination.at(name);
          const auto& source = Out(base + "_present_" + name.back());
          if (tensor.dtype != "uint16" || tensor.shape != std::vector<int64_t>({1, 2, 2047, 256}))
            throw std::runtime_error("native Prefill KV state shape mismatch");
          auto* target = static_cast<uint16_t*>(destination);
          for (size_t head = 0; head < 2; ++head)
            for (size_t i = 0; i < 2047 * 256; ++i)
              target[head * 2047 * 256 + i] = Quantize(source.data[(head * 2048 + 1) * 256 + i], plan);
        } else if (tensor.dtype == "uint16" && name.find("state_in_QuantizeLinear_Output") != std::string::npos) {
          const auto& plan = *by_destination.at(name);
          const size_t head = IndexInName(name, "/head_");
          size_t width = 128, column = 0;
          if (name.find("/selective_lane8_refinement/") != std::string::npos) {
            width = 8;
            column = IndexInName(name, "/lane_") * 16 + IndexInName(name, "/half_") * 8;
          } else if (name.find("/value_lane_recurrence/") != std::string::npos) {
            width = 16;
            column = IndexInName(name, "/lane_") * 16;
          } else if (name.find("/headlocal_recurrence/") == std::string::npos) {
            throw std::runtime_error("native Prefill unknown recurrent layout");
          }
          if (head >= 16 || column + width > 128 ||
              tensor.shape != std::vector<int64_t>({1, 1, 128, static_cast<int64_t>(width)}))
            throw std::runtime_error("native Prefill recurrent state shape mismatch");
          const float* source = Out(base + "_recurrent_state_out").data + head * 128 * 128 + column;
          auto* target = static_cast<uint16_t*>(destination);
          for (size_t row = 0; row < 128; ++row)
            for (size_t column_index = 0; column_index < width; ++column_index)
              target[row * width + column_index] = Quantize(source[row * 128 + column_index], plan);
        } else throw std::runtime_error("native Prefill unhandled seed tensor: " + name);
      }
      if (quantized > quantized_before)
        max_local_clip_fraction = std::max(max_local_clip_fraction,
            static_cast<double>(clipped - clipped_before) / (quantized - quantized_before));
    }
    if (quantized == 0 || static_cast<double>(clipped) / quantized > 0.001)
      throw std::runtime_error("native Prefill state clipping exceeds accepted limit");
    seed_seconds = Seconds(started, Clock::now());
    if (!debug_directory.empty()) {
      std::ofstream stream(debug_directory / "p5_seed_inputs.bin", std::ios::binary);
      for (size_t index = 0; index < kExpectedInputs; ++index)
        stream.write(static_cast<const char*>(pointers[index]), contract[index].bytes);
      if (!stream) throw std::runtime_error("native Prefill diagnostic seed output failed");
    }
    // Seed reads outputs; P5 decoding uses separate buffers. Cache only after
    // successful state encoding. Debug runs deliberately leave no reusable cache.
    if (prefix_cache_enabled && debug_directory.empty()) {
      cached_prefix = prefix;
      cached_prompt = std::move(prompt);
    } else prompt.reset();
    debug_directory.clear();
    prefix = 0;
    return kExpectedInputBytes;
  }
};
