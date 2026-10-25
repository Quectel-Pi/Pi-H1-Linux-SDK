// R55 diagnostic decoder derived from the frozen R53 quality engine.
// The R55 candidate binds all 556 recurrent-state/KV graph outputs to
// non-overlapping, 64-byte-aligned slices of one QnnHtpShared arena.  This keeps
// the R53 model/context/advance contract while avoiding 556 extra rpcmem
// allocations.  The arena owns every slice for the complete session lifetime.
#define SEED_COMMAND 1
#define R25_SEEDED_REMAINDER_MAX 64
// runtime persistent token engine built on the frozen runtime P5 exact-LUT runner.
//
// The frozen source is included unchanged so its contract parsers and exact
// UINT16 requantization proof are reused rather than reimplemented.  Its main
// function is renamed and never called.  This file supplies the product-facing
// persistent session and a deliberately small TSV command protocol.

#define main FrozenBenchmarkMain
#include "qnn_memory.cpp"
#undef main

#include <array>
#include <cerrno>
#include <ext/stdio_filebuf.h>
#include <fcntl.h>
#include <queue>
#include <memory>
#include <random>
#include <set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

constexpr size_t kEngineContext = 2048;
constexpr size_t kEngineVocab = 248320;
constexpr size_t kEngineHidden = 1024;
constexpr size_t kEngineRotary = 64;
constexpr size_t kEngineRopeBytes = kEngineContext * kEngineRotary * sizeof(float) * 2;
#ifdef R25_SEEDED_REMAINDER_MAX
constexpr size_t kEngineSeededRemainderMax = R25_SEEDED_REMAINDER_MAX;
#else
constexpr size_t kEngineSeededRemainderMax = 15;
#endif

struct EngineArgs {
  fs::path model;
  fs::path plugin;
  fs::path backend;
  fs::path tensor_contract;
  fs::path advance_plan;
  fs::path embedding_npy;
  fs::path rope_binary;
  std::string state_buffer_mode;
};

struct MappedFile {
  int descriptor{-1};
  size_t bytes{};
  const std::byte* data{};

  explicit MappedFile(const fs::path& path) {
    descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
      throw std::runtime_error("open failed for " + path.string() + ": " +
                               std::strerror(errno));
    }
    struct stat status {};
    if (fstat(descriptor, &status) != 0 || status.st_size <= 0) {
      const std::string error = std::strerror(errno);
      close(descriptor);
      descriptor = -1;
      throw std::runtime_error("fstat failed for " + path.string() + ": " + error);
    }
    bytes = static_cast<size_t>(status.st_size);
    void* mapped = mmap(nullptr, bytes, PROT_READ, MAP_SHARED, descriptor, 0);
    if (mapped == MAP_FAILED) {
      const std::string error = std::strerror(errno);
      close(descriptor);
      descriptor = -1;
      throw std::runtime_error("mmap failed for " + path.string() + ": " + error);
    }
    data = static_cast<const std::byte*>(mapped);
  }

  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;

  ~MappedFile() {
    if (data != nullptr) munmap(const_cast<std::byte*>(data), bytes);
    if (descriptor >= 0) close(descriptor);
  }
};

struct StepTiming {
  double prepare{};
  double run{};
  double advance{};
  double end_to_end{};
};

struct Candidate {
  size_t token{};
  double score{};
  float raw_logit{};
};

struct SampleDecision {
  size_t token{};
  float raw_logit{};
  size_t retained_candidates{};
  std::vector<Candidate> top;
};

EngineArgs ParseEngineArgs(int argc, char** argv) {
  if (argc != 8 && argc != 9) {
    throw std::runtime_error(
        "usage: qwen_qnn_text_engine MODEL PLUGIN BACKEND TENSOR_CONTRACT "
        "ADVANCE_PLAN EMBEDDING_NPY ROPE_CTX2048_BINARY "
        "[state_buffer_mode=aliased|disjoint|arena_disjoint]");
  }
  const std::string mode = argc == 9 ? argv[8] : "aliased";
  if (mode != "aliased" && mode != "disjoint" && mode != "arena_disjoint") {
    throw std::runtime_error(
        "state_buffer_mode must be aliased, disjoint, or arena_disjoint");
  }
  return {argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], mode};
}

uint16_t ReadLittle16(const std::byte* value) {
  return static_cast<uint16_t>(std::to_integer<uint8_t>(value[0])) |
         (static_cast<uint16_t>(std::to_integer<uint8_t>(value[1])) << 8);
}

uint32_t ReadLittle32(const std::byte* value) {
  return static_cast<uint32_t>(std::to_integer<uint8_t>(value[0])) |
         (static_cast<uint32_t>(std::to_integer<uint8_t>(value[1])) << 8) |
         (static_cast<uint32_t>(std::to_integer<uint8_t>(value[2])) << 16) |
         (static_cast<uint32_t>(std::to_integer<uint8_t>(value[3])) << 24);
}

const float* ParseEmbeddingNpy(const MappedFile& mapped) {
  constexpr std::array<uint8_t, 6> magic = {0x93, 'N', 'U', 'M', 'P', 'Y'};
  if (mapped.bytes < 16) throw std::runtime_error("embedding NPY is too short");
  for (size_t index = 0; index < magic.size(); ++index) {
    if (std::to_integer<uint8_t>(mapped.data[index]) != magic[index]) {
      throw std::runtime_error("embedding NPY magic mismatch");
    }
  }
  const uint8_t major = std::to_integer<uint8_t>(mapped.data[6]);
  size_t prefix = 0;
  size_t header_bytes = 0;
  if (major == 1) {
    prefix = 10;
    header_bytes = ReadLittle16(mapped.data + 8);
  } else if (major == 2 || major == 3) {
    prefix = 12;
    header_bytes = ReadLittle32(mapped.data + 8);
  } else {
    throw std::runtime_error("unsupported embedding NPY version");
  }
  if (prefix + header_bytes > mapped.bytes) {
    throw std::runtime_error("embedding NPY header range mismatch");
  }
  const std::string header(reinterpret_cast<const char*>(mapped.data + prefix), header_bytes);
  const bool dtype_ok = header.find("'<f4'") != std::string::npos ||
                        header.find("\"<f4\"") != std::string::npos;
  const bool c_order = header.find("fortran_order': False") != std::string::npos ||
                       header.find("\"fortran_order\": False") != std::string::npos;
  const bool shape_ok = header.find("(248320, 1024)") != std::string::npos ||
                        header.find("(248320,1024)") != std::string::npos;
  const size_t payload_bytes = kEngineVocab * kEngineHidden * sizeof(float);
  if (!dtype_ok || !c_order || !shape_ok || prefix + header_bytes + payload_bytes != mapped.bytes) {
    throw std::runtime_error("embedding NPY dtype/order/shape/size contract mismatch");
  }
  return reinterpret_cast<const float*>(mapped.data + prefix + header_bytes);
}

std::vector<int64_t> ParseIds(std::string_view value, bool allow_empty) {
  std::vector<int64_t> ids;
  if (value.empty()) {
    if (allow_empty) return ids;
    throw std::runtime_error("token ID list is empty");
  }
  for (const auto& field : Split(std::string(value), ',')) {
    if (field.empty()) throw std::runtime_error("empty token ID field");
    size_t parsed = 0;
    const int64_t token = std::stoll(field, &parsed);
    if (parsed != field.size() || token < 0 || token >= static_cast<int64_t>(kEngineVocab)) {
      throw std::runtime_error("token ID is outside the frozen vocabulary");
    }
    ids.push_back(token);
  }
  return ids;
}

bool BetterCandidate(const Candidate& left, const Candidate& right) {
  if (left.score != right.score) return left.score > right.score;
  return left.token < right.token;
}

