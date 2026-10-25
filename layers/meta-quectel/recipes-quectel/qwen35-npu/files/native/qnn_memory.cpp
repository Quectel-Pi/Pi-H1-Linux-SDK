#include <algorithm>
#include <arm_neon.h>
#include <chrono>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "onnxruntime_cxx_api.h"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr size_t kSteps = 64;
constexpr size_t kWarmup = 8;
constexpr size_t kExpectedInputs = 578;
constexpr size_t kExpectedOutputs = 575;
constexpr size_t kExpectedAdvanceRecords = 574;
constexpr size_t kExpectedTeacherBytesPerStep = 12800;
constexpr size_t kExpectedInputBytes = 35930624;
constexpr size_t kExpectedOutputBytes = 36911104;
constexpr size_t kExpectedAdvanceBytes = 35917824;
constexpr char kQnnEpName[] = "QNNExecutionProvider";

struct Args {
  fs::path model;
  fs::path plugin;
  fs::path backend;
  fs::path tensor_contract;
  fs::path teacher_layout;
  fs::path teacher_binary;
  fs::path advance_plan;
  fs::path reference_top1;
  fs::path output_dir;
  fs::path result;
};

struct TensorRecord {
  std::string kind;
  size_t position{};
  std::string name;
  std::string dtype;
  std::vector<int64_t> shape;
  size_t elements{};
  size_t bytes{};
};

struct TeacherRecord {
  std::string input_name;
  std::string bundle_name;
  size_t offset_bytes{};
  size_t bytes{};
};

struct AdvanceRecord {
  std::string source;
  size_t source_offset{};
  std::string destination;
  size_t destination_offset{};
  size_t rows{};
  size_t row_elements{};
  size_t source_stride{};
  size_t destination_stride{};
  std::string category;
  std::string operation;
  std::string source_dtype;
  std::string destination_dtype;
  float output_scale{};
  int64_t output_zero_point{};
  float input_scale{};
  int64_t input_zero_point{};
};

struct ReferenceRecord {
  int64_t device_top1{};
  int64_t pc_top1{};
  int64_t official_top1{};
};

struct TimingSummary {
  size_t count{};
  double minimum{};
  double median{};
  double maximum{};
  double mean{};
  double sum{};
};

std::string JsonEscape(std::string_view value) {
  std::ostringstream out;
  for (unsigned char c : value) {
    switch (c) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (c < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(c) << std::dec << std::setfill(' ');
        } else {
          out << static_cast<char>(c);
        }
    }
  }
  return out.str();
}

std::vector<std::string> Split(const std::string& value, char delimiter) {
  std::vector<std::string> fields;
  std::stringstream stream(value);
  std::string field;
  while (std::getline(stream, field, delimiter)) fields.push_back(field);
  if (!value.empty() && value.back() == delimiter) fields.emplace_back();
  return fields;
}

std::vector<std::string> ReadLines(const fs::path& path) {
  std::ifstream stream(path);
  if (!stream) throw std::runtime_error("cannot open text file: " + path.string());
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

std::vector<std::byte> ReadBinary(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw std::runtime_error("cannot open binary file: " + path.string());
  const auto size = stream.tellg();
  if (size < 0) throw std::runtime_error("cannot stat binary file: " + path.string());
  std::vector<std::byte> data(static_cast<size_t>(size));
  stream.seekg(0);
  stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!stream) throw std::runtime_error("short binary read: " + path.string());
  return data;
}

std::string ReadTrimmed(const fs::path& path) {
  std::ifstream stream(path);
  if (!stream) return {};
  std::string value;
  std::getline(stream, value);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
    value.pop_back();
  }
  return value;
}

