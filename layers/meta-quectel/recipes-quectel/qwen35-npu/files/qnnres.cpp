// Resident QNN executor for the Qwen3.5-0.8B HTP context.
//
// Why this exists: qnn-net-run reloads the whole 764 MB context before every
// token, converts every tensor through float32, and writes every output to a
// file.  On this board that costs far more than the graph itself -- measured
// 1337 ms/step through qnn-net-run versus 133 ms/token here.  This loads the
// context once and keeps every tensor in process memory.
//
// Two builds from one file:
//   shared library (used by qwen35-rt.py):
//     g++ -O2 -std=c++17 -shared -fPIC -o libqnnres.so qnnres.cpp -I<QNN> -ldl
//   benchmark binary:
//     g++ -O2 -std=c++17 -DQNNRES_MAIN -o qnnres qnnres.cpp -I<QNN> -ldl
//     ./qnnres --ctx kvc.bin --steps 20 [--init dir/s000]
//
// The C API takes and returns raw tensor pointers, so the caller can memmove
// straight into a graph buffer (no file, no conversion).

#include <dlfcn.h>
#include <time.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>

#include "QnnInterface.h"
#include "QnnContext.h"
#include "QnnGraph.h"
#include "QnnTensor.h"
#include "QnnTypes.h"
#include "QnnCommon.h"
#include "System/QnnSystemInterface.h"
#include "System/QnnSystemContext.h"

#define LOG(...)                                       \
  do {                                                 \
    if (g_verbose) {                                   \
      fprintf(stderr, "qnnres: " __VA_ARGS__);         \
      fprintf(stderr, "\n");                           \
    }                                                  \
  } while (0)

static bool g_verbose = false;

static double now_ms() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void log_cb(const char* fmt, QnnLog_Level_t level, uint64_t ts, va_list ap) {
  static const char* name[] = {"ERROR", "WARN", "INFO", "VERBOSE", "DEBUG"};
  int i = (int)level - 1;
  fprintf(stderr, "[%s] ", (i >= 0 && i < 5) ? name[i] : "LOG");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
}

static size_t dtype_size(Qnn_DataType_t t) {
  switch (t) {
    case QNN_DATATYPE_INT_8:
    case QNN_DATATYPE_UINT_8:
    case QNN_DATATYPE_BOOL_8:
    case QNN_DATATYPE_SFIXED_POINT_8:
    case QNN_DATATYPE_UFIXED_POINT_8:
      return 1;
    case QNN_DATATYPE_INT_16:
    case QNN_DATATYPE_UINT_16:
    case QNN_DATATYPE_FLOAT_16:
    case QNN_DATATYPE_SFIXED_POINT_16:
    case QNN_DATATYPE_UFIXED_POINT_16:
      return 2;
    case QNN_DATATYPE_INT_32:
    case QNN_DATATYPE_UINT_32:
    case QNN_DATATYPE_FLOAT_32:
    case QNN_DATATYPE_SFIXED_POINT_32:
    case QNN_DATATYPE_UFIXED_POINT_32:
      return 4;
    case QNN_DATATYPE_INT_64:
    case QNN_DATATYPE_UINT_64:
    case QNN_DATATYPE_FLOAT_64:
      return 8;
    default:
      return 0;
  }
}

struct Tensor {
  std::string name;
  std::vector<uint32_t> dims;
  Qnn_DataType_t dtype;
  size_t bytes = 0;
  void* buf = nullptr;
  Qnn_Tensor_t desc;
};

// name_out -> name_in, plus an optional moveaxis(1,-1) when the two sides
// disagree about layout (conv_state is emitted in ONNX order but consumed
// natively).
struct Carry {
  Tensor* src;
  Tensor* dst;
  bool transpose;
};