SampleDecision SelectToken(const float* logits, size_t logits_count,
                           std::string_view mode, const std::vector<int64_t>& history,
                           double temperature, double top_p, size_t top_k, double min_p,
                           double presence_penalty, double repetition_penalty, double frequency_penalty,
                           std::mt19937_64& generator) {
  if (logits_count != kEngineVocab) throw std::runtime_error("logits vocabulary mismatch");
  const bool penalize_history = mode == "sample" || mode == "penalized_greedy";
  std::unordered_map<int64_t, size_t> seen;
  if (penalize_history) {
    seen.reserve(history.size());
    for (int64_t token : history) ++seen[token];
  }
  std::set<size_t> blocked_tokens;
  if (mode == "no_repeat4_greedy" && history.size() >= 3) {
    const size_t prefix = history.size() - 3;
    for (size_t index = 0; index + 3 < history.size(); ++index) {
      if (history[index] == history[prefix] && history[index + 1] == history[prefix + 1] &&
          history[index + 2] == history[prefix + 2]) {
        blocked_tokens.insert(static_cast<size_t>(history[index + 3]));
      }
    }
  }

  // Traverse sparse completion penalties in token order instead of hashing
  // every vocabulary item. Keep only the exact top-k, using a bounded heap.
  std::vector<std::pair<int64_t, size_t>> penalties(seen.begin(), seen.end());
  std::sort(penalties.begin(), penalties.end());
  size_t penalty_index = 0;
  const size_t kept = mode == "sample" ? std::min(top_k, logits_count)
                                        : std::min<size_t>(5, logits_count);
  std::vector<Candidate> candidates;
  candidates.reserve(kept);
  for (size_t token = 0; token < logits_count; ++token) {
    const float raw = logits[token];
    if (!std::isfinite(raw)) throw std::runtime_error("non-finite logits");
    if (blocked_tokens.contains(token)) continue;
    double score = raw;
    if (penalty_index < penalties.size() &&
        penalties[penalty_index].first == static_cast<int64_t>(token)) {
      const size_t occurrences = penalties[penalty_index++].second;
      score -= presence_penalty;
      if (repetition_penalty != 1.0) {
        score = score < 0.0 ? score * repetition_penalty : score / repetition_penalty;
      }
      if (frequency_penalty != 0.0) score -= frequency_penalty * occurrences;
    }
    if (mode == "sample") score /= temperature;
    const Candidate value{token, score, raw};
    if (candidates.size() < kept) {
      candidates.push_back(value);
      std::push_heap(candidates.begin(), candidates.end(), BetterCandidate);
    } else if (kept && BetterCandidate(value, candidates.front())) {
      std::pop_heap(candidates.begin(), candidates.end(), BetterCandidate);
      candidates.back() = value;
      std::push_heap(candidates.begin(), candidates.end(), BetterCandidate);
    }
  }
  std::sort_heap(candidates.begin(), candidates.end(), BetterCandidate);
  if (candidates.empty()) throw std::runtime_error("sampling candidate set is empty");

  if (mode != "sample") {
    return {candidates.front().token, candidates.front().raw_logit, 1, candidates};
  }

  std::vector<double> weights;
  weights.reserve(candidates.size());
  const double maximum = candidates.front().score;
  for (const auto& candidate : candidates) {
    const double weight = std::exp(candidate.score - maximum);
    weights.push_back(weight >= min_p ? weight : 0.0);
  }
  weights.front() = 1.0;
  const double all_weight = std::accumulate(weights.begin(), weights.end(), 0.0);
  if (!(all_weight > 0.0) || !std::isfinite(all_weight)) {
    throw std::runtime_error("invalid sampling probability mass");
  }
  double cumulative = 0.0;
  size_t retained = 0;
  for (size_t index = 0; index < weights.size(); ++index) {
    cumulative += weights[index] / all_weight;
    ++retained;
    if (cumulative >= top_p) break;
  }
  weights.resize(retained);
  candidates.resize(retained);
  const double retained_weight = std::accumulate(weights.begin(), weights.end(), 0.0);
  std::uniform_real_distribution<double> distribution(0.0, retained_weight);
  const double draw = distribution(generator);
  double cursor = 0.0;
  size_t selected = retained - 1;
  for (size_t index = 0; index < retained; ++index) {
    cursor += weights[index];
    if (draw <= cursor) {
      selected = index;
      break;
    }
  }
  std::vector<Candidate> reported(candidates.begin(),
                                  candidates.begin() + std::min<size_t>(5, candidates.size()));
  return {candidates[selected].token, candidates[selected].raw_logit, retained,
          std::move(reported)};
}

void WriteCandidates(std::ostream& stream, const std::vector<Candidate>& candidates) {
  stream << '[';
  for (size_t index = 0; index < candidates.size(); ++index) {
    if (index != 0) stream << ',';
    stream << "{\"token_id\":" << candidates[index].token
           << ",\"raw_logit\":" << candidates[index].raw_logit
           << ",\"sampling_score\":" << candidates[index].score << '}';
  }
  stream << ']';
}

double MedianOrZero(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  return Summarize(values, 0).median;
}