double Seconds(Clock::time_point begin, Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

TimingSummary Summarize(const std::vector<double>& values, size_t begin) {
  if (begin >= values.size()) throw std::runtime_error("empty timing window");
  std::vector<double> sorted(values.begin() + static_cast<std::ptrdiff_t>(begin), values.end());
  std::sort(sorted.begin(), sorted.end());
  const size_t count = sorted.size();
  const double median = count % 2 == 0
                            ? (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0
                            : sorted[count / 2];
  const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
  return {count, sorted.front(), median, sorted.back(), sum / static_cast<double>(count), sum};
}

void WriteSummary(std::ostream& stream, const TimingSummary& value, int indent) {
  const std::string pad(static_cast<size_t>(indent), ' ');
  stream << "{\n"
         << pad << "  \"count\": " << value.count << ",\n"
         << pad << "  \"minimum\": " << value.minimum << ",\n"
         << pad << "  \"median\": " << value.median << ",\n"
         << pad << "  \"maximum\": " << value.maximum << ",\n"
         << pad << "  \"mean\": " << value.mean << ",\n"
         << pad << "  \"sum\": " << value.sum << "\n"
         << pad << "}";
}

Args ParseArgs(int argc, char** argv) {
  if (argc != 11) {
    throw std::runtime_error(
        "usage: qnn_memory MODEL PLUGIN BACKEND TENSOR_CONTRACT "
        "TEACHER_LAYOUT TEACHER_BINARY ADVANCE_PLAN REFERENCE_TOP1 OUTPUT_DIR RESULT");
  }
  return {argv[1], argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9], argv[10]};
}

size_t ElementSize(std::string_view dtype) {
  if (dtype == "float32") return sizeof(float);
  if (dtype == "uint16") return sizeof(uint16_t);
  throw std::runtime_error("unsupported tensor dtype: " + std::string(dtype));
}

ONNXTensorElementDataType ElementType(std::string_view dtype) {
  if (dtype == "float32") return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
  if (dtype == "uint16") return ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16;
  throw std::runtime_error("unsupported tensor dtype: " + std::string(dtype));
}

uint16_t RequantizeUint16Code(uint16_t value, const AdvanceRecord& record) {
  const int32_t centered = static_cast<int32_t>(value) -
                           static_cast<int32_t>(record.output_zero_point);
  const float decoded = static_cast<float>(centered) * record.output_scale;
  const float scaled = decoded / record.input_scale;
  const int64_t rounded = static_cast<int64_t>(std::nearbyintf(scaled));
  const int64_t converted = rounded + record.input_zero_point;
  return static_cast<uint16_t>(
      std::clamp<int64_t>(converted, 0, std::numeric_limits<uint16_t>::max()));
}

bool IsExactIdentityRequantize(const AdvanceRecord& record) {
  if (record.operation != "requantize_uint16") return false;
  for (uint32_t code = 0; code <= std::numeric_limits<uint16_t>::max(); ++code) {
    const auto value = static_cast<uint16_t>(code);
    if (RequantizeUint16Code(value, record) != value) return false;
  }
  return true;
}

void RequantizeNeon8(const uint16_t* source, uint16_t* destination,
                     const AdvanceRecord& record) {
  const uint16x8_t input = vld1q_u16(source);
  const int32x4_t output_zero =
      vdupq_n_s32(static_cast<int32_t>(record.output_zero_point));
  const float32x4_t output_scale = vdupq_n_f32(record.output_scale);
  const float32x4_t input_scale = vdupq_n_f32(record.input_scale);
  const int32x4_t input_zero =
      vdupq_n_s32(static_cast<int32_t>(record.input_zero_point));
  const int32x4_t low_centered = vsubq_s32(
      vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(input))), output_zero);
  const int32x4_t high_centered = vsubq_s32(
      vreinterpretq_s32_u32(vmovl_high_u16(input)), output_zero);
  const float32x4_t low_scaled = vdivq_f32(
      vmulq_f32(vcvtq_f32_s32(low_centered), output_scale), input_scale);
  const float32x4_t high_scaled = vdivq_f32(
      vmulq_f32(vcvtq_f32_s32(high_centered), output_scale), input_scale);
  const int32x4_t low_rounded =
      vaddq_s32(vcvtnq_s32_f32(low_scaled), input_zero);
  const int32x4_t high_rounded =
      vaddq_s32(vcvtnq_s32_f32(high_scaled), input_zero);
  vst1q_u16(destination,
            vcombine_u16(vqmovun_s32(low_rounded), vqmovun_s32(high_rounded)));
}

bool IsExactNeonRequantize(const AdvanceRecord& record) {
  if (record.operation != "requantize_uint16" || IsExactIdentityRequantize(record)) {
    return false;
  }
  alignas(16) uint16_t source[8];
  alignas(16) uint16_t destination[8];
  for (uint32_t base = 0; base < 65536; base += 8) {
    for (uint32_t lane = 0; lane < 8; ++lane) {
      source[lane] = static_cast<uint16_t>(base + lane);
    }
    RequantizeNeon8(source, destination, record);
    for (uint32_t lane = 0; lane < 8; ++lane) {
      if (destination[lane] != RequantizeUint16Code(source[lane], record)) return false;
    }
  }
  return true;
}

std::vector<TensorRecord> LoadTensorContract(const fs::path& path) {
  const auto lines = ReadLines(path);
  if (lines.empty() || lines.front() != "kind\tposition\tname\tdtype\tshape\telements\tbytes") {
    throw std::runtime_error("tensor contract header mismatch");
  }
  std::vector<TensorRecord> records;
  for (size_t index = 1; index < lines.size(); ++index) {
    const auto fields = Split(lines[index], '\t');
    if (fields.size() != 7) throw std::runtime_error("tensor contract field count mismatch");
    TensorRecord item;
    item.kind = fields[0];
    item.position = std::stoull(fields[1]);
    item.name = fields[2];
    item.dtype = fields[3];
    for (const auto& dimension : Split(fields[4], ',')) item.shape.push_back(std::stoll(dimension));
    item.elements = std::stoull(fields[5]);
    item.bytes = std::stoull(fields[6]);
    if (item.elements * ElementSize(item.dtype) != item.bytes) {
      throw std::runtime_error("tensor contract element/byte mismatch: " + item.name);
    }
    records.push_back(std::move(item));
  }
  return records;
}

std::vector<TeacherRecord> LoadTeacherLayout(const fs::path& path) {
  const auto lines = ReadLines(path);
  if (lines.empty() || lines.front() != "input_name\tbundle_name\toffset_bytes\tbytes") {
    throw std::runtime_error("teacher layout header mismatch");
  }
  std::vector<TeacherRecord> records;
  for (size_t index = 1; index < lines.size(); ++index) {
    const auto fields = Split(lines[index], '\t');
    if (fields.size() != 4) throw std::runtime_error("teacher layout field count mismatch");
    records.push_back({fields[0], fields[1], std::stoull(fields[2]), std::stoull(fields[3])});
  }
  return records;
}