struct Engine {
  void* sys_lib = nullptr;
  void* htp_lib = nullptr;
  QNN_SYSTEM_INTERFACE_VER_TYPE sys;
  QNN_INTERFACE_VER_TYPE qnn;
  Qnn_LogHandle_t logger = nullptr;
  Qnn_BackendHandle_t backend = nullptr;
  Qnn_DeviceHandle_t device = nullptr;
  Qnn_ContextHandle_t context = nullptr;
  Qnn_GraphHandle_t graph = nullptr;
  QnnSystemContext_Handle_t sys_ctx = nullptr;
  std::vector<uint8_t> blob;
  std::vector<Tensor> ins, outs;
  std::vector<Carry> carries;
  std::vector<Qnn_Tensor_t> in_desc, out_desc;
  double load_ms = 0, last_ms = 0, total_ms = 0;
  long steps = 0;
};

static void* aligned_buf(size_t bytes) {
  size_t a = (bytes + 4095) & ~(size_t)4095;
  if (!a) a = 4096;
  void* p = nullptr;
  if (posix_memalign(&p, 4096, a) != 0) return nullptr;
  memset(p, 0, a);
  return p;
}

static bool load_file(const char* path, std::vector<uint8_t>& out) {
  FILE* f = fopen(path, "rb");
  if (!f) { perror(path); return false; }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  out.resize(n);
  bool ok = fread(out.data(), 1, n, f) == (size_t)n;
  fclose(f);
  return ok;
}

static void do_carry(const Carry& c) {
  if (!c.transpose) {
    memcpy(c.dst->buf, c.src->buf, std::min(c.src->bytes, c.dst->bytes));
    return;
  }
  size_t d1 = c.src->dims.size() >= 3 ? c.src->dims[1] : 1;
  size_t d2 = c.src->dims.size() >= 3 ? c.src->dims[2] : 1;
  size_t es = dtype_size(c.src->dtype);
  if (d1 * d2 * es != c.src->bytes || d1 * d2 * es != c.dst->bytes) {
    memcpy(c.dst->buf, c.src->buf, std::min(c.src->bytes, c.dst->bytes));
    return;
  }
  const char* s = (const char*)c.src->buf;
  char* d = (char*)c.dst->buf;
  for (size_t j = 0; j < d1; ++j)
    for (size_t k = 0; k < d2; ++k) memcpy(d + (k * d1 + j) * es, s + (j * d2 + k) * es, es);
}