double PercentileOrZero(const std::vector<double>& values, double quantile) {
  if (values.empty()) return 0.0;
  if (!(quantile >= 0.0 && quantile <= 1.0)) {
    throw std::runtime_error("percentile quantile is outside [0,1]");
  }
  std::vector<double> sorted(values);
  std::sort(sorted.begin(), sorted.end());
  const double position = quantile * static_cast<double>(sorted.size() - 1);
  const size_t lower = static_cast<size_t>(std::floor(position));
  const size_t upper = static_cast<size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(lower);
  return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

#include "resident_prefill.h"
#include "logit_offset.h"

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--sampler-self-test") {
    std::vector<float> logits(kEngineVocab, -1000.0f);
    logits[30] = 5.0f; logits[31] = 4.7f;
    std::mt19937_64 rng(0);
    auto choose = [&](const std::vector<int64_t>& completion, double frequency) {
      return SelectToken(logits.data(), logits.size(), "penalized_greedy", completion,
                         1.0, 1.0, 1, 0.0, 0.0, 1.0, frequency, rng).token;
    };
    if (choose({30,30,30,30},0.0)!=30 || choose({30},0.1)!=30 ||
        choose({30,30,30,30},0.1)!=31 || choose({},2.0)!=30 ||
        choose({31,31,31,31},-0.1)!=31) throw std::runtime_error("frequency sampler self-test failed");
    std::cout << "{\"status\":\"pass\",\"frequency_sampler_cases\":5}\n";
    return 0;
  }

  std::string phase = "argument_validation";
  const int protocol_descriptor = dup(STDOUT_FILENO);
  if (protocol_descriptor < 0) {
    std::cerr << "cannot duplicate protocol stdout: " << std::strerror(errno) << '\n';
    return 2;
  }
  __gnu_cxx::stdio_filebuf<char> protocol_buffer(protocol_descriptor, std::ios::out);
  std::ostream protocol(&protocol_buffer);
  std::cout.flush();
  if (dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
    std::cerr << "cannot redirect native stdout diagnostics: " << std::strerror(errno) << '\n';
    return 2;
  }
  try {
    const EngineArgs args = ParseEngineArgs(argc, argv);
    const char* shared_prefill_model = std::getenv("QWEN35_SHARED_PREFILL_MODEL");
    for (const auto& path : {args.model, args.plugin, args.backend, args.tensor_contract,
                             args.advance_plan, args.embedding_npy, args.rope_binary}) {
      if (!fs::is_regular_file(path)) throw std::runtime_error("required file absent: " + path.string());
    }
    if (std::fegetround() != FE_TONEAREST) {
      throw std::runtime_error("host floating-point rounding mode is not round-to-nearest-even");
    }

    phase = "runtime_asset_map";
    const MappedFile embedding_map(args.embedding_npy);
    const float* embedding = ParseEmbeddingNpy(embedding_map);
    const MappedFile rope_map(args.rope_binary);
    if (rope_map.bytes != kEngineRopeBytes) throw std::runtime_error("RoPE binary byte contract mismatch");
    const float* rope_cos = reinterpret_cast<const float*>(rope_map.data);
    const float* rope_sin = rope_cos + kEngineContext * kEngineRotary;
    for (size_t index = 0; index < kEngineContext * kEngineRotary * 2; ++index) {
      if (!std::isfinite(rope_cos[index])) throw std::runtime_error("non-finite RoPE table");
    }

    phase = "flat_contract_validation";
    const auto tensor_contract = LoadTensorContract(args.tensor_contract);
    const auto advance_plan = LoadAdvancePlan(args.advance_plan);
    if (advance_plan.size() != kExpectedAdvanceRecords) {
      throw std::runtime_error("advance plan record count mismatch");
    }

    phase = "environment_and_plugin_registration";
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "qwen_qnn_text_engine");
    env.RegisterExecutionProviderLibrary(kQnnEpName, args.plugin.string());
    std::vector<Ort::ConstEpDevice> qnn_devices;
    for (const auto& device : env.GetEpDevices()) {
      if (std::string_view(device.EpName()) == kQnnEpName) qnn_devices.push_back(device);
    }
    if (qnn_devices.empty()) throw std::runtime_error("no QNN OrtEpDevice after registration");

    phase = "persistent_session_create";
    Ort::SessionOptions options;
    options.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
    options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
    options.AddConfigEntry("ep.context_enable", "0");
    options.SetIntraOpNumThreads(1);
    options.SetInterOpNumThreads(1);
    options.AddConfigEntry("session.intra_op.allow_spinning", "0");
    if (shared_prefill_model) options.AddConfigEntry("ep.share_ep_contexts", "1");
    const std::unordered_map<std::string, std::string> provider_options = {
        {"backend_path", args.backend.string()},
        {"offload_graph_io_quantization", "0"},
        {"htp_performance_mode", "burst"},
        {"profiling_level", "off"},
        {"enable_htp_shared_memory_allocator", "1"},
    };
    options.AppendExecutionProvider_V2(env, qnn_devices, provider_options);
    const auto session_begin = Clock::now();
    Ort::Session session(env, args.model.c_str(), options);
    const double session_create_seconds = Seconds(session_begin, Clock::now());

    phase = "context_contract_validation";
    if (session.GetInputCount() != kExpectedInputs || session.GetOutputCount() != kExpectedOutputs ||
        tensor_contract.size() != kExpectedInputs + kExpectedOutputs) {
      throw std::runtime_error("context tensor count mismatch");
    }
    Ort::AllocatorWithDefaultOptions default_allocator;
    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
    std::vector<std::vector<int64_t>> input_shapes;
    std::vector<std::vector<int64_t>> output_shapes;
    std::vector<ONNXTensorElementDataType> input_types;
    std::vector<ONNXTensorElementDataType> output_types;
    size_t input_bytes = 0;
    size_t output_bytes = 0;
    for (size_t index = 0; index < kExpectedInputs; ++index) {
      auto name = session.GetInputNameAllocated(index, default_allocator);
      input_names.emplace_back(name.get());
      input_shapes.push_back(GetShape(session, true, index));
      input_types.push_back(GetElementType(session, true, index));
      const auto& expected = tensor_contract[index];
      if (expected.kind != "input" || expected.position != index || expected.name != input_names.back() ||
          expected.shape != input_shapes.back() || expected.elements != ShapeElements(input_shapes.back()) ||
          ElementType(expected.dtype) != input_types.back()) {
        throw std::runtime_error("context input differs from tensor contract at index " +
                                 std::to_string(index));
      }
      input_bytes += expected.bytes;
    }
    for (size_t index = 0; index < kExpectedOutputs; ++index) {
      auto name = session.GetOutputNameAllocated(index, default_allocator);
      output_names.emplace_back(name.get());
      output_shapes.push_back(GetShape(session, false, index));
      output_types.push_back(GetElementType(session, false, index));
      const auto& expected = tensor_contract[kExpectedInputs + index];
      if (expected.kind != "output" || expected.position != index || expected.name != output_names.back() ||
          expected.shape != output_shapes.back() || expected.elements != ShapeElements(output_shapes.back()) ||
          ElementType(expected.dtype) != output_types.back()) {
        throw std::runtime_error("context output differs from tensor contract at index " +
                                 std::to_string(index));
      }
      output_bytes += expected.bytes;
    }
    if (input_bytes != kExpectedInputBytes || output_bytes != kExpectedOutputBytes) {
      throw std::runtime_error("context byte volume mismatch");
    }

    phase = "qnn_htp_shared_allocation";
    Ort::MemoryInfo shared_info("QnnHtpShared", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeCPU);
    Ort::Allocator shared_allocator(session, shared_info);
    const auto allocator_info = shared_allocator.GetInfo();
    if (allocator_info.GetAllocatorName() != "QnnHtpShared") {
      throw std::runtime_error("session allocator is not QnnHtpShared");
    }
    std::vector<Ort::MemoryAllocation> input_allocations;
    std::vector<Ort::MemoryAllocation> output_allocations;
    std::vector<Ort::MemoryAllocation> output_arena_allocations;
    std::vector<Ort::Value> input_values;
    std::vector<Ort::Value> output_values;
    std::vector<void*> input_ptrs;
    std::vector<void*> output_ptrs;
    input_allocations.reserve(kExpectedInputs);
    output_allocations.reserve(kExpectedOutputs);
    output_arena_allocations.reserve(1);
    input_values.reserve(kExpectedInputs);
    output_values.reserve(kExpectedOutputs);
    input_ptrs.reserve(kExpectedInputs);
    output_ptrs.reserve(kExpectedOutputs);
    std::unordered_map<std::string, size_t> input_index;
    std::unordered_map<std::string, size_t> output_index;
    for (size_t index = 0; index < kExpectedInputs; ++index) {
      const auto& expected = tensor_contract[index];
      auto allocation = shared_allocator.GetAllocation(expected.bytes);
      void* pointer = allocation.get();
      if (pointer == nullptr) throw std::runtime_error("null shared input allocation");
      std::memset(pointer, 0, expected.bytes);
      input_values.push_back(Ort::Value::CreateTensor(
          shared_info, pointer, expected.bytes, input_shapes[index].data(), input_shapes[index].size(),
          input_types[index]));
      input_ptrs.push_back(pointer);
      input_allocations.push_back(std::move(allocation));
      input_index.emplace(input_names[index], index);
    }
    for (size_t index = 0; index < kExpectedOutputs; ++index) output_index.emplace(output_names[index], index);

    std::unordered_map<std::string, std::string> uint16_output_to_input;
    size_t aliased_state_pairs = 0;
    size_t aliased_kv_pairs = 0;
    for (const auto& record : advance_plan) {
      if (record.category != "deltanet_recurrent_state" && record.category != "attention_kv") continue;
      if (record.source_dtype != "uint16" || record.destination_dtype != "uint16" ||
          !output_index.contains(record.source) || !input_index.contains(record.destination)) {
        throw std::runtime_error("UINT16 alias endpoint/type mismatch");
      }
      const size_t source_position = output_index.at(record.source);
      const size_t destination_position = input_index.at(record.destination);
      const auto& source_contract = tensor_contract[kExpectedInputs + source_position];
      const auto& destination_contract = tensor_contract[destination_position];
      if (source_contract.bytes != destination_contract.bytes ||
          source_contract.elements != destination_contract.elements ||
          source_contract.dtype != destination_contract.dtype || record.source_offset != 0 ||
          record.destination_offset != 0 ||
          record.rows * record.row_elements != source_contract.elements ||
          record.source_stride != record.row_elements || record.destination_stride != record.row_elements) {
        throw std::runtime_error("UINT16 alias tensor is not whole and contiguous");
      }
      if (!uint16_output_to_input.emplace(record.source, record.destination).second) {
        throw std::runtime_error("duplicate UINT16 alias source");
      }
      aliased_state_pairs += record.category == "deltanet_recurrent_state" ? 1 : 0;
      aliased_kv_pairs += record.category == "attention_kv" ? 1 : 0;
    }
    if (uint16_output_to_input.size() != 556 || aliased_state_pairs != 544 || aliased_kv_pairs != 12) {
      throw std::runtime_error("UINT16 state/KV alias count mismatch");
    }

    constexpr size_t kOutputArenaAlignment = 64;
    constexpr size_t kNoArenaOffset = std::numeric_limits<size_t>::max();
    std::vector<size_t> output_arena_offsets(kExpectedOutputs, kNoArenaOffset);
    size_t output_arena_bytes = 0;
    size_t output_arena_slices = 0;
    void* output_arena_base = nullptr;
    if (args.state_buffer_mode == "arena_disjoint") {
      for (size_t index = 0; index < kExpectedOutputs; ++index) {
        if (!uint16_output_to_input.contains(output_names[index])) continue;
        const auto& expected = tensor_contract[kExpectedInputs + index];
        if (output_arena_bytes >
            std::numeric_limits<size_t>::max() - (kOutputArenaAlignment - 1)) {
          throw std::runtime_error("shared output arena alignment overflow");
        }
        const size_t aligned =
            (output_arena_bytes + (kOutputArenaAlignment - 1)) & ~(kOutputArenaAlignment - 1);
        if (expected.bytes > std::numeric_limits<size_t>::max() - aligned) {
          throw std::runtime_error("shared output arena size overflow");
        }
        output_arena_offsets[index] = aligned;
        output_arena_bytes = aligned + expected.bytes;
        ++output_arena_slices;
      }
      if (output_arena_slices != 556 || output_arena_bytes == 0) {
        throw std::runtime_error("shared output arena slice count mismatch");
      }
      auto arena = shared_allocator.GetAllocation(output_arena_bytes);
      output_arena_base = arena.get();
      if (output_arena_base == nullptr ||
          reinterpret_cast<uintptr_t>(output_arena_base) % kOutputArenaAlignment != 0) {
        throw std::runtime_error("shared output arena base is null or misaligned");
      }
      std::memset(output_arena_base, 0, output_arena_bytes);
      output_arena_allocations.push_back(std::move(arena));
    }
    for (size_t index = 0; index < kExpectedOutputs; ++index) {
      const auto& expected = tensor_contract[kExpectedInputs + index];
      void* pointer = nullptr;
      const auto alias = uint16_output_to_input.find(output_names[index]);
      if (args.state_buffer_mode == "aliased" && alias != uint16_output_to_input.end()) {
        pointer = input_ptrs[input_index.at(alias->second)];
      } else if (args.state_buffer_mode == "arena_disjoint" &&
                 alias != uint16_output_to_input.end()) {
        const size_t offset = output_arena_offsets[index];
        if (offset == kNoArenaOffset || offset > output_arena_bytes ||
            expected.bytes > output_arena_bytes - offset) {
          throw std::runtime_error("shared output arena slice is outside allocation");
        }
        pointer = static_cast<std::byte*>(output_arena_base) + offset;
        if (reinterpret_cast<uintptr_t>(pointer) % kOutputArenaAlignment != 0) {
          throw std::runtime_error("shared output arena slice is misaligned");
        }
      } else {
        auto allocation = shared_allocator.GetAllocation(expected.bytes);
        pointer = allocation.get();
        if (pointer == nullptr) throw std::runtime_error("null shared output allocation");
        std::memset(pointer, 0, expected.bytes);
        output_allocations.push_back(std::move(allocation));
      }
      output_values.push_back(Ort::Value::CreateTensor(
          shared_info, pointer, expected.bytes, output_shapes[index].data(), output_shapes[index].size(),
          output_types[index]));
      output_ptrs.push_back(pointer);
    }
    const size_t expected_shared_allocations = args.state_buffer_mode == "aliased"
                                                   ? 597
                                                   : (args.state_buffer_mode == "arena_disjoint" ? 598 : 1153);
    if (input_allocations.size() + output_allocations.size() +
            output_arena_allocations.size() !=
        expected_shared_allocations) {
      throw std::runtime_error("shared allocation count mismatch");
    }
    size_t input_output_overlap_pairs = 0;
    size_t exact_input_output_alias_pairs = 0;
    for (size_t input_position = 0; input_position < kExpectedInputs; ++input_position) {
      const uintptr_t input_begin = reinterpret_cast<uintptr_t>(input_ptrs[input_position]);
      const uintptr_t input_end = input_begin + tensor_contract[input_position].bytes;
      if (input_end < input_begin) throw std::runtime_error("input address range overflow");
      for (size_t output_position = 0; output_position < kExpectedOutputs; ++output_position) {
        const uintptr_t output_begin = reinterpret_cast<uintptr_t>(output_ptrs[output_position]);
        const uintptr_t output_end =
            output_begin + tensor_contract[kExpectedInputs + output_position].bytes;
        if (output_end < output_begin) throw std::runtime_error("output address range overflow");
        if (input_begin < output_end && output_begin < input_end) {
          ++input_output_overlap_pairs;
          if (input_begin == output_begin && input_end == output_end) {
            ++exact_input_output_alias_pairs;
          }
        }
      }
    }
    const size_t expected_overlap_pairs = args.state_buffer_mode == "aliased" ? 556 : 0;
    if (input_output_overlap_pairs != expected_overlap_pairs ||
        exact_input_output_alias_pairs != expected_overlap_pairs) {
      throw std::runtime_error("input/output address overlap contract mismatch");
    }
    size_t output_output_overlap_pairs = 0;
    for (size_t left = 0; left < kExpectedOutputs; ++left) {
      const uintptr_t left_begin = reinterpret_cast<uintptr_t>(output_ptrs[left]);
      const uintptr_t left_end = left_begin + tensor_contract[kExpectedInputs + left].bytes;
      if (left_end < left_begin) throw std::runtime_error("output address range overflow");
      for (size_t right = left + 1; right < kExpectedOutputs; ++right) {
        const uintptr_t right_begin = reinterpret_cast<uintptr_t>(output_ptrs[right]);
        const uintptr_t right_end = right_begin + tensor_contract[kExpectedInputs + right].bytes;
        if (right_end < right_begin) throw std::runtime_error("output address range overflow");
        output_output_overlap_pairs +=
            left_begin < right_end && right_begin < left_end ? 1 : 0;
      }
    }
    if (output_output_overlap_pairs != 0) {
      throw std::runtime_error("output/output address overlap contract mismatch");
    }
    Ort::IoBinding binding(session);
    for (size_t index = 0; index < kExpectedInputs; ++index) {
      binding.BindInput(input_names[index].c_str(), input_values[index]);
    }
    for (size_t index = 0; index < kExpectedOutputs; ++index) {
      binding.BindOutput(output_names[index].c_str(), output_values[index]);
    }

    phase = "advance_plan_validation_and_lut_build";
    size_t planned_advance_bytes = 0;
    size_t state_copy_records = 0;
    size_t state_requantize_records = 0;
    size_t kv_copy_records = 0;
    std::unordered_map<std::string, bool> initialized_uint16_inputs;
    for (const auto& record : advance_plan) {
      if (!output_index.contains(record.source) || !input_index.contains(record.destination)) {
        throw std::runtime_error("advance plan endpoint mismatch");
      }
      const auto& source_contract = tensor_contract[kExpectedInputs + output_index.at(record.source)];
      const auto& destination_contract = tensor_contract[input_index.at(record.destination)];
      if (source_contract.dtype != record.source_dtype ||
          destination_contract.dtype != record.destination_dtype) {
        throw std::runtime_error("advance plan dtype mismatch");
      }
      planned_advance_bytes += record.rows * record.row_elements * ElementSize(record.destination_dtype);
      if (record.operation == "copy_float32") {
        if (record.source_dtype != "float32" || record.destination_dtype != "float32") {
          throw std::runtime_error("invalid float advance operation");
        }
      } else if (record.operation == "copy_uint16" || record.operation == "requantize_uint16") {
        if (record.source_dtype != "uint16" || record.destination_dtype != "uint16" ||
            record.input_scale <= 0.0f || record.output_scale <= 0.0f ||
            initialized_uint16_inputs.contains(record.destination)) {
          throw std::runtime_error("invalid or duplicate UINT16 advance operation");
        }
        initialized_uint16_inputs.emplace(record.destination, true);
        if (record.category == "deltanet_recurrent_state") {
          state_copy_records += record.operation == "copy_uint16" ? 1 : 0;
          state_requantize_records += record.operation == "requantize_uint16" ? 1 : 0;
        } else if (record.category == "attention_kv") {
          kv_copy_records += record.operation == "copy_uint16" ? 1 : 0;
        } else {
          throw std::runtime_error("unexpected UINT16 advance category");
        }
      } else {
        throw std::runtime_error("unknown advance operation");
      }
    }
    if (planned_advance_bytes != kExpectedAdvanceBytes || state_copy_records != 209 ||
        state_requantize_records != 335 || kv_copy_records != 12 ||
        initialized_uint16_inputs.size() != 556) {
      throw std::runtime_error("advance plan fixed-count contract mismatch");
    }

    std::vector<bool> exact_identity_requantize;
    exact_identity_requantize.reserve(advance_plan.size());
    size_t identity_records = 0;
    size_t identity_elements = 0;
    for (const auto& record : advance_plan) {
      const bool identity = IsExactIdentityRequantize(record);
      exact_identity_requantize.push_back(identity);
      if (identity) {
        ++identity_records;
        identity_elements += record.rows * record.row_elements;
      }
    }
    if (identity_records != 290 || identity_elements != 2'378'752) {
      throw std::runtime_error("identity requantization proof contract mismatch");
    }
    std::vector<std::vector<uint16_t>> requantize_luts(advance_plan.size());
    size_t lut_records = 0;
    for (size_t index = 0; index < advance_plan.size(); ++index) {
      const auto& record = advance_plan[index];
      if (record.operation != "requantize_uint16" || exact_identity_requantize[index]) continue;
      auto& lut = requantize_luts[index];
      lut.resize(65'536);
      for (uint32_t code = 0; code <= std::numeric_limits<uint16_t>::max(); ++code) {
        lut[code] = RequantizeUint16Code(static_cast<uint16_t>(code), record);
      }
      ++lut_records;
    }
    if (lut_records != 45) throw std::runtime_error("nonidentity LUT count mismatch");

    const size_t hidden_index = input_index.at("hidden_in");
    const size_t rope_cos_index = input_index.at("rope_cos");
    const size_t rope_sin_index = input_index.at("rope_sin");
    const size_t mask_index = input_index.at("tail_mask");
    const size_t logits_index = output_index.at("logits_fp32");
    const size_t logits_count = ShapeElements(output_shapes[logits_index]);
    if (tensor_contract[hidden_index].elements != kEngineHidden ||
        tensor_contract[rope_cos_index].elements != kEngineRotary ||
        tensor_contract[rope_sin_index].elements != kEngineRotary ||
        tensor_contract[mask_index].elements != kEngineContext || logits_count != kEngineVocab) {
      throw std::runtime_error("dynamic text boundary shape mismatch");
    }

    std::unique_ptr<ResidentPrefill> shared_prefill;
    if (shared_prefill_model) {
      phase = "shared_prefill_session_create";
      shared_prefill = std::make_unique<ResidentPrefill>(env, qnn_devices, shared_prefill_model, args.backend);
    }
    std::unique_ptr<LogitOffset> logit_correction;
    if (const char* model = std::getenv("QWEN35_LOGIT_CORRECTION_MODEL")) {
      phase = "logit_correction_session_create";
      logit_correction = std::make_unique<LogitOffset>(env, qnn_devices, model, args.backend, output_values.at(logits_index));
    }

    auto reset_state = [&]() {
      if (logit_correction) logit_correction->ResetStats();
      for (size_t index = 0; index < kExpectedInputs; ++index) {
        std::memset(input_ptrs[index], 0, tensor_contract[index].bytes);
      }
      for (size_t index = 0; index < kExpectedOutputs; ++index) {
        if (args.state_buffer_mode != "aliased" ||
            !uint16_output_to_input.contains(output_names[index])) {
          std::memset(output_ptrs[index], 0, tensor_contract[kExpectedInputs + index].bytes);
        }
      }
      for (const auto& record : advance_plan) {
        if (record.operation != "copy_uint16" && record.operation != "requantize_uint16") continue;
        const auto& destination_contract = tensor_contract[input_index.at(record.destination)];
        auto* destination = static_cast<uint16_t*>(input_ptrs[input_index.at(record.destination)]);
        std::fill_n(destination, destination_contract.elements,
                    static_cast<uint16_t>(record.input_zero_point));
      }
    };

#ifdef SEED_COMMAND
    auto load_seed = [&](const fs::path& seed_path, const std::string& id, size_t position,
                         const std::vector<int64_t>& history) -> size_t {
      if (seed_path.string().rfind("shared-resident:", 0) == 0) {
        if (!shared_prefill) throw std::runtime_error("resident Prefill is disabled");
        return shared_prefill->Seed(seed_path.string(), id, position, history, tensor_contract, advance_plan, input_ptrs);
      }
      const MappedFile seed(seed_path);
      if (seed.bytes != kExpectedInputBytes) {
        throw std::runtime_error("seed binary byte count differs from the 578-input contract");
      }
      size_t offset = 0;
      for (size_t index = 0; index < kExpectedInputs; ++index) {
        const size_t bytes = tensor_contract[index].bytes;
        if (offset + bytes > seed.bytes) {
          throw std::runtime_error("seed binary tensor range overflow");
        }
        std::memcpy(input_ptrs[index], seed.data + offset, bytes);
        offset += bytes;
      }
      if (offset != seed.bytes) throw std::runtime_error("seed binary trailing bytes");
      for (size_t index = 0; index < kExpectedOutputs; ++index) {
        if (args.state_buffer_mode != "aliased" ||
            !uint16_output_to_input.contains(output_names[index])) {
          std::memset(output_ptrs[index], 0, tensor_contract[kExpectedInputs + index].bytes);
        }
      }
      return seed.bytes;
    };
#endif

    auto advance_state = [&]() {
      for (size_t advance_index = 0; advance_index < advance_plan.size(); ++advance_index) {
        const auto& record = advance_plan[advance_index];
        if (record.operation == "copy_float32") {
          const auto* source = static_cast<const float*>(output_ptrs[output_index.at(record.source)]) +
                               record.source_offset;
          auto* destination = static_cast<float*>(input_ptrs[input_index.at(record.destination)]) +
                              record.destination_offset;
          for (size_t row = 0; row < record.rows; ++row) {
            std::memcpy(destination + row * record.destination_stride,
                        source + row * record.source_stride,
                        record.row_elements * sizeof(float));
          }
        } else if (record.operation == "copy_uint16") {
          const auto* source = static_cast<const uint16_t*>(output_ptrs[output_index.at(record.source)]) +
                               record.source_offset;
          auto* destination = static_cast<uint16_t*>(input_ptrs[input_index.at(record.destination)]) +
                              record.destination_offset;
          if (source != destination) {
            for (size_t row = 0; row < record.rows; ++row) {
              std::memcpy(destination + row * record.destination_stride,
                          source + row * record.source_stride,
                          record.row_elements * sizeof(uint16_t));
            }
          }
        } else if (record.operation == "requantize_uint16") {
          const auto* source = static_cast<const uint16_t*>(output_ptrs[output_index.at(record.source)]) +
                               record.source_offset;
          auto* destination = static_cast<uint16_t*>(input_ptrs[input_index.at(record.destination)]) +
                              record.destination_offset;
          if (exact_identity_requantize[advance_index]) {
            if (source != destination) {
              for (size_t row = 0; row < record.rows; ++row) {
                std::memcpy(destination + row * record.destination_stride,
                            source + row * record.source_stride,
                            record.row_elements * sizeof(uint16_t));
              }
            }
          } else {
            const auto& lut = requantize_luts[advance_index];
            for (size_t row = 0; row < record.rows; ++row) {
              const auto* source_row = source + row * record.source_stride;
              auto* destination_row = destination + row * record.destination_stride;
              for (size_t element = 0; element < record.row_elements; ++element) {
                destination_row[element] = lut[source_row[element]];
              }
            }
          }
        }
      }
    };

    Ort::RunOptions run_options;
    auto run_embedding_with_rope = [&](const float* hidden, size_t cache_position,
                                       const float* cos, const float* sin) -> StepTiming {
      if (cache_position >= kEngineContext) throw std::runtime_error("embedding position outside context");
      const auto e2e_begin = Clock::now();
      const auto prepare_begin = Clock::now();
      std::memcpy(input_ptrs[hidden_index], hidden, kEngineHidden * sizeof(float));
      std::memcpy(input_ptrs[rope_cos_index], cos, kEngineRotary * sizeof(float));
      std::memcpy(input_ptrs[rope_sin_index], sin, kEngineRotary * sizeof(float));
      auto* mask = static_cast<float*>(input_ptrs[mask_index]);
      std::fill_n(mask, kEngineContext, -64.0f);
      std::fill(mask + (kEngineContext - cache_position - 1), mask + kEngineContext, 0.0f);
      const double prepare_seconds = Seconds(prepare_begin, Clock::now());
      binding.SynchronizeInputs();
      const auto run_begin = Clock::now();
      session.Run(run_options, binding);
      const double run_seconds = Seconds(run_begin, Clock::now());
      binding.SynchronizeOutputs();
      const auto advance_begin = Clock::now();
      advance_state();
      const double advance_seconds = Seconds(advance_begin, Clock::now());
      return {prepare_seconds, run_seconds, advance_seconds, Seconds(e2e_begin, Clock::now())};
    };
    auto run_token_with_rope = [&](int64_t token, size_t cache_position,
                                   int64_t rope_position) -> StepTiming {
      if (token < 0 || token >= static_cast<int64_t>(kEngineVocab) ||
          cache_position >= kEngineContext || rope_position < 0 ||
          rope_position >= static_cast<int64_t>(kEngineContext)) {
        throw std::runtime_error("run token or position outside fixed context");
      }
      return run_embedding_with_rope(embedding + static_cast<size_t>(token) * kEngineHidden,
          cache_position, rope_cos + static_cast<size_t>(rope_position) * kEngineRotary,
          rope_sin + static_cast<size_t>(rope_position) * kEngineRotary);
    };
    auto run_token = [&](int64_t token, size_t position) -> StepTiming {
      return run_token_with_rope(token, position, static_cast<int64_t>(position));
    };

#ifdef SEED_COMMAND
    auto run_loaded_seed = [&]() -> StepTiming {
      const auto e2e_begin = Clock::now();
      binding.SynchronizeInputs();
      const auto run_begin = Clock::now();
      session.Run(run_options, binding);
      const double run_seconds = Seconds(run_begin, Clock::now());
      binding.SynchronizeOutputs();
      const auto advance_begin = Clock::now();
      advance_state();
      const double advance_seconds = Seconds(advance_begin, Clock::now());
      return {0.0, run_seconds, advance_seconds, Seconds(e2e_begin, Clock::now())};
    };
#endif

    reset_state();
    const std::string boot_id = ReadTrimmed("/proc/sys/kernel/random/boot_id");
    protocol << std::setprecision(17)
              << "{\"type\":\"ready\",\"schema\":\"qwen_qnn_text_engine_tsv_v1\""
              << ",\"shared_prefill\":" << (shared_prefill ? "true" : "false")
              << ",\"prefill_session_create_seconds\":" << (shared_prefill ? shared_prefill->create_seconds : 0.0)
              << ",\"prefill_input_arena_bytes\":" << (shared_prefill ? shared_prefill->input_bytes : 0)
              << ",\"prefill_output_arena_bytes\":" << (shared_prefill ? shared_prefill->output_bytes : 0)
              << ",\"logit_correction_enabled\":" << (logit_correction ? "true" : "false")
              << ",\"boot_id\":\"" << JsonEscape(boot_id) << "\""
              << ",\"session_create_seconds\":" << session_create_seconds
              << ",\"frequency_penalty_supported\":true,\"frequency_penalty_scope\":\"completion_only\""
              << ",\"persistent_session\":true,\"fallback_disabled\":true"
              << ",\"allocator\":\"QnnHtpShared\",\"qnn_ep_devices\":" << qnn_devices.size()
              << ",\"input_tensors\":" << kExpectedInputs << ",\"output_tensors\":"
              << kExpectedOutputs << ",\"shared_allocations\":" << expected_shared_allocations
              << ",\"aliased_uint16_pairs\":"
              << (args.state_buffer_mode == "aliased" ? 556 : 0)
              << ",\"state_buffer_mode\":\"" << args.state_buffer_mode << "\""
              << ",\"output_arena_bytes\":" << output_arena_bytes
              << ",\"output_arena_slices\":" << output_arena_slices
              << ",\"output_arena_alignment\":" << kOutputArenaAlignment
              << ",\"input_output_overlap_pairs\":" << input_output_overlap_pairs
              << ",\"exact_input_output_alias_pairs\":" << exact_input_output_alias_pairs
              << ",\"output_output_overlap_pairs\":" << output_output_overlap_pairs
#ifdef SEED_COMMAND
              << ",\"seeded_generation_command\":true"
              << ",\"seeded_remainder_command\":true"
              << ",\"seeded_mrope_command\":true"
#endif
              << ",\"identity_requantization_records\":" << identity_records
              << ",\"nonidentity_lut_records\":" << lut_records
              << ",\"context\":" << kEngineContext
              << ",\"vocab\":" << kEngineVocab << "}\n" << std::flush;

    phase = "command_loop";
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      double frequency_penalty = 0.0;
      if (line.rfind("FREQUENCY_V1\t", 0) == 0) {
        std::string frequency_request_id;
        try {
          const size_t end = line.find('\t', 13);
          if (end == std::string::npos) throw std::runtime_error("incomplete frequency envelope");
          const std::string value = line.substr(13, end - 13);
          line.erase(0, end + 1);
          const auto command_fields = Split(line, '\t');
          if (command_fields.size() > 1) frequency_request_id = command_fields[1];
          size_t consumed = 0;
          frequency_penalty = std::stod(value, &consumed);
          if (consumed != value.size() || !std::isfinite(frequency_penalty) ||
              frequency_penalty < -2.0 || frequency_penalty > 2.0 ||
              line.rfind("GENERATE", 0) != 0) {
            throw std::runtime_error("frequency envelope parameter contract mismatch");
          }
        } catch (const std::exception& error) {
          reset_state();
          if (shared_prefill) shared_prefill->Invalidate();
          protocol << "{\"type\":\"error\",\"request_id\":\"" << JsonEscape(frequency_request_id)
                   << "\",\"error\":\"" << JsonEscape(error.what())
                   << "\",\"state_reset_after_error\":true}\n" << std::flush;
          continue;
        }
      }
      if (line.rfind("PREFILL_V1\t", 0) == 0) {
        std::string id;
        try {
          const auto fields = Split(line, '\t');
          if (fields.size() > 1) id = fields[1];
          if (fields.size() != 5 || !shared_prefill)
            throw std::runtime_error("native Prefill command/configuration mismatch");
          size_t parsed = 0;
          const size_t count = std::stoull(fields[2], &parsed);
          if (parsed != fields[2].size()) throw std::runtime_error("native Prefill prefix is invalid");
          reset_state();
          shared_prefill->Run(id, count, fields[3], fields[4]);
          protocol << "{\"type\":\"prefill_done\",\"request_id\":\"" << JsonEscape(id)
                   << "\",\"resident_seed_tag\":\"shared-resident:" << JsonEscape(id)
                   << "\",\"prefix_tokens\":" << count
                   << ",\"prompt_tokens\":" << shared_prefill->prompt->sequence
                   << ",\"rope_delta\":" << shared_prefill->prompt->rope_delta
                   << ",\"compute_seconds\":" << shared_prefill->compute_seconds
                   << ",\"preparation_seconds\":" << shared_prefill->preparation_seconds
                   << ",\"prefix_cache_enabled\":" << (shared_prefill->prefix_cache_enabled ? "true" : "false")
                   << ",\"cached_prefix_tokens\":" << shared_prefill->reused_prefix
                   << ",\"executed_tokens\":" << (count - shared_prefill->reused_prefix)
                   << ",\"cache_match_seconds\":" << shared_prefill->cache_match_seconds
                   << ",\"retained_prompt_bytes\":" << (shared_prefill->prefix_cache_enabled ? shared_prefill->prompt->file.bytes : 0)
                   << ",\"all_finite\":true,\"fallback_disabled\":true,\"session_reused\":true}\n" << std::flush;
        } catch (const std::exception& error) {
          reset_state();
          if (shared_prefill) shared_prefill->Invalidate();
          protocol << "{\"type\":\"error\",\"request_id\":\"" << JsonEscape(id)
                   << "\",\"error\":\"" << JsonEscape(error.what())
                   << "\",\"state_reset_after_error\":true}\n" << std::flush;
        }
        continue;
      }
      if (line == "PING") {
        protocol << "{\"type\":\"pong\"}\n" << std::flush;
        continue;
      }
      if (line == "RESET") {
        if (shared_prefill) shared_prefill->Invalidate();
        reset_state();
        protocol << "{\"type\":\"reset\",\"status\":\"ok\"}\n" << std::flush;
        continue;
      }
      if (line == "SHUTDOWN") {
        if (shared_prefill) shared_prefill->Invalidate();
        reset_state();
        protocol << "{\"type\":\"shutdown\",\"status\":\"ok\"}\n" << std::flush;
        return 0;
      }

#ifdef SEED_COMMAND
      if (line.rfind("GENERATE_SEEDED\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_V2\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_REMAINDER\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_MROPE\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_REMAINDER_MROPE\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_REMAINDER_V2\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_MROPE_V2\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_REMAINDER_MROPE_V2\t", 0) == 0 ||
          line.rfind("GENERATE_SEEDED_EMBEDDINGS_V3\t", 0) == 0) {
        std::string seeded_request_id;
        try {
          auto fields = Split(line, '\t');
          const bool embedding_remainder = !fields.empty() && fields[0] == "GENERATE_SEEDED_EMBEDDINGS_V3";
          std::unique_ptr<MappedFile> suffix_hidden_map, suffix_cos_map, suffix_sin_map;
          const float *suffix_hidden=nullptr, *suffix_cos=nullptr, *suffix_sin=nullptr;
          if (embedding_remainder) {
            if (fields.size() != 20) throw std::runtime_error("expected20 fields for visual suffix V3");
            suffix_hidden_map = std::make_unique<MappedFile>(fields[17]);
            suffix_cos_map = std::make_unique<MappedFile>(fields[18]);
            suffix_sin_map = std::make_unique<MappedFile>(fields[19]);
            fields.resize(17);
            fields[0] = "GENERATE_SEEDED_REMAINDER_MROPE_V2";
          }
          const bool standard_command =
              !fields.empty() && fields[0] == "GENERATE_SEEDED";
          const bool version2 = !fields.empty() && fields[0] == "GENERATE_SEEDED_V2";
          const bool remainder_command =
              !fields.empty() && fields[0] == "GENERATE_SEEDED_REMAINDER";
          const bool mrope_command =
              !fields.empty() && fields[0] == "GENERATE_SEEDED_MROPE";
          const bool remainder_mrope_command =
              !fields.empty() && fields[0] == "GENERATE_SEEDED_REMAINDER_MROPE";
          const bool remainder_v2_command =
              !fields.empty() && fields[0] == "GENERATE_SEEDED_REMAINDER_V2";
          const bool mrope_v2_command =
              !fields.empty() && fields[0] == "GENERATE_SEEDED_MROPE_V2";
          const bool remainder_mrope_v2_command =
              !fields.empty() && fields[0] == "GENERATE_SEEDED_REMAINDER_MROPE_V2";
          const bool any_version2 =
              version2 || remainder_v2_command || mrope_v2_command ||
              remainder_mrope_v2_command;
          const bool any_remainder_command =
              remainder_command || remainder_mrope_command || remainder_v2_command ||
              remainder_mrope_v2_command;
          const bool any_mrope_command =
              mrope_command || remainder_mrope_command || mrope_v2_command ||
              remainder_mrope_v2_command;
          if ((!standard_command && !version2 && !remainder_command && !mrope_command &&
               !remainder_mrope_command && !remainder_v2_command && !mrope_v2_command &&
               !remainder_mrope_v2_command) ||
              (standard_command && fields.size() != 7) ||
              (version2 && fields.size() != 15) ||
              (remainder_command && fields.size() != 8) ||
              (mrope_command && fields.size() != 8) ||
              (remainder_mrope_command && fields.size() != 9) ||
              (remainder_v2_command && fields.size() != 16) ||
              (mrope_v2_command && fields.size() != 16) ||
              (remainder_mrope_v2_command && fields.size() != 17) ||
              fields[1].empty()) {
            throw std::runtime_error(
                "expected a supported GENERATE_SEEDED TSV command");
          }
          seeded_request_id = fields[1];
          const size_t max_new = std::stoull(fields[2]);
          const auto stop_ids = ParseIds(fields[3], true);
          const fs::path seed_path = fields[4];
          const size_t seed_position = std::stoull(fields[5]);
          auto history = ParseIds(fields[6], false);
          const auto remainder =
              any_remainder_command ? ParseIds(fields[7], false) : std::vector<int64_t>{};
          const int64_t rope_delta =
              (remainder_mrope_command || remainder_mrope_v2_command)
                  ? std::stoll(fields[8])
                  : ((mrope_command || mrope_v2_command) ? std::stoll(fields[7]) : 0);
          std::string mode = "greedy";
          double temperature = 1.0;
          double top_p = 1.0;
          size_t top_k = 5;
          double min_p = 0.0;
          double presence_penalty = 0.0;
          double repetition_penalty = 1.0;
          uint64_t random_seed = 0;
          if (any_version2) {
            const size_t parameter_index =
                7 + (any_remainder_command ? 1 : 0) + (any_mrope_command ? 1 : 0);
            mode = fields[parameter_index];
            temperature = std::stod(fields[parameter_index + 1]);
            top_p = std::stod(fields[parameter_index + 2]);
            top_k = std::stoull(fields[parameter_index + 3]);
            min_p = std::stod(fields[parameter_index + 4]);
            presence_penalty = std::stod(fields[parameter_index + 5]);
            repetition_penalty = std::stod(fields[parameter_index + 6]);
            random_seed = std::stoull(fields[parameter_index + 7]);
          }
          const bool standard_history =
              !any_remainder_command && history.size() == seed_position + 1;
          const bool remainder_history =
              any_remainder_command && !remainder.empty() &&
              remainder.size() <= kEngineSeededRemainderMax &&
              history.size() == seed_position + remainder.size() &&
              std::equal(remainder.begin(), remainder.end(), history.end() - remainder.size());
          const int64_t generation_rope_start =
              static_cast<int64_t>(any_remainder_command ? history.size() : seed_position) +
              rope_delta;
          const int64_t final_rope_position =
              static_cast<int64_t>(history.size()) + static_cast<int64_t>(max_new) - 2 +
              rope_delta;
          if (max_new == 0 || max_new > kEngineContext || seed_position >= kEngineContext ||
              (!standard_history && !remainder_history) ||
              history.size() + max_new > kEngineContext ||
              (any_mrope_command &&
               ((!embedding_remainder && static_cast<int64_t>(seed_position) + rope_delta < 0) ||
                generation_rope_start < 0 ||
                final_rope_position >= static_cast<int64_t>(kEngineContext))) ||
              (mode != "greedy" && mode != "sample" && mode != "penalized_greedy" &&
               mode != "no_repeat4_greedy") ||
              !(temperature > 0.0) || !std::isfinite(temperature) ||
              !(top_p > 0.0 && top_p <= 1.0) || top_k == 0 || top_k > kEngineVocab ||
              !(min_p >= 0.0 && min_p <= 1.0) || !std::isfinite(min_p) ||
              !std::isfinite(presence_penalty) || !(repetition_penalty > 0.0) ||
              !std::isfinite(repetition_penalty)) {
            throw std::runtime_error("seeded generation parameter contract mismatch");
          }

          if (embedding_remainder) {
            if (suffix_hidden_map->bytes != remainder.size()*kEngineHidden*sizeof(float) ||
                suffix_cos_map->bytes != remainder.size()*kEngineRotary*sizeof(float) ||
                suffix_sin_map->bytes != suffix_cos_map->bytes) {
              throw std::runtime_error("visual suffix binary size mismatch");
            }
            suffix_hidden=reinterpret_cast<const float*>(suffix_hidden_map->data);
            suffix_cos=reinterpret_cast<const float*>(suffix_cos_map->data);
            suffix_sin=reinterpret_cast<const float*>(suffix_sin_map->data);
            for (const auto* mapped : {suffix_hidden_map.get(),suffix_cos_map.get(),suffix_sin_map.get()}) {
              const auto* values=reinterpret_cast<const float*>(mapped->data);
              for (size_t i=0;i<mapped->bytes/sizeof(float);++i) {
                if (!std::isfinite(values[i])) throw std::runtime_error("nonfinite visual suffix input");
              }
            }
          }
          reset_state();
          const auto request_begin = Clock::now();
          const auto load_begin = Clock::now();
          const bool resident_seed = seed_path.string().rfind("shared-resident:", 0) == 0;
          const size_t seed_bytes = load_seed(seed_path, seeded_request_id, seed_position, history);
          if (embedding_remainder && (
              std::memcmp(input_ptrs[hidden_index],suffix_hidden,kEngineHidden*sizeof(float)) != 0 ||
              std::memcmp(input_ptrs[rope_cos_index],suffix_cos,kEngineRotary*sizeof(float)) != 0 ||
              std::memcmp(input_ptrs[rope_sin_index],suffix_sin,kEngineRotary*sizeof(float)) != 0)) {
            throw std::runtime_error("seed first input differs from visual suffix data");
          }
          const double seed_load_seconds = Seconds(load_begin, Clock::now());
          std::vector<int64_t> generated;
          std::vector<int64_t> completion_history;
          if (!any_remainder_command) completion_history.push_back(history.back());
          std::vector<double> remainder_run;
          std::vector<double> remainder_e2e;
          std::vector<double> decode_run;
          std::vector<double> decode_e2e;
          generated.reserve(max_new);
          decode_run.reserve(max_new);
          decode_e2e.reserve(max_new);
          const std::set<int64_t> stop_set(stop_ids.begin(), stop_ids.end());
          std::mt19937_64 generator(random_seed);
          const float* logits = static_cast<const float*>(output_ptrs[logits_index]);
          StepTiming timing = run_loaded_seed();
          if (any_remainder_command) {
            remainder_run.push_back(timing.run);
            remainder_e2e.push_back(timing.end_to_end);
            for (size_t index = 1; index < remainder.size(); ++index) {
              const size_t cache_position = seed_position + index;
              timing = embedding_remainder
                  ? run_embedding_with_rope(suffix_hidden + index*kEngineHidden, cache_position,
                                            suffix_cos + index*kEngineRotary, suffix_sin + index*kEngineRotary)
                  : run_token_with_rope(remainder[index], cache_position,
                                        static_cast<int64_t>(cache_position) + rope_delta);
              remainder_run.push_back(timing.run);
              remainder_e2e.push_back(timing.end_to_end);
            }
          } else {
            decode_run.push_back(timing.run);
            decode_e2e.push_back(timing.end_to_end);
          }
          const size_t history_tokens_before_generation = history.size();
          std::string finish_reason = "length";
          double first_token_seconds = 0.0;
          for (size_t generation_index = 0; generation_index < max_new; ++generation_index) {
            if (generation_index != 0) {
              const size_t cache_position =
                  history_tokens_before_generation + generation_index - 1;
              timing = run_token_with_rope(
                  generated.back(), cache_position,
                  static_cast<int64_t>(cache_position) + rope_delta);
              decode_run.push_back(timing.run);
              decode_e2e.push_back(timing.end_to_end);
            }
            const auto& penalty_history =
                (mode == "sample" || mode == "penalized_greedy" || mode == "no_repeat4_greedy")
                    ? completion_history
                    : history;
            if (logit_correction) logits = logit_correction->Run();
            const SampleDecision decision = SelectToken(
                logits, logits_count, mode, penalty_history, temperature, top_p, top_k, min_p,
                presence_penalty, repetition_penalty, frequency_penalty, generator);
            generated.push_back(static_cast<int64_t>(decision.token));
            history.push_back(static_cast<int64_t>(decision.token));
            completion_history.push_back(static_cast<int64_t>(decision.token));
            const double request_elapsed = Seconds(request_begin, Clock::now());
            if (generation_index == 0) first_token_seconds = request_elapsed;
            protocol << std::setprecision(17)
                     << "{\"type\":\"seeded_token\",\"request_id\":\""
                     << JsonEscape(seeded_request_id) << "\",\"index\":" << generation_index
                     << ",\"token_id\":" << decision.token
                     << ",\"raw_logit\":" << decision.raw_logit
                     << ",\"request_elapsed_seconds\":" << request_elapsed << ",\"top\":";
            WriteCandidates(protocol, decision.top);
            protocol << "}\n" << std::flush;
            if (stop_set.contains(static_cast<int64_t>(decision.token))) {
              finish_reason = "stop";
              break;
            }
          }
          const double request_seconds = Seconds(request_begin, Clock::now());
          const double decode_median = MedianOrZero(decode_e2e);
          protocol << std::setprecision(17)
                   << "{\"type\":\"seeded_done\",\"request_id\":\""
                   << JsonEscape(seeded_request_id) << "\",\"finish_reason\":\""
                   << finish_reason << "\",\"seed_bytes\":" << seed_bytes
                   << ",\"resident_seed\":" << (resident_seed ? "true" : "false")
                   << ",\"native_seed_encode_seconds\":" << (resident_seed ? shared_prefill->seed_seconds : 0.0)
                   << ",\"native_seed_clip_fraction\":" << (resident_seed ? double(shared_prefill->clipped) / shared_prefill->quantized : 0.0)
                   << ",\"native_seed_max_local_clip_fraction\":" << (resident_seed ? shared_prefill->max_local_clip_fraction : 0.0)
                   << ",\"seed_position\":" << seed_position
                   << ",\"history_tokens_before_generation\":"
                   << history_tokens_before_generation
                   << ",\"embedding_remainder\":" << (embedding_remainder ? "true" : "false")
                   << ",\"rope_delta\":" << rope_delta
                   << ",\"generation_rope_start\":" << generation_rope_start
                   << ",\"remainder_tokens\":" << remainder.size()
                   << ",\"completion_tokens\":" << generated.size()
                   << ",\"decoding_mode\":\"" << JsonEscape(mode) << "\""
                   << ",\"temperature\":" << temperature << ",\"top_p\":" << top_p
                   << ",\"top_k\":" << top_k << ",\"min_p\":" << min_p
                   << ",\"logit_correction_enabled\":" << (logit_correction ? "true" : "false")
                   << ",\"logit_correction_runs\":" << (logit_correction ? logit_correction->runs : 0)
                   << ",\"logit_correction_seconds\":" << (logit_correction ? logit_correction->compute_seconds : 0.0)
                   << ",\"frequency_penalty\":" << frequency_penalty
                   << ",\"presence_penalty\":" << presence_penalty
                   << ",\"repetition_penalty\":" << repetition_penalty
                   << ",\"penalty_scope\":\""
                   << ((mode == "sample" || mode == "penalized_greedy" || mode == "no_repeat4_greedy")
                           ? "completion"
                           : "full_history")
                   << "\""
                   << ",\"random_seed\":" << random_seed
                   << ",\"seed_load_seconds\":" << seed_load_seconds
                   << ",\"first_token_seconds\":" << first_token_seconds
                   << ",\"request_seconds\":" << request_seconds
                   << ",\"remainder_feed_steps\":" << remainder_e2e.size()
                   << ",\"remainder_run_seconds\":"
                   << std::accumulate(remainder_run.begin(), remainder_run.end(), 0.0)
                   << ",\"remainder_e2e_seconds\":"
                   << std::accumulate(remainder_e2e.begin(), remainder_e2e.end(), 0.0)
                   << ",\"decode_feed_steps\":" << decode_e2e.size()
                   << ",\"decode_run_median_seconds\":" << MedianOrZero(decode_run)
                   << ",\"decode_run_p95_seconds\":" << PercentileOrZero(decode_run, 0.95)
                   << ",\"decode_e2e_median_seconds\":" << decode_median
                   << ",\"decode_e2e_p95_seconds\":" << PercentileOrZero(decode_e2e, 0.95)
                   << ",\"decode_tokens_per_second\":"
                   << (decode_median > 0.0 ? 1.0 / decode_median : 0.0)
                   << ",\"state_loaded_from_seed\":true,\"state_reset_after_request\":true"
                   << ",\"all_logits_finite\":true}\n" << std::flush;
          reset_state();
        } catch (const std::exception& error) {
          reset_state();
          if (shared_prefill) shared_prefill->Invalidate();
          protocol << "{\"type\":\"error\",\"request_id\":\""
                   << JsonEscape(seeded_request_id) << "\",\"error\":\""
                   << JsonEscape(error.what())
                   << "\",\"state_reset_after_error\":true}\n" << std::flush;
        }
        continue;
      }
#endif

      if (line.rfind("VERIFY\t", 0) == 0) {
        std::string verify_id;
        try {
          const auto fields = Split(line, '\t');
          if (fields.size() != 3 || fields[0] != "VERIFY" || fields[1].empty()) {
            throw std::runtime_error("expected 3-field VERIFY TSV command");
          }
          verify_id = fields[1];
          const auto verify_tokens = ParseIds(fields[2], false);
          if (verify_tokens.size() > kEngineContext) {
            throw std::runtime_error("VERIFY sequence exceeds fixed context");
          }
          if (shared_prefill) shared_prefill->Invalidate();
          reset_state();
          const auto verify_begin = Clock::now();
          std::vector<int64_t> top1;
          std::vector<double> run_seconds;
          std::vector<double> e2e_seconds;
          top1.reserve(verify_tokens.size());
          run_seconds.reserve(verify_tokens.size());
          e2e_seconds.reserve(verify_tokens.size());
          const float* logits = static_cast<const float*>(output_ptrs[logits_index]);
          for (size_t position = 0; position < verify_tokens.size(); ++position) {
            const StepTiming timing = run_token(verify_tokens[position], position);
            size_t best = 0;
            for (size_t token = 0; token < logits_count; ++token) {
              if (!std::isfinite(logits[token])) throw std::runtime_error("non-finite VERIFY logits");
              if (logits[token] > logits[best]) best = token;
            }
            top1.push_back(static_cast<int64_t>(best));
            run_seconds.push_back(timing.run);
            e2e_seconds.push_back(timing.end_to_end);
          }
          const double e2e_median = MedianOrZero(e2e_seconds);
          protocol << std::setprecision(17)
                    << "{\"type\":\"verify\",\"request_id\":\"" << JsonEscape(verify_id)
                    << "\",\"steps\":" << verify_tokens.size() << ",\"top1\":[";
          for (size_t index = 0; index < top1.size(); ++index) {
            if (index != 0) protocol << ',';
            protocol << top1[index];
          }
          protocol << "],\"run_median_seconds\":" << MedianOrZero(run_seconds)
                    << ",\"run_p95_seconds\":" << PercentileOrZero(run_seconds, 0.95)
                    << ",\"e2e_median_seconds\":" << e2e_median
                    << ",\"e2e_p95_seconds\":" << PercentileOrZero(e2e_seconds, 0.95)
                    << ",\"e2e_minimum_seconds\":" << PercentileOrZero(e2e_seconds, 0.0)
                    << ",\"e2e_maximum_seconds\":" << PercentileOrZero(e2e_seconds, 1.0)
                    << ",\"e2e_tokens_per_second\":"
                    << (e2e_median > 0.0 ? 1.0 / e2e_median : 0.0)
                    << ",\"all_logits_finite\":true,\"state_reset_before_verify\":true"
                    << ",\"elapsed_seconds\":" << Seconds(verify_begin, Clock::now()) << "}\n"
                    << std::flush;
          reset_state();
        } catch (const std::exception& error) {
          reset_state();
          if (shared_prefill) shared_prefill->Invalidate();
          protocol << "{\"type\":\"error\",\"request_id\":\"" << JsonEscape(verify_id)
                    << "\",\"error\":\"" << JsonEscape(error.what())
                    << "\",\"state_reset_after_error\":true}\n" << std::flush;
        }
        continue;
      }

      std::string request_id;
      try {
        const auto fields = Split(line, '\t');
        if (fields.size() != 13 || fields[0] != "GENERATE") {
          throw std::runtime_error("expected 13-field GENERATE TSV command");
        }
        request_id = fields[1];
        const std::string& mode = fields[2];
        const size_t max_new = std::stoull(fields[3]);
        const uint64_t seed = std::stoull(fields[4]);
        const double temperature = std::stod(fields[5]);
        const double top_p = std::stod(fields[6]);
        const size_t top_k = std::stoull(fields[7]);
        const double min_p = std::stod(fields[8]);
        const double presence_penalty = std::stod(fields[9]);
        const double repetition_penalty = std::stod(fields[10]);
        const auto stop_ids = ParseIds(fields[11], true);
        const auto prompt_ids = ParseIds(fields[12], false);
        if (request_id.empty() ||
            (mode != "greedy" && mode != "sample" && mode != "penalized_greedy") ||
            max_new == 0 ||
            max_new > kEngineContext || prompt_ids.size() >= kEngineContext ||
            !(temperature > 0.0) || !(top_p > 0.0 && top_p <= 1.0) || top_k == 0 ||
            top_k > kEngineVocab || !(min_p >= 0.0 && min_p <= 1.0) ||
            !std::isfinite(presence_penalty) || presence_penalty < -2.0 || presence_penalty > 2.0 ||
            !(repetition_penalty > 0.0) || !std::isfinite(repetition_penalty)) {
          throw std::runtime_error("generation parameter contract mismatch");
        }

        if (shared_prefill) shared_prefill->Invalidate();
        reset_state();
        const auto request_begin = Clock::now();
        std::vector<double> prompt_run;
        std::vector<double> prompt_e2e;
        std::vector<double> decode_run;
        std::vector<double> decode_e2e;
        std::vector<int64_t> history = prompt_ids;
        std::vector<int64_t> completion_history;
        std::vector<int64_t> generated;
        prompt_run.reserve(prompt_ids.size());
        prompt_e2e.reserve(prompt_ids.size());
        completion_history.reserve(max_new);
        generated.reserve(max_new);
        for (size_t position = 0; position < prompt_ids.size(); ++position) {
          const StepTiming timing = run_token(prompt_ids[position], position);
          prompt_run.push_back(timing.run);
          prompt_e2e.push_back(timing.end_to_end);
        }

        const float* logits = static_cast<const float*>(output_ptrs[logits_index]);
        const std::set<int64_t> stop_set(stop_ids.begin(), stop_ids.end());
        std::mt19937_64 generator(seed);
        std::string finish_reason = "length";
        double first_token_seconds = 0.0;
        for (size_t generation_index = 0; generation_index < max_new; ++generation_index) {
          if (history.size() >= kEngineContext) {
            finish_reason = "context_length";
            break;
          }
          if (generation_index != 0) {
            const size_t position = prompt_ids.size() + generation_index - 1;
            const StepTiming timing = run_token(generated.back(), position);
            decode_run.push_back(timing.run);
            decode_e2e.push_back(timing.end_to_end);
          }
          const auto& penalty_history =
              (mode == "sample" || mode == "penalized_greedy") ? completion_history : history;
          if (logit_correction) logits = logit_correction->Run();
          const SampleDecision decision = SelectToken(
              logits, logits_count, mode, penalty_history, temperature, top_p, top_k, min_p,
              presence_penalty, repetition_penalty, frequency_penalty, generator);
          generated.push_back(static_cast<int64_t>(decision.token));
          history.push_back(static_cast<int64_t>(decision.token));
          completion_history.push_back(static_cast<int64_t>(decision.token));
          const double request_elapsed = Seconds(request_begin, Clock::now());
          if (generation_index == 0) first_token_seconds = request_elapsed;
          protocol << std::setprecision(17)
                    << "{\"type\":\"token\",\"request_id\":\"" << JsonEscape(request_id)
                    << "\",\"index\":" << generation_index << ",\"token_id\":" << decision.token
                    << ",\"raw_logit\":" << decision.raw_logit
                    << ",\"retained_candidates\":" << decision.retained_candidates
                    << ",\"request_elapsed_seconds\":" << request_elapsed << ",\"top\":";
          WriteCandidates(protocol, decision.top);
          protocol << "}\n" << std::flush;
          if (stop_set.contains(static_cast<int64_t>(decision.token))) {
            finish_reason = "stop";
            break;
          }
        }

        const double request_seconds = Seconds(request_begin, Clock::now());
        const double decode_e2e_median = MedianOrZero(decode_e2e);
        protocol << std::setprecision(17)
                  << "{\"type\":\"done\",\"request_id\":\"" << JsonEscape(request_id)
                  << "\",\"finish_reason\":\"" << finish_reason << "\""
                  << ",\"logit_correction_enabled\":" << (logit_correction ? "true" : "false")
                   << ",\"logit_correction_runs\":" << (logit_correction ? logit_correction->runs : 0)
                   << ",\"logit_correction_seconds\":" << (logit_correction ? logit_correction->compute_seconds : 0.0)
                   << ",\"frequency_penalty\":" << frequency_penalty
                  << ",\"prompt_tokens\":" << prompt_ids.size()
                  << ",\"completion_tokens\":" << generated.size()
                  << ",\"first_token_seconds\":" << first_token_seconds
                  << ",\"request_seconds\":" << request_seconds
                  << ",\"prompt_run_seconds_sum\":"
                  << std::accumulate(prompt_run.begin(), prompt_run.end(), 0.0)
                  << ",\"prompt_e2e_seconds_sum\":"
                  << std::accumulate(prompt_e2e.begin(), prompt_e2e.end(), 0.0)
                  << ",\"decode_feed_steps\":" << decode_e2e.size()
                  << ",\"decode_run_median_seconds\":" << MedianOrZero(decode_run)
                  << ",\"decode_run_p95_seconds\":" << PercentileOrZero(decode_run, 0.95)
                  << ",\"decode_e2e_median_seconds\":" << decode_e2e_median
                  << ",\"decode_e2e_p95_seconds\":" << PercentileOrZero(decode_e2e, 0.95)
                  << ",\"prompt_e2e_p95_seconds\":" << PercentileOrZero(prompt_e2e, 0.95)
                  << ",\"decode_tokens_per_second\":"
                  << (decode_e2e_median > 0.0 ? 1.0 / decode_e2e_median : 0.0)
                  << ",\"state_reset_before_request\":true,\"all_logits_finite\":true}\n"
                  << std::flush;
      } catch (const std::exception& error) {
        reset_state();
        if (shared_prefill) shared_prefill->Invalidate();
        protocol << "{\"type\":\"error\",\"request_id\":\"" << JsonEscape(request_id)
                  << "\",\"error\":\"" << JsonEscape(error.what())
                  << "\",\"state_reset_after_error\":true}\n" << std::flush;
      }
    }
    reset_state();
    return 0;
  } catch (const Ort::Exception& error) {
    std::cerr << "ORT exception in " << phase << ": " << error.what() << '\n';
    return 3;
  } catch (const std::exception& error) {
    std::cerr << "exception in " << phase << ": " << error.what() << '\n';
    return 2;
  }
}