std::vector<AdvanceRecord> LoadAdvancePlan(const fs::path& path) {
  const auto lines = ReadLines(path);
  const std::string expected =
      "source\tsource_offset_elements\tdestination\tdestination_offset_elements\trows\t"
      "row_elements\tsource_stride_elements\tdestination_stride_elements\tcategory\toperation\t"
      "source_dtype\tdestination_dtype\toutput_scale\toutput_zero_point\tinput_scale\tinput_zero_point";
  if (lines.empty() || lines.front() != expected) throw std::runtime_error("advance plan header mismatch");
  std::vector<AdvanceRecord> records;
  for (size_t index = 1; index < lines.size(); ++index) {
    const auto fields = Split(lines[index], '\t');
    if (fields.size() != 16) throw std::runtime_error("advance plan field count mismatch");
    records.push_back({fields[0], std::stoull(fields[1]), fields[2], std::stoull(fields[3]),
                       std::stoull(fields[4]), std::stoull(fields[5]), std::stoull(fields[6]),
                       std::stoull(fields[7]), fields[8], fields[9], fields[10], fields[11],
                       std::stof(fields[12]), std::stoll(fields[13]), std::stof(fields[14]),
                       std::stoll(fields[15])});
  }
  return records;
}

std::vector<ReferenceRecord> LoadReference(const fs::path& path) {
  const auto lines = ReadLines(path);
  if (lines.empty() || lines.front() != "step\tr15_device_top1\tpc_qdq_top1\tofficial_top1") {
    throw std::runtime_error("reference header mismatch");
  }
  std::vector<ReferenceRecord> records;
  for (size_t index = 1; index < lines.size(); ++index) {
    const auto fields = Split(lines[index], '\t');
    if (fields.size() != 4 || std::stoull(fields[0]) != index) {
      throw std::runtime_error("reference field/order mismatch");
    }
    records.push_back({std::stoll(fields[1]), std::stoll(fields[2]), std::stoll(fields[3])});
  }
  return records;
}

std::vector<int64_t> GetShape(const Ort::Session& session, bool input, size_t position) {
  auto type = input ? session.GetInputTypeInfo(position) : session.GetOutputTypeInfo(position);
  auto tensor = type.GetTensorTypeAndShapeInfo();
  auto shape = tensor.GetShape();
  if (std::any_of(shape.begin(), shape.end(), [](int64_t value) { return value <= 0; })) {
    throw std::runtime_error("dynamic/nonpositive context boundary shape");
  }
  return shape;
}

ONNXTensorElementDataType GetElementType(const Ort::Session& session, bool input, size_t position) {
  auto type = input ? session.GetInputTypeInfo(position) : session.GetOutputTypeInfo(position);
  return type.GetTensorTypeAndShapeInfo().GetElementType();
}

size_t ShapeElements(const std::vector<int64_t>& shape) {
  size_t value = 1;
  for (int64_t dimension : shape) value *= static_cast<size_t>(dimension);
  return value;
}

void WriteFailure(const Args& args, std::string_view phase, std::string_view error, std::string_view boot_id) {
  fs::create_directories(args.result.parent_path());
  std::ofstream stream(args.result);
  stream << "{\n"
         << "  \"schema\": \"qnn_qnn_htp_shared_compact_kv_neon_cxx_v10\",\n"
         << "  \"status\": \"capability_fail\",\n"
         << "  \"allocator\": \"QnnHtpShared\",\n"
         << "  \"boot_id\": \"" << JsonEscape(boot_id) << "\",\n"
         << "  \"exception_phase\": \"" << JsonEscape(phase) << "\",\n"
         << "  \"exception\": \"" << JsonEscape(error) << "\"\n"
         << "}\n";
}