static Engine* open_engine(const char* ctx_path, const char* libdir, int log_level,
                           const char* graph_name) {
  Engine* e = new Engine();
  if (!load_file(ctx_path, e->blob)) { delete e; return nullptr; }

  std::string sys_path = std::string(libdir) + "/libQnnSystem.so";
  e->sys_lib = dlopen(sys_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!e->sys_lib) { fprintf(stderr, "qnnres: %s\n", dlerror()); delete e; return nullptr; }
  auto sys_get = (Qnn_ErrorHandle_t(*)(const QnnSystemInterface_t***, uint32_t*))
      dlsym(e->sys_lib, "QnnSystemInterface_getProviders");
  const QnnSystemInterface_t** sp = nullptr;
  uint32_t nsp = 0;
  if (!sys_get || sys_get(&sp, &nsp) != QNN_SUCCESS || !nsp) {
    fprintf(stderr, "qnnres: system interface unavailable\n");
    delete e;
    return nullptr;
  }
  e->sys = sp[0]->QNN_SYSTEM_INTERFACE_VER_NAME;
  e->sys.systemContextCreate(&e->sys_ctx);
  const QnnSystemContext_BinaryInfo_t* bi = nullptr;
  Qnn_ContextBinarySize_t bi_size = 0;
  if (e->sys.systemContextGetBinaryInfo(e->sys_ctx, e->blob.data(), e->blob.size(), &bi,
                                        &bi_size) != QNN_SUCCESS) {
    fprintf(stderr, "qnnres: cannot read context binary info\n");
    delete e;
    return nullptr;
  }

  uint32_t num_graphs = 0;
  QnnSystemContext_GraphInfo_t* graphs = nullptr;
  if (bi->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_1) {
    num_graphs = bi->contextBinaryInfoV1.numGraphs;
    graphs = bi->contextBinaryInfoV1.graphs;
  } else if (bi->version == QNN_SYSTEM_CONTEXT_BINARY_INFO_VERSION_2) {
    num_graphs = bi->contextBinaryInfoV2.numGraphs;
    graphs = bi->contextBinaryInfoV2.graphs;
  } else {
    num_graphs = bi->contextBinaryInfoV3.numGraphs;
    graphs = bi->contextBinaryInfoV3.graphs;
  }
  if (!num_graphs) { fprintf(stderr, "qnnres: no graphs in context\n"); delete e; return nullptr; }

  const QnnSystemContext_GraphInfoV1_t* g = &graphs[0].graphInfoV1;
  if (graph_name && *graph_name) {
    for (uint32_t i = 0; i < num_graphs; ++i)
      if (strcmp(graphs[i].graphInfoV1.graphName, graph_name) == 0) {
        g = &graphs[i].graphInfoV1;
        break;
      }
  }
  LOG("graph \"%s\": %u inputs, %u outputs", g->graphName, g->numGraphInputs, g->numGraphOutputs);

  auto build = [&](Tensor& t, Qnn_Tensor_t& src) {
    // v2 shares the leading members with v1, so a v1 view works for both.
    const Qnn_TensorV1_t& v = *reinterpret_cast<const Qnn_TensorV1_t*>(&src.v1);
    t.name = v.name ? v.name : "";
    t.dims.assign(v.dimensions, v.dimensions + v.rank);
    t.dtype = v.dataType;
    size_t n = dtype_size(t.dtype);
    for (auto d : t.dims) n *= d;
    t.bytes = n;
    t.buf = aligned_buf(n);
    t.desc = src;  // keep id/name/dims/quantisation, only attach a buffer
    t.desc.v2.memType = QNN_TENSORMEMTYPE_RAW;
    t.desc.v2.clientBuf.data = t.buf;
    t.desc.v2.clientBuf.dataSize = (uint32_t)n;
  };
  e->ins.resize(g->numGraphInputs);
  e->outs.resize(g->numGraphOutputs);
  for (uint32_t i = 0; i < g->numGraphInputs; ++i) build(e->ins[i], g->graphInputs[i]);
  for (uint32_t i = 0; i < g->numGraphOutputs; ++i) build(e->outs[i], g->graphOutputs[i]);

  for (auto& o : e->outs) {
    if (o.name.size() < 4 || o.name.compare(o.name.size() - 4, 4, "_out") != 0) continue;
    std::string want = o.name.substr(0, o.name.size() - 4) + "_in";
    for (auto& in : e->ins) {
      if (in.name != want) continue;
      Carry c{&o, &in, false};
      if (o.dims != in.dims) {
        if (o.dims.size() == 3 && in.dims.size() == 3 && o.dims[0] == in.dims[0] &&
            o.dims[1] == in.dims[2] && o.dims[2] == in.dims[1])
          c.transpose = true;
        else
          fprintf(stderr, "qnnres: layout mismatch %s\n", in.name.c_str());
      }
      e->carries.push_back(c);
      break;
    }
  }

  std::string htp_path = std::string(libdir) + "/libQnnHtp.so";
  e->htp_lib = dlopen(htp_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!e->htp_lib) { fprintf(stderr, "qnnres: %s\n", dlerror()); delete e; return nullptr; }
  auto get_iface = (Qnn_ErrorHandle_t(*)(const QnnInterface_t***, uint32_t*))
      dlsym(e->htp_lib, "QnnInterface_getProviders");
  const QnnInterface_t** ip = nullptr;
  uint32_t nip = 0;
  if (!get_iface || get_iface(&ip, &nip) != QNN_SUCCESS || !nip) {
    fprintf(stderr, "qnnres: backend interface unavailable\n");
    delete e;
    return nullptr;
  }
  e->qnn = ip[0]->QNN_INTERFACE_VER_NAME;

  e->qnn.logCreate(log_cb, (QnnLog_Level_t)log_level, &e->logger);
  if (e->qnn.backendCreate(e->logger, nullptr, &e->backend) != QNN_SUCCESS) {
    fprintf(stderr, "qnnres: backendCreate failed\n");
    delete e;
    return nullptr;
  }
  if (e->qnn.deviceCreate(e->logger, nullptr, &e->device) != QNN_SUCCESS) {
    fprintf(stderr, "qnnres: deviceCreate failed\n");
    delete e;
    return nullptr;
  }
  double t0 = now_ms();
  if (e->qnn.contextCreateFromBinary(e->backend, e->device, nullptr, e->blob.data(),
                                     e->blob.size(), &e->context, nullptr) != QNN_SUCCESS) {
    fprintf(stderr, "qnnres: contextCreateFromBinary failed\n");
    delete e;
    return nullptr;
  }
  if (e->qnn.graphRetrieve(e->context, g->graphName, &e->graph) != QNN_SUCCESS) {
    fprintf(stderr, "qnnres: graphRetrieve failed\n");
    delete e;
    return nullptr;
  }
  e->load_ms = now_ms() - t0;
  // A graph retrieved from a serialized context is already finalized; the
  // backend answers 0x3e8 ("finalized already") to a second call.
  e->qnn.graphFinalize(e->graph, nullptr, nullptr);

  for (auto& t : e->ins) e->in_desc.push_back(t.desc);
  for (auto& t : e->outs) e->out_desc.push_back(t.desc);
  LOG("loaded in %.0f ms", e->load_ms);
  return e;
}