int main(int argc, char** argv) {
  Args args;
  std::string phase = "argument_validation";
  std::string boot_id = ReadTrimmed("/proc/sys/kernel/random/boot_id");
  try {
    args = ParseArgs(argc, argv);
    if (fs::exists(args.output_dir) || fs::exists(args.result)) {
      throw std::runtime_error("refusing to overwrite C++ shared-memory result");
    }
    fs::create_directories(args.output_dir);
    for (const auto& path : {args.model, args.plugin, args.backend, args.tensor_contract,
                             args.teacher_layout, args.teacher_binary, args.advance_plan,
                             args.reference_top1}) {
      if (!fs::is_regular_file(path)) throw std::runtime_error("required file absent: " + path.string());
    }

    phase = "flat_input_validation";
    const auto tensor_contract = LoadTensorContract(args.tensor_contract);
    const auto teacher_layout = LoadTeacherLayout(args.teacher_layout);
    const auto teacher_binary = ReadBinary(args.teacher_binary);
    const auto advance_plan = LoadAdvancePlan(args.advance_plan);
    const auto reference = LoadReference(args.reference_top1);
    if (teacher_layout.size() != 4 || teacher_binary.size() != kSteps * kExpectedTeacherBytesPerStep ||
        advance_plan.size() != kExpectedAdvanceRecords || reference.size() != kSteps) {
      throw std::runtime_error("flat input count/size contract mismatch");
    }
    if (std::fegetround() != FE_TONEAREST) {
      throw std::runtime_error("host floating-point rounding mode is not round-to-nearest-even");
    }

    phase = "environment_and_plugin_registration";
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "qnn_qnn_htp_shared_segmented_cxx");
    env.RegisterExecutionProviderLibrary(kQnnEpName, args.plugin.string());
    std::vector<Ort::ConstEpDevice> qnn_devices;
    for (const auto& device : env.GetEpDevices()) {
      if (std::string_view(device.EpName()) == kQnnEpName) qnn_devices.push_back(device);
    }
    if (qnn_devices.empty()) throw std::runtime_error("no QNN OrtEpDevice after library registration");

    phase = "session_create";
    Ort::SessionOptions options;
    options.SetLogSeverityLevel(ORT_LOGGING_LEVEL_WARNING);
    options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
    options.AddConfigEntry("ep.context_enable", "0");
    const fs::path profile_prefix = args.output_dir / "qnn_cxx_shared_ort_profile";
    options.EnableProfiling(profile_prefix.c_str());
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
        throw std::runtime_error("context input differs from frozen tensor contract at index " +
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
        throw std::runtime_error("context output differs from frozen tensor contract at index " +
                                 std::to_string(index));
      }
      output_bytes += expected.bytes;
    }
    if (input_bytes != kExpectedInputBytes || output_bytes != kExpectedOutputBytes) {
      throw std::runtime_error("context byte volume mismatch");
    }

    phase = "qnn_htp_shared_allocator_create";
    Ort::MemoryInfo shared_info("QnnHtpShared", OrtAllocatorType::OrtDeviceAllocator, 0, OrtMemTypeCPU);
    Ort::Allocator shared_allocator(session, shared_info);
    const auto allocator_info = shared_allocator.GetInfo();
    if (allocator_info.GetAllocatorName() != "QnnHtpShared") {
      throw std::runtime_error("session allocator is not QnnHtpShared");
    }

    phase = "qnn_htp_shared_allocation_and_binding";
    std::vector<Ort::MemoryAllocation> input_allocations;
    std::vector<Ort::MemoryAllocation> output_allocations;
    std::vector<Ort::Value> input_values;
    std::vector<Ort::Value> output_values;
    std::vector<void*> input_ptrs;
    std::vector<void*> output_ptrs;
    input_allocations.reserve(kExpectedInputs);
    output_allocations.reserve(kExpectedOutputs);
    input_values.reserve(kExpectedInputs);
    output_values.reserve(kExpectedOutputs);
    input_ptrs.reserve(kExpectedInputs);
    output_ptrs.reserve(kExpectedOutputs);
    std::unordered_map<std::string, size_t> input_index;
    std::unordered_map<std::string, size_t> output_index;

    for (size_t index = 0; index < kExpectedInputs; ++index) {
      const auto& expected = tensor_contract[index];
      phase = "qnn_htp_shared_allocator_input_" + std::to_string(index) +
              "_bytes_" + std::to_string(expected.bytes);
      auto allocation = shared_allocator.GetAllocation(expected.bytes);
      auto* pointer = allocation.get();
      if (pointer == nullptr) throw std::runtime_error("null shared input allocation");
      std::memset(pointer, 0, expected.bytes);
      input_values.push_back(Ort::Value::CreateTensor(
          shared_info, pointer, expected.bytes, input_shapes[index].data(), input_shapes[index].size(),
          input_types[index]));
      input_ptrs.push_back(pointer);
      input_allocations.push_back(std::move(allocation));
      input_index.emplace(input_names[index], index);
    }
    for (size_t index = 0; index < kExpectedOutputs; ++index) {
      output_index.emplace(output_names[index], index);
    }
    std::unordered_map<std::string, std::string> uint16_output_to_input;
    size_t aliased_state_pairs = 0;
    size_t aliased_kv_pairs = 0;
    for (const auto& record : advance_plan) {
      if (record.category != "deltanet_recurrent_state" &&
          record.category != "attention_kv") continue;
      if (record.source_dtype != "uint16" || record.destination_dtype != "uint16") {
        throw std::runtime_error("state alias record is not UINT16");
      }
      if (!output_index.contains(record.source) || !input_index.contains(record.destination)) {
        throw std::runtime_error("UINT16 alias endpoint mismatch");
      }
      const auto source_position = output_index.at(record.source);
      const auto destination_position = input_index.at(record.destination);
      const auto& source_contract = tensor_contract[kExpectedInputs + source_position];
      const auto& destination_contract = tensor_contract[destination_position];
      if (source_contract.bytes != destination_contract.bytes ||
          source_contract.elements != destination_contract.elements ||
          source_contract.dtype != destination_contract.dtype ||
          record.source_offset != 0 || record.destination_offset != 0 ||
          record.rows * record.row_elements != source_contract.elements ||
          record.source_stride != record.row_elements ||
          record.destination_stride != record.row_elements) {
        throw std::runtime_error("UINT16 alias tensor is not whole, contiguous, and size-identical");
      }
      if (!uint16_output_to_input.emplace(record.source, record.destination).second) {
        throw std::runtime_error("duplicate UINT16 alias source");
      }
      aliased_state_pairs += record.category == "deltanet_recurrent_state" ? 1 : 0;
      aliased_kv_pairs += record.category == "attention_kv" ? 1 : 0;
    }
    if (uint16_output_to_input.size() != 556 || aliased_state_pairs != 544 ||
        aliased_kv_pairs != 12) {
      throw std::runtime_error("UINT16 recurrent-state/compact-KV alias count mismatch");
    }
    for (size_t index = 0; index < kExpectedOutputs; ++index) {
      const auto& expected = tensor_contract[kExpectedInputs + index];
      void* pointer = nullptr;
      const auto alias = uint16_output_to_input.find(output_names[index]);
      if (alias != uint16_output_to_input.end()) {
        phase = "qnn_htp_shared_alias_output_" + std::to_string(index);
        pointer = input_ptrs[input_index.at(alias->second)];
      } else {
        phase = "qnn_htp_shared_allocator_output_" + std::to_string(index) +
                "_bytes_" + std::to_string(expected.bytes);
        auto allocation = shared_allocator.GetAllocation(expected.bytes);
        pointer = allocation.get();
        if (pointer == nullptr) throw std::runtime_error("null shared output allocation");
        output_allocations.push_back(std::move(allocation));
      }
      output_values.push_back(Ort::Value::CreateTensor(
          shared_info, pointer, expected.bytes, output_shapes[index].data(), output_shapes[index].size(),
          output_types[index]));
      output_ptrs.push_back(pointer);
    }
    phase = "qnn_htp_shared_io_binding";
    Ort::IoBinding binding(session);
    for (size_t index = 0; index < kExpectedInputs; ++index) {
      binding.BindInput(input_names[index].c_str(), input_values[index]);
    }
    for (size_t index = 0; index < kExpectedOutputs; ++index) {
      binding.BindOutput(output_names[index].c_str(), output_values[index]);
    }

    std::unordered_map<std::string, const TeacherRecord*> teacher_by_name;
    for (const auto& record : teacher_layout) {
      if (!input_index.contains(record.input_name) ||
          record.offset_bytes + record.bytes > kExpectedTeacherBytesPerStep ||
          tensor_contract[input_index.at(record.input_name)].dtype != "float32") {
        throw std::runtime_error("teacher layout endpoint/range mismatch");
      }
      teacher_by_name.emplace(record.input_name, &record);
    }
    if (teacher_by_name.size() != 4) throw std::runtime_error("teacher layout name mismatch");
    size_t planned_advance_bytes = 0;
    size_t state_copy_records = 0;
    size_t state_requantize_records = 0;
    size_t kv_copy_records = 0;
    size_t kv_requantize_records = 0;
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
      planned_advance_bytes +=
          record.rows * record.row_elements * ElementSize(record.destination_dtype);
      if (record.operation == "copy_float32") {
        if (record.source_dtype != "float32" || record.destination_dtype != "float32") {
          throw std::runtime_error("invalid float copy operation");
        }
      } else if (record.operation == "copy_uint16" ||
                 record.operation == "requantize_uint16") {
        if (record.source_dtype != "uint16" || record.destination_dtype != "uint16" ||
            record.input_scale <= 0.0f || record.output_scale <= 0.0f) {
          throw std::runtime_error("invalid UINT16 advance operation");
        }
        if (record.category == "deltanet_recurrent_state") {
          state_copy_records += record.operation == "copy_uint16" ? 1 : 0;
          state_requantize_records += record.operation == "requantize_uint16" ? 1 : 0;
        } else if (record.category == "attention_kv") {
          kv_copy_records += record.operation == "copy_uint16" ? 1 : 0;
          kv_requantize_records += record.operation == "requantize_uint16" ? 1 : 0;
        } else {
          throw std::runtime_error("unexpected UINT16 advance category");
        }
        if (initialized_uint16_inputs.contains(record.destination)) {
          throw std::runtime_error("duplicate UINT16 advance destination");
        }
        auto* destination = static_cast<uint16_t*>(input_ptrs[input_index.at(record.destination)]);
        std::fill_n(destination, destination_contract.elements,
                    static_cast<uint16_t>(record.input_zero_point));
        initialized_uint16_inputs.emplace(record.destination, true);
      } else {
        throw std::runtime_error("unknown advance operation: " + record.operation);
      }
    }
    if (planned_advance_bytes != kExpectedAdvanceBytes || state_copy_records != 185 ||
        state_requantize_records != 359 || kv_copy_records != 12 ||
        kv_requantize_records != 0 || initialized_uint16_inputs.size() != 556) {
      throw std::runtime_error("advance plan byte volume mismatch");
    }
    std::vector<bool> exact_identity_requantize;
    exact_identity_requantize.reserve(advance_plan.size());
    size_t identity_requantize_records = 0;
    size_t identity_requantize_elements = 0;
    size_t state_identity_requantize_records = 0;
    size_t kv_identity_requantize_records = 0;
    std::vector<bool> exact_neon_requantize;
    exact_neon_requantize.reserve(advance_plan.size());
    size_t neon_exact_nonidentity_records = 0;
    size_t neon_exact_nonidentity_elements = 0;
    for (const auto& record : advance_plan) {
      const bool identity = IsExactIdentityRequantize(record);
      exact_identity_requantize.push_back(identity);
      const bool neon_exact = !identity && IsExactNeonRequantize(record);
      exact_neon_requantize.push_back(neon_exact);
      if (identity) {
        ++identity_requantize_records;
        identity_requantize_elements += record.rows * record.row_elements;
        state_identity_requantize_records +=
            record.category == "deltanet_recurrent_state" ? 1 : 0;
        kv_identity_requantize_records += record.category == "attention_kv" ? 1 : 0;
      } else if (neon_exact) {
        ++neon_exact_nonidentity_records;
        neon_exact_nonidentity_elements += record.rows * record.row_elements;
      }
    }
    if (identity_requantize_records != 301 || identity_requantize_elements != 2'626'560 ||
        state_identity_requantize_records != 301 || kv_identity_requantize_records != 0) {
      throw std::runtime_error("exact UINT16 identity-fastpath proof count mismatch");
    }
    if (neon_exact_nonidentity_records != 58 ||
        neon_exact_nonidentity_elements != 571'392) {
      throw std::runtime_error("exact UINT16 NEON proof count mismatch");
    }
    phase = "exact_nonidentity_requantize_lut_build";
    std::vector<std::vector<uint16_t>> requantize_luts(advance_plan.size());
    size_t nonidentity_lut_records = 0;
    size_t nonidentity_lut_bytes = 0;
    for (size_t index = 0; index < advance_plan.size(); ++index) {
      const auto& record = advance_plan[index];
      if (record.operation != "requantize_uint16" || exact_identity_requantize[index]) continue;
      auto& lut = requantize_luts[index];
      lut.resize(static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1);
      for (uint32_t code = 0; code <= std::numeric_limits<uint16_t>::max(); ++code) {
        lut[code] = RequantizeUint16Code(static_cast<uint16_t>(code), record);
      }
      ++nonidentity_lut_records;
      nonidentity_lut_bytes += lut.size() * sizeof(uint16_t);
    }
    if (nonidentity_lut_records != 58 || nonidentity_lut_bytes != 7'602'176) {
      throw std::runtime_error("exact nonidentity UINT16 LUT count/size mismatch");
    }

    phase = "teacher64_execution";
    std::vector<double> run_seconds;
    std::vector<double> advance_seconds;
    std::vector<double> e2e_seconds;
    std::vector<int64_t> actual_top1;
    run_seconds.reserve(kSteps);
    advance_seconds.reserve(kSteps);
    e2e_seconds.reserve(kSteps);
    actual_top1.reserve(kSteps);
    bool all_finite = true;
    size_t reference_matches = 0;
    size_t pc_matches = 0;
    size_t official_matches = 0;
    const size_t logits_index = output_index.at("logits_fp32");
    const size_t logits_count = ShapeElements(output_shapes[logits_index]);
    const fs::path timing_path = args.output_dir / "teacher64_timing.tsv";
    std::ofstream timing_stream(timing_path);
    timing_stream << "step\tdevice_top1\tr15_reference_top1\tpc_qdq_top1\tofficial_top1\t"
                     "r15_match\tpc_match\tofficial_match\tall_outputs_finite\trun_seconds\t"
                     "advance_seconds\tend_to_end_excluding_validation_seconds\n";
    Ort::RunOptions run_options;

    for (size_t step = 0; step < kSteps; ++step) {
      const auto e2e_begin = Clock::now();
      const auto* teacher_step = teacher_binary.data() + step * kExpectedTeacherBytesPerStep;
      for (const auto& record : teacher_layout) {
        auto* destination = reinterpret_cast<std::byte*>(input_ptrs[input_index.at(record.input_name)]);
        std::memcpy(destination, teacher_step + record.offset_bytes, record.bytes);
      }
      binding.SynchronizeInputs();
      const auto run_begin = Clock::now();
      session.Run(run_options, binding);
      const double run_elapsed = Seconds(run_begin, Clock::now());
      binding.SynchronizeOutputs();

      const float* logits = static_cast<const float*>(output_ptrs[logits_index]);
      size_t best = 0;
      for (size_t index = 1; index < logits_count; ++index) {
        if (logits[index] > logits[best]) best = index;
      }

      const auto advance_begin = Clock::now();
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
          const auto* source = static_cast<const uint16_t*>(
                                   output_ptrs[output_index.at(record.source)]) +
                               record.source_offset;
          auto* destination = static_cast<uint16_t*>(
                                  input_ptrs[input_index.at(record.destination)]) +
                              record.destination_offset;
          if (source != destination) {
            for (size_t row = 0; row < record.rows; ++row) {
              std::memcpy(destination + row * record.destination_stride,
                          source + row * record.source_stride,
                          record.row_elements * sizeof(uint16_t));
            }
          }
        } else if (record.operation == "requantize_uint16") {
          const auto* source = static_cast<const uint16_t*>(
                                   output_ptrs[output_index.at(record.source)]) +
                               record.source_offset;
          auto* destination = static_cast<uint16_t*>(
                                  input_ptrs[input_index.at(record.destination)]) +
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
            if (lut.size() != 65'536) {
              throw std::runtime_error("nonidentity UINT16 LUT absent during execution");
            }
            for (size_t row = 0; row < record.rows; ++row) {
              const auto* source_row = source + row * record.source_stride;
              auto* destination_row = destination + row * record.destination_stride;
              size_t element = 0;
              if (exact_neon_requantize[advance_index]) {
                for (; element + 8 <= record.row_elements; element += 8) {
                  RequantizeNeon8(source_row + element, destination_row + element, record);
                }
              }
              for (; element < record.row_elements; ++element) {
                destination_row[element] = lut[source_row[element]];
              }
            }
          }
        } else {
          throw std::runtime_error("unknown advance operation during execution");
        }
      }
      const double advance_elapsed = Seconds(advance_begin, Clock::now());
      const double e2e_elapsed = Seconds(e2e_begin, Clock::now());

      bool step_finite = true;
      for (size_t output = 0; output < kExpectedOutputs && step_finite; ++output) {
        if (output_types[output] != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) continue;
        const size_t count = ShapeElements(output_shapes[output]);
        const float* pointer = static_cast<const float*>(output_ptrs[output]);
        for (size_t index = 0; index < count; ++index) {
          if (!std::isfinite(pointer[index])) {
            step_finite = false;
            break;
          }
        }
      }
      all_finite = all_finite && step_finite;
      const bool matches = static_cast<int64_t>(best) == reference[step].device_top1;
      const bool pc_match = static_cast<int64_t>(best) == reference[step].pc_top1;
      const bool official_match = static_cast<int64_t>(best) == reference[step].official_top1;
      reference_matches += matches ? 1 : 0;
      pc_matches += pc_match ? 1 : 0;
      official_matches += official_match ? 1 : 0;
      actual_top1.push_back(static_cast<int64_t>(best));
      run_seconds.push_back(run_elapsed);
      advance_seconds.push_back(advance_elapsed);
      e2e_seconds.push_back(e2e_elapsed);
      timing_stream << (step + 1) << '\t' << best << '\t' << reference[step].device_top1 << '\t'
                    << reference[step].pc_top1 << '\t' << reference[step].official_top1 << '\t'
                    << (matches ? 1 : 0) << '\t' << (pc_match ? 1 : 0) << '\t'
                    << (official_match ? 1 : 0) << '\t' << (step_finite ? 1 : 0) << '\t'
                    << std::setprecision(17) << run_elapsed << '\t' << advance_elapsed << '\t'
                    << e2e_elapsed << '\n';
    }
    timing_stream.close();

    phase = "profiling_close";
    auto profile_path_allocated = session.EndProfilingAllocated(default_allocator);
    const std::string profile_path = profile_path_allocated.get();
    const auto run_summary = Summarize(run_seconds, kWarmup);
    const auto advance_summary = Summarize(advance_seconds, kWarmup);
    const auto e2e_summary = Summarize(e2e_seconds, kWarmup);
    const bool first_step_matches = !actual_top1.empty() &&
                                    actual_top1.front() == reference.front().device_top1;
    const size_t uint16_inputs = std::count(input_types.begin(), input_types.end(),
                                            ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16);
    const size_t uint16_outputs = std::count(output_types.begin(), output_types.end(),
                                             ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16);
    const bool pass = all_finite && first_step_matches && uint16_inputs == 556 &&
                      uint16_outputs == 556 && identity_requantize_records == 301 &&
                      identity_requantize_elements == 2'626'560 &&
                      nonidentity_lut_records == 58 && nonidentity_lut_bytes == 7'602'176 &&
                      neon_exact_nonidentity_records == 58 &&
                      neon_exact_nonidentity_elements == 571'392 &&
                      fs::is_regular_file(profile_path);

    phase = "result_write";
    std::ofstream result_stream(args.result);
    result_stream << std::setprecision(17)
                  << "{\n"
                  << "  \"schema\": \"qnn_qnn_htp_shared_compact_kv_neon_cxx_v10\",\n"
                  << "  \"status\": \"" << (pass ? "execution_pass" : "execution_fail") << "\",\n"
                  << "  \"allocator\": \"QnnHtpShared\",\n"
                  << "  \"allocator_info\": {\n"
                  << "    \"name\": \"" << JsonEscape(allocator_info.GetAllocatorName()) << "\",\n"
                  << "    \"device_id\": " << allocator_info.GetDeviceId() << ",\n"
                  << "    \"device_type\": " << static_cast<int>(allocator_info.GetDeviceType()) << ",\n"
                  << "    \"device_memory_type\": " << static_cast<int>(allocator_info.GetDeviceMemoryType()) << ",\n"
                  << "    \"memory_type\": " << static_cast<int>(allocator_info.GetMemoryType()) << "\n"
                  << "  },\n"
                  << "  \"boot_id\": \"" << JsonEscape(boot_id) << "\",\n"
                  << "  \"model\": \"" << JsonEscape(args.model.string()) << "\",\n"
                  << "  \"plugin\": \"" << JsonEscape(args.plugin.string()) << "\",\n"
                  << "  \"backend\": \"" << JsonEscape(args.backend.string()) << "\",\n"
                  << "  \"fallback_disabled\": true,\n"
                  << "  \"profile_mode\": \"off\",\n"
                  << "  \"graph_io_qdq_offload_to_cpu\": false,\n"
                  << "  \"session_create_seconds\": " << session_create_seconds << ",\n"
                  << "  \"qnn_ep_devices\": " << qnn_devices.size() << ",\n"
                  << "  \"input_tensors\": " << kExpectedInputs << ",\n"
                  << "  \"output_tensors\": " << kExpectedOutputs << ",\n"
                  << "  \"uint16_input_tensors\": " << uint16_inputs << ",\n"
                  << "  \"uint16_output_tensors\": " << uint16_outputs << ",\n"
                  << "  \"shared_allocation_count\": "
                  << (input_allocations.size() + output_allocations.size()) << ",\n"
                  << "  \"aliased_uint16_pairs\": " << uint16_output_to_input.size() << ",\n"
                  << "  \"aliased_recurrent_state_pairs\": " << aliased_state_pairs << ",\n"
                  << "  \"aliased_compact_kv_pairs\": " << aliased_kv_pairs << ",\n"
                  << "  \"aliased_compact_kv_bytes_per_step\": 25153536,\n"
                  << "  \"input_bytes\": " << input_bytes << ",\n"
                  << "  \"output_bytes\": " << output_bytes << ",\n"
                  << "  \"advance_records\": " << advance_plan.size() << ",\n"
                  << "  \"advance_bytes_per_step\": " << planned_advance_bytes << ",\n"
                  << "  \"total_steps\": " << kSteps << ",\n"
                  << "  \"warmup_steps\": " << kWarmup << ",\n"
                  << "  \"reference_top1_matches\": " << reference_matches << ",\n"
                  << "  \"pc_top1_matches\": " << pc_matches << ",\n"
                  << "  \"official_top1_matches\": " << official_matches << ",\n"
                  << "  \"state_copy_records\": " << state_copy_records << ",\n"
                  << "  \"state_requantize_records\": " << state_requantize_records << ",\n"
                  << "  \"kv_copy_records\": " << kv_copy_records << ",\n"
                  << "  \"kv_requantize_records\": " << kv_requantize_records << ",\n"
                  << "  \"identity_requantize_proof_domain\": 65536,\n"
                  << "  \"identity_requantize_records\": " << identity_requantize_records << ",\n"
                  << "  \"identity_requantize_elements\": " << identity_requantize_elements << ",\n"
                  << "  \"state_identity_requantize_records\": "
                  << state_identity_requantize_records << ",\n"
                  << "  \"kv_identity_requantize_records\": "
                  << kv_identity_requantize_records << ",\n"
                  << "  \"nonidentity_requantize_lut_records\": "
                  << nonidentity_lut_records << ",\n"
                  << "  \"nonidentity_requantize_lut_bytes\": "
                  << nonidentity_lut_bytes << ",\n"
                  << "  \"neon_exact_nonidentity_records\": "
                  << neon_exact_nonidentity_records << ",\n"
                  << "  \"neon_exact_nonidentity_elements\": "
                  << neon_exact_nonidentity_elements << ",\n"
                  << "  \"neon_width\": 8,\n"
                  << "  \"all_outputs_finite\": " << (all_finite ? "true" : "false") << ",\n"
                  << "  \"timing\": {\n"
                  << "    \"measured_run_seconds\": ";
    WriteSummary(result_stream, run_summary, 4);
    result_stream << ",\n    \"measured_advance_seconds\": ";
    WriteSummary(result_stream, advance_summary, 4);
    result_stream << ",\n    \"measured_end_to_end_excluding_validation_seconds\": ";
    WriteSummary(result_stream, e2e_summary, 4);
    result_stream << ",\n"
                  << "    \"run_tokens_per_second_from_median\": " << (1.0 / run_summary.median) << ",\n"
                  << "    \"end_to_end_tokens_per_second_from_median\": " << (1.0 / e2e_summary.median) << "\n"
                  << "  },\n"
                  << "  \"timing_tsv\": \"" << JsonEscape(timing_path.string()) << "\",\n"
                  << "  \"ort_profile\": \"" << JsonEscape(profile_path) << "\",\n"
                  << "  \"gates\": {\n"
                  << "    \"allocator_name_qnn_htp_shared\": true,\n"
                  << "    \"fallback_disabled\": true,\n"
                  << "    \"profile_mode_off\": true,\n"
                  << "    \"input_count_578\": true,\n"
                  << "    \"output_count_575\": true,\n"
                  << "    \"uint16_input_output_count_556\": "
                  << (uint16_inputs == 556 && uint16_outputs == 556 ? "true" : "false") << ",\n"
                  << "    \"shared_allocation_count_597\": "
                  << (input_allocations.size() + output_allocations.size() == 597 ? "true" : "false") << ",\n"
                  << "    \"aliased_uint16_pairs_556_state544_kv12\": "
                  << (uint16_output_to_input.size() == 556 && aliased_state_pairs == 544 &&
                              aliased_kv_pairs == 12
                          ? "true"
                          : "false")
                  << ",\n"
                  << "    \"advance_records_574\": true,\n"
                  << "    \"teacher_steps_64\": true,\n"
                  << "    \"first_step_matches_frozen_r15\": "
                  << (first_step_matches ? "true" : "false") << ",\n"
                  << "    \"state_copy185_requantize359\": "
                  << (state_copy_records == 185 && state_requantize_records == 359 ? "true" : "false")
                  << ",\n"
                  << "    \"kv_copy12_requantize0\": "
                  << (kv_copy_records == 12 && kv_requantize_records == 0 ? "true" : "false")
                  << ",\n"
                  << "    \"identity_requantize_full_uint16_domain_301_records_2626560_elements\": "
                  << (identity_requantize_records == 301 &&
                              identity_requantize_elements == 2'626'560 &&
                              state_identity_requantize_records == 301 &&
                              kv_identity_requantize_records == 0
                          ? "true"
                          : "false")
                  << ",\n"
                  << "    \"nonidentity_requantize_exact_lut_58_records_7602176_bytes\": "
                  << (nonidentity_lut_records == 58 && nonidentity_lut_bytes == 7'602'176
                          ? "true"
                          : "false")
                  << ",\n"
                  << "    \"neon_exact_all_58_nonidentity_records_571392_elements\": "
                  << (neon_exact_nonidentity_records == 58 &&
                              neon_exact_nonidentity_elements == 571'392
                          ? "true"
                          : "false")
                  << ",\n"
                  << "    \"host_rounding_mode_nearest_even\": true,\n"
                  << "    \"all_outputs_finite\": " << (all_finite ? "true" : "false") << ",\n"
                  << "    \"ort_profile_present\": " << (fs::is_regular_file(profile_path) ? "true" : "false") << "\n"
                  << "  },\n"
                  << "  \"exception_phase\": null\n"
                  << "}\n";
    result_stream.close();
    std::cout << "{\"status\":\"" << (pass ? "execution_pass" : "execution_fail")
              << "\",\"boot_id\":\"" << JsonEscape(boot_id)
              << "\",\"reference_matches\":" << reference_matches
              << ",\"pc_matches\":" << pc_matches
              << ",\"official_matches\":" << official_matches
              << ",\"run_median_seconds\":" << run_summary.median
              << ",\"run_tokens_per_second\":" << (1.0 / run_summary.median)
              << ",\"end_to_end_tokens_per_second\":" << (1.0 / e2e_summary.median) << "}\n";
    return pass ? 0 : 2;
  } catch (const Ort::Exception& error) {
    if (!args.result.empty()) WriteFailure(args, phase, error.what(), boot_id);
    std::cerr << "ORT exception in " << phase << ": " << error.what() << '\n';
    return phase.find("allocator") != std::string::npos ? 3 : 2;
  } catch (const std::exception& error) {
    if (!args.result.empty()) WriteFailure(args, phase, error.what(), boot_id);
    std::cerr << "exception in " << phase << ": " << error.what() << '\n';
    return phase.find("allocator") != std::string::npos ? 3 : 2;
  }
}