static int exec(Engine* e) {
  double a = now_ms();
  Qnn_ErrorHandle_t r = e->qnn.graphExecute(e->graph, e->in_desc.data(),
                                            (uint32_t)e->in_desc.size(), e->out_desc.data(),
                                            (uint32_t)e->out_desc.size(), nullptr, nullptr);
  e->last_ms = now_ms() - a;
  e->total_ms += e->last_ms;
  e->steps++;
  if (r != QNN_SUCCESS) {
    fprintf(stderr, "qnnres: graphExecute failed 0x%llx\n", (unsigned long long)r);
    return 0;
  }
  return 1;
}

static int lookup(const std::vector<Tensor>& v, const char* name) {
  if (!name) return -1;
  for (size_t i = 0; i < v.size(); ++i)
    if (v[i].name == name) return (int)i;
  return -1;
}

// ------------------------------------------------------------------ C API
extern "C" {

void* qres_open(const char* ctx, const char* libdir) {
  return open_engine(ctx, libdir ? libdir : "/usr/lib", QNN_LOG_LEVEL_ERROR, nullptr);
}
void* qres_open_verbose(const char* ctx, const char* libdir) {
  return open_engine(ctx, libdir ? libdir : "/usr/lib", QNN_LOG_LEVEL_INFO, nullptr);
}
int qres_num_in(void* h) { return h ? (int)((Engine*)h)->ins.size() : -1; }
int qres_num_out(void* h) { return h ? (int)((Engine*)h)->outs.size() : -1; }
const char* qres_in_name(void* h, int i) { return ((Engine*)h)->ins[i].name.c_str(); }
const char* qres_out_name(void* h, int i) { return ((Engine*)h)->outs[i].name.c_str(); }
unsigned long long qres_in_bytes(void* h, int i) { return ((Engine*)h)->ins[i].bytes; }
unsigned long long qres_out_bytes(void* h, int i) { return ((Engine*)h)->outs[i].bytes; }
const int* qres_in_dims(void* h, int i) {
  static thread_local std::vector<int> d;
  d.clear();
  for (auto v : ((Engine*)h)->ins[i].dims) d.push_back((int)v);
  return d.data();
}
int qres_in_rank(void* h, int i) { return (int)((Engine*)h)->ins[i].dims.size(); }
void* qres_in_ptr(void* h, int i) { return ((Engine*)h)->ins[i].buf; }
void* qres_out_ptr(void* h, int i) { return ((Engine*)h)->outs[i].buf; }
int qres_in_lookup(void* h, const char* n) { return lookup(((Engine*)h)->ins, n); }
int qres_out_lookup(void* h, const char* n) { return lookup(((Engine*)h)->outs, n); }
int qres_carry(void* h) {
  for (auto& c : ((Engine*)h)->carries) do_carry(c);
  return (int)((Engine*)h)->carries.size();
}
int qres_exec(void* h) { return exec((Engine*)h); }
double qres_load_ms(void* h) { return ((Engine*)h)->load_ms; }
double qres_last_ms(void* h) { return ((Engine*)h)->last_ms; }
double qres_mean_ms(void* h) {
  Engine* e = (Engine*)h;
  return e->steps ? e->total_ms / e->steps : 0;
}
void qres_reset_stats(void* h) { ((Engine*)h)->total_ms = 0; ((Engine*)h)->steps = 0; }
void qres_close(void* h) {
  Engine* e = (Engine*)h;
  if (!e) return;
  if (e->qnn.contextFree && e->context) e->qnn.contextFree(e->context, nullptr);
  if (e->qnn.deviceFree && e->device) e->qnn.deviceFree(e->device);
  if (e->qnn.backendFree && e->backend) e->qnn.backendFree(e->backend);
  if (e->qnn.logFree && e->logger) e->qnn.logFree(e->logger);
  delete e;
}

// Convenience for one-shot experiments: feed a directory of <tensor>.raw
// files, run N steps, print timings.
int qres_bench(const char* ctx, const char* libdir, const char* init_dir, int steps) {
  void* h = qres_open(ctx, libdir);
  if (!h) return 1;
  Engine* e = (Engine*)h;
  printf("context %s: %zu bytes, %zu inputs (%.1f MB) / %zu outputs (%.1f MB)\n", ctx,
         e->blob.size(), e->ins.size(), [&] {
           size_t s = 0; for (auto& t : e->ins) s += t.bytes; return s / 1e6;
         }(), e->outs.size(), [&] {
           size_t s = 0; for (auto& t : e->outs) s += t.bytes; return s / 1e6;
         }());
  printf("%zu state pairs, context load %.0f ms\n", e->carries.size(), e->load_ms);
  if (init_dir) {
    for (auto& t : e->ins) {
      std::string p = std::string(init_dir) + "/" + t.name + ".raw";
      FILE* f = fopen(p.c_str(), "rb");
      if (!f) continue;
      size_t n = fread(t.buf, 1, t.bytes, f);
      fclose(f);
      if (n != t.bytes) fprintf(stderr, "qnnres: short read %s\n", p.c_str());
    }
  }
  if (!exec(e)) return 1;
  printf("first execute %.0f ms\n", e->last_ms);
  qres_reset_stats(h);
  for (int i = 0; i < steps; ++i) {
    qres_carry(h);
    if (!exec(e)) return 1;
  }
  printf("steps %d: mean %.1f ms/token  => %.2f tok/s\n", steps, qres_mean_ms(h),
         1000.0 / qres_mean_ms(h));
  qres_close(h);
  return 0;
}

}  // extern "C"

#ifdef QNNRES_MAIN
int main(int argc, char** argv) {
  const char* ctx = nullptr;
  const char* libdir = "/usr/lib";
  const char* init = nullptr;
  int steps = 20;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--ctx") && i + 1 < argc) ctx = argv[++i];
    else if (!strcmp(argv[i], "--libdir") && i + 1 < argc) libdir = argv[++i];
    else if (!strcmp(argv[i], "--init") && i + 1 < argc) init = argv[++i];
    else if (!strcmp(argv[i], "--steps") && i + 1 < argc) steps = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--verbose")) g_verbose = true;
    else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
  }
  if (!ctx) { fprintf(stderr, "usage: qnnres --ctx <bin> [--steps N] [--init dir]\n"); return 2; }
  return qres_bench(ctx, libdir, init, steps);
}
#endif
