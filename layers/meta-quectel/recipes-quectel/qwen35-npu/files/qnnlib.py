#!/usr/bin/env python3
"""qnnlib.py -- minimal ctypes binding to the QNN 2.45 C API.

Why a binding instead of a compiled helper: the board ships libQnnHtp.so and
libQnnSystem.so but not the QNN headers, so anything C++ has to be
cross-compiled.  A step costs exactly one graphExecute call, and keeping the
runner in Python means the package installs as plain files and can be fixed on
the board without a rebuild.

The structures below mirror QnnTypes.h / QnnInterface.h / QnnSystemContext.h
exactly; their sizes are asserted against the C++ `sizeof`
(Qnn_QuantizeParams_t=40, Qnn_TensorV1_t=112, Qnn_TensorV2_t=136) so a mismatch
fails loudly instead of silently feeding the DSP garbage.

Typical use:

    eng = Engine("/data/local/tmp/kvc.bin")
    eng.view_in("hidden_in")[...] = codes     # numpy view of the graph buffer
    eng.exec()                                # one graphExecute
    eng.carry()                               # output -> input, in place
    eng.close()
"""
import ctypes
import json
import os
import sys
import time

import numpy as np

QNN_SUCCESS = 0
QNN_TENSOR_VERSION_1 = 1
QNN_TENSOR_VERSION_2 = 2
QNN_TENSORMEMTYPE_RAW = 0
QNN_QUANTIZATION_ENCODING_SCALE_OFFSET = 1

U32 = ctypes.c_uint32
I32 = ctypes.c_int32
U64 = ctypes.c_uint64
VP = ctypes.c_void_p


LOG_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_char_p, U32, U64, VP)


class Version(ctypes.Structure):
    _fields_ = [("major", U32), ("minor", U32), ("patch", U32)]


class ApiVersion(ctypes.Structure):
    # two versions, not one: iface starts at offset 40, not 32
    _fields_ = [("coreApiVersion", Version),
                ("backendApiVersion", Version)]


class ScaleOffset(ctypes.Structure):
    _fields_ = [("scale", ctypes.c_float), ("offset", I32)]


class BwAxisScaleOffset(ctypes.Structure):
    _fields_ = [("bitwidth", U32), ("axis", I32), ("numElements", U32),
                ("scales", VP), ("offsets", VP)]


class _QuantUnion(ctypes.Union):
    _fields_ = [("scaleOffset", ScaleOffset),
                ("bwAxisScaleOffset", BwAxisScaleOffset),
                ("ptr", VP)]


class QuantizeParams(ctypes.Structure):
    _fields_ = [("definition", I32), ("encoding", I32), ("u", _QuantUnion)]


class ClientBuffer(ctypes.Structure):
    _fields_ = [("data", VP), ("dataSize", U32)]


class _TensorUnion(ctypes.Union):
    _fields_ = [("clientBuf", ClientBuffer), ("memHandle", VP)]


class TensorV1(ctypes.Structure):
    _fields_ = [("id", U32), ("name", ctypes.c_char_p), ("type", I32),
                ("dataFormat", I32), ("dataType", I32),
                ("quantizeParams", QuantizeParams), ("rank", U32),
                ("dimensions", ctypes.POINTER(U32)), ("memType", I32),
                ("u", _TensorUnion)]


class SparseParams(ctypes.Structure):
    _fields_ = [("type", I32), ("hybridCoo", U32 * 2)]


class TensorV2(ctypes.Structure):
    _fields_ = [("id", U32), ("name", ctypes.c_char_p), ("type", I32),
                ("dataFormat", I32), ("dataType", I32),
                ("quantizeParams", QuantizeParams), ("rank", U32),
                ("dimensions", ctypes.POINTER(U32)), ("memType", I32),
                ("u", _TensorUnion), ("isDynamicDimensions", VP),
                ("sparseParams", SparseParams), ("isProduced", ctypes.c_uint8)]


class _TensorBody(ctypes.Union):
    _fields_ = [("v1", TensorV1), ("v2", TensorV2)]


class Tensor(ctypes.Structure):
    _fields_ = [("version", I32), ("u", _TensorBody)]


IFACE_FIELDS = [
    "propertyHasCapability", "backendCreate", "backendSetConfig",
    "backendGetApiVersion", "backendGetBuildId", "backendRegisterOpPackage",
    "backendGetSupportedOperations", "backendValidateOpConfig", "backendFree",
    "contextCreate", "contextSetConfig", "contextGetBinarySize",
    "contextGetBinary", "contextCreateFromBinary", "contextFree",
    "graphCreate", "graphCreateSubgraph", "graphSetConfig", "graphAddNode",
    "graphFinalize", "graphRetrieve", "graphExecute", "graphExecuteAsync",
    "tensorCreateContextTensor", "tensorCreateGraphTensor", "logCreate",
    "logSetLogLevel", "logFree", "profileCreate", "profileSetConfig",
    "profileGetEvents", "profileGetSubEvents", "profileGetEventData",
    "profileGetExtendedEventData", "profileFree", "memRegister",
    "memDeRegister", "deviceGetPlatformInfo", "deviceFreePlatformInfo",
    "deviceGetInfrastructure", "deviceCreate", "deviceSetConfig",
    "deviceGetInfo", "deviceFree", "signalCreate", "signalSetConfig",
    "signalTrigger", "signalFree", "errorGetMessage",
    "errorGetVerboseMessage", "errorFreeVerboseMessage",
    "graphPrepareExecutionEnvironment", "graphReleaseExecutionEnvironment",
    "graphGetProperty", "contextValidateBinary",
    "contextCreateFromBinaryWithSignal", "contextCreateFromBinaryListAsync",
    "tensorUpdateGraphTensors", "tensorUpdateContextTensors",
    "contextGetBinarySectionSize", "contextGetBinarySection",
    "contextApplyBinarySection", "backendGetProperty", "contextGetProperty",
    "contextGetIncrementalBinary", "contextReleaseIncrementalBinary",
    "contextFinalize", "globalConfigSet",
    "contextCreateFromBinaryWithCallback", "contextGetBinarySectionUpdate",
    "contextFreeBinarySectionUpdate", "contextAddToDlc",
]

SYS_FIELDS = [
    "systemContextCreate", "systemContextGetBinaryInfo",
    "systemContextGetMetaData", "systemContextFree",
    "systemTensorGetMemoryFootprint", "systemLogCreate",
    "systemLogSetLogLevel", "systemLogFree", "systemDlcCreateFromFile",
    "systemDlcCreateFromBinary", "systemDlcComposeGraphs",
    "systemDlcGetOpMappings", "systemDlcFree",
    "systemProfileCreateSerializationTarget",
    "systemProfileSerializeEventData", "systemProfileFreeSerializationTarget",
    "systemDlcGetRecordByName", "systemDlcGetRecordsByType",
    "systemDlcGetRecordDataSize", "systemDlcReadRecordDataMemoryMapped",
    "systemDlcFreeRecord", "systemDlcSave",
]


class _Interface(ctypes.Structure):
    _fields_ = [(n, VP) for n in IFACE_FIELDS]


class _SysInterface(ctypes.Structure):
    _fields_ = [(n, VP) for n in SYS_FIELDS]


class InterfaceT(ctypes.Structure):
    _fields_ = [("backendId", U32), ("providerName", ctypes.c_char_p),
                ("apiVersion", ApiVersion), ("iface", _Interface)]


class SysInterfaceT(ctypes.Structure):
    # the system library reports a single version, not a core/backend pair
    _fields_ = [("backendId", U32), ("providerName", ctypes.c_char_p),
                ("systemApiVersion", Version), ("iface", _SysInterface)]


class SysGraphInfoV1(ctypes.Structure):
    _fields_ = [("graphName", ctypes.c_char_p), ("numGraphInputs", U32),
                ("graphInputs", ctypes.POINTER(Tensor)),
                ("numGraphOutputs", U32),
                ("graphOutputs", ctypes.POINTER(Tensor))]


class SysGraphInfoV3(ctypes.Structure):
    # extends V1; declared in full so the array stride is right
    _fields_ = [("graphName", ctypes.c_char_p), ("numGraphInputs", U32),
                ("graphInputs", ctypes.POINTER(Tensor)),
                ("numGraphOutputs", U32),
                ("graphOutputs", ctypes.POINTER(Tensor)),
                ("numUpdateableTensors", U32), ("updateableTensors", VP),
                ("graphBlobInfoSize", U32), ("graphBlobInfo", VP),
                ("startOpIndex", U32), ("endOpIndex", U32)]


class _GraphInfoBody(ctypes.Union):
    _fields_ = [("v1", SysGraphInfoV1), ("v3", SysGraphInfoV3)]


class SysGraphInfo(ctypes.Structure):
    _fields_ = [("version", I32), ("u", _GraphInfoBody)]


_BI_TAIL = [
    ("numContextTensors", U32), ("contextTensors", VP),
    ("numGraphs", U32), ("graphs", ctypes.POINTER(SysGraphInfo)),
]


class SysBinaryInfoV1(ctypes.Structure):
    _fields_ = [
        ("backendId", U32), ("buildId", ctypes.c_char_p),
        ("coreApiVersion", Version), ("backendApiVersion", Version),
        ("socVersion", ctypes.c_char_p), ("hwInfoBlobVersion", Version),
        ("contextBlobVersion", Version), ("hwInfoBlobSize", U32),
        ("hwInfoBlob", VP), ("contextBlobSize", U64),
    ] + _BI_TAIL


class SysBinaryInfoV2(ctypes.Structure):
    _fields_ = [
        ("backendId", U32), ("buildId", ctypes.c_char_p),
        ("coreApiVersion", Version), ("backendApiVersion", Version),
        ("socVersion", ctypes.c_char_p), ("hwInfoBlobVersion", Version),
        ("contextBlobVersion", Version), ("hwInfoBlobSize", U32),
        ("hwInfoBlob", VP), ("contextBlobSize", U64),
    ] + _BI_TAIL + [("platformInfo", VP)]


class SysBinaryInfoV3(ctypes.Structure):
    # 2.45 drops hwInfoBlob* and adds context metadata + socModel
    _fields_ = [
        ("backendId", U32), ("buildId", ctypes.c_char_p),
        ("coreApiVersion", Version), ("backendApiVersion", Version),
        ("socVersion", ctypes.c_char_p),
        ("contextBlobVersion", Version), ("contextBlobSize", U64),
    ] + _BI_TAIL + [
        ("platformInfo", VP), ("contextMetadataSize", U32),
        ("contextMetadata", VP), ("socModel", U32),
    ]


class _BinaryInfoBody(ctypes.Union):
    _fields_ = [("v1", SysBinaryInfoV1), ("v2", SysBinaryInfoV2),
                ("v3", SysBinaryInfoV3)]


class SysBinaryInfo(ctypes.Structure):
    _fields_ = [("version", I32), ("u", _BinaryInfoBody)]


# QNN encodes (category, width) in the enum value, e.g. UFIXED_POINT_16 = 0x416
DTYPE_SIZE = {
    0x008: 1, 0x016: 2, 0x032: 4, 0x064: 8,      # INT
    0x108: 1, 0x116: 2, 0x132: 4, 0x164: 8,      # UINT
    0x216: 2, 0x232: 4, 0x264: 8,                # FLOAT
    0x308: 1, 0x316: 2, 0x332: 4,                # SFIXED_POINT
    0x408: 1, 0x416: 2, 0x432: 4,                # UFIXED_POINT
    0x508: 1,                                    # BOOL_8
}


def qnn_name(name):
    """The context stores "/Concat_6_output_0" as "_Concat_6_output_0"."""
    return (name or "").replace("/", "_")


def check(err, what):
    if err != QNN_SUCCESS:
        raise RuntimeError("%s failed (0x%x)" % (what, err))


class Engine(object):
    """One context, one graph, every tensor resident."""

    def __init__(self, ctx, libdir="/usr/lib", layout=None, graph=None,
                 verbose=False):
        if not isinstance(layout, dict):
            layout = json.load(open(layout)) if layout else {}
        self.layout = layout
        self.libdir = libdir
        self.verbose = verbose
        self._in, self._out = {}, {}
        self._carry = []
        # layout.json is keyed by ONNX name; the context uses the sanitised one
        self._by_qnn = {}
        for onnx_name, spec in self.layout.items():
            self._by_qnn[spec.get("qnn_name") or qnn_name(onnx_name)] = spec

        offs = (InterfaceT.iface.offset, SysInterfaceT.iface.offset)
        if offs != (40, 32):
            raise RuntimeError("QNN interface table offsets %s != (40, 32)"
                               % (offs,))
        sizes = (ctypes.sizeof(QuantizeParams), ctypes.sizeof(TensorV1),
                 ctypes.sizeof(TensorV2), ctypes.sizeof(Tensor),
                 ctypes.sizeof(SysGraphInfo), ctypes.sizeof(SysBinaryInfo))
        if sizes != (40, 112, 136, 144, 88, 144):
            raise RuntimeError("QNN struct layout mismatch: %s" % (sizes,))

        self.sys_lib = ctypes.CDLL(os.path.join(libdir, "libQnnSystem.so"))
        self.htp_lib = ctypes.CDLL(os.path.join(libdir, "libQnnHtp.so"))

        get = self.sys_lib.QnnSystemInterface_getProviders
        get.restype = U32
        get.argtypes = [
            ctypes.POINTER(ctypes.POINTER(ctypes.POINTER(SysInterfaceT))),
            ctypes.POINTER(U32)]
        prov = ctypes.POINTER(ctypes.POINTER(SysInterfaceT))()
        n = U32(0)
        check(get(ctypes.byref(prov), ctypes.byref(n)),
              "QnnSystemInterface_getProviders")
        if not n.value:
            raise RuntimeError("no QNN system provider")
        self.sys = _Bind(prov[0].contents.iface, {
            "systemContextCreate": [ctypes.POINTER(VP)],
            "systemContextGetBinaryInfo": [VP, VP, U64, ctypes.POINTER(VP),
                                           ctypes.POINTER(U64)],
            "systemContextFree": [VP],
        })

        self.blob = np.fromfile(ctx, np.uint8)
        self.ctx_path = ctx
        self._size = U64(self.blob.size)
        h = VP()
        check(self.sys.systemContextCreate(ctypes.byref(h)),
              "systemContextCreate")
        info = VP()
        info_size = U64(0)
        check(self.sys.systemContextGetBinaryInfo(
            h, ctypes.cast(self.blob.ctypes.data, VP), self._size,
            ctypes.byref(info), ctypes.byref(info_size)),
            "systemContextGetBinaryInfo")
        bi = ctypes.cast(info, ctypes.POINTER(SysBinaryInfo)).contents
        self.binary_info_version = bi.version
        binfo = {1: bi.u.v1, 2: bi.u.v2, 3: bi.u.v3}.get(bi.version)
        if binfo is None:
            raise RuntimeError("unsupported binary info version %d"
                               % bi.version)
        if bi.version == 3:
            self.soc_model = binfo.socModel
        if not binfo.numGraphs:
            raise RuntimeError("context has no graphs")
        gi = None
        for i in range(binfo.numGraphs):
            cand = binfo.graphs[i].u.v1
            if graph is None or cand.graphName.decode() == graph:
                gi = cand
                break
        if gi is None:
            raise RuntimeError("graph %r not found" % graph)
        self.graph_name = gi.graphName.decode()
        self.system_ctx = h

        self.ins = [self._tensor(gi.graphInputs[i])
                    for i in range(gi.numGraphInputs)]
        self.outs = [self._tensor(gi.graphOutputs[i])
                     for i in range(gi.numGraphOutputs)]

        get2 = self.htp_lib.QnnInterface_getProviders
        get2.restype = U32
        get2.argtypes = [
            ctypes.POINTER(ctypes.POINTER(ctypes.POINTER(InterfaceT))),
            ctypes.POINTER(U32)]
        prov2 = ctypes.POINTER(ctypes.POINTER(InterfaceT))()
        n2 = U32(0)
        check(get2(ctypes.byref(prov2), ctypes.byref(n2)),
              "QnnInterface_getProviders")
        if not n2.value:
            raise RuntimeError("no QNN HTP provider")
        iface = prov2[0].contents.iface
        ver = prov2[0].contents.apiVersion.coreApiVersion
        self.api_version = "%d.%d.%d" % (ver.major, ver.minor, ver.patch)
        self.qnn = _Bind(iface, {
            "logCreate": [VP, U32, ctypes.POINTER(VP)],
            "logFree": [VP],
            "backendCreate": [VP, VP, ctypes.POINTER(VP)],
            "backendFree": [VP],
            "deviceCreate": [VP, VP, ctypes.POINTER(VP)],
            "deviceFree": [VP],
            "contextCreateFromBinary": [VP, VP, VP, VP, U64,
                                        ctypes.POINTER(VP), VP],
            "contextFree": [VP, VP],
            "graphRetrieve": [VP, ctypes.c_char_p, ctypes.POINTER(VP)],
            "graphFinalize": [VP, VP, VP],
            "graphExecute": [VP, ctypes.POINTER(Tensor), U32,
                             ctypes.POINTER(Tensor), U32, VP,
                             ctypes.POINTER(VP)],
        })

        # A logger is required (backendCreate rejects NULL); the callback only
        # receives a va_list we cannot format from Python, so it drops the text
        # and keeps the level so failures are still visible.
        self._log_cb = LOG_CALLBACK(self._on_log)
        self.logger = VP()
        check(self.qnn.logCreate(self._log_cb,
                                 3 if verbose else 1,
                                 ctypes.byref(self.logger)), "logCreate")
        self.backend = VP()
        self.device = VP()
        self.context = VP()
        self.graph = VP()
        check(self.qnn.backendCreate(self.logger, None,
                                     ctypes.byref(self.backend)),
              "backendCreate")
        check(self.qnn.deviceCreate(self.logger, None,
                                    ctypes.byref(self.device)),
              "deviceCreate")
        t0 = time.perf_counter()
        check(self.qnn.contextCreateFromBinary(
            self.backend, self.device, None,
            ctypes.cast(self.blob.ctypes.data, VP), self._size,
            ctypes.byref(self.context), None), "contextCreateFromBinary")
        self.load_ms = (time.perf_counter() - t0) * 1000.0
        check(self.qnn.graphRetrieve(self.context, self.graph_name.encode(),
                                     ctypes.byref(self.graph)), "graphRetrieve")
        # a graph from a serialized context is already finalized
        self.qnn.graphFinalize(self.graph, None, None)

        self.in_desc = (Tensor * len(self.ins))()
        for i, t in enumerate(self.ins):
            self.in_desc[i] = t.desc
            t.desc = ctypes.pointer(self.in_desc[i])
        self.out_desc = (Tensor * len(self.outs))()
        for i, t in enumerate(self.outs):
            self.out_desc[i] = t.desc
            t.desc = ctypes.pointer(self.out_desc[i])

        self._build_carries()
        self._last_ms = 0.0
        self._total_ms = 0.0
        self.steps = 0

    def _on_log(self, fmt, level, ts, args):
        if self.verbose:
            # the fmt may contain %s/%d, so print it as text only
            print("qnn[%d] %s" % (level, fmt.decode("utf-8", "replace")),
                  file=sys.stderr)

    # ---------------------------------------------------------------- setup
    def _tensor(self, src):
        t = _T()
        v = src.u.v1 if src.version == QNN_TENSOR_VERSION_1 else src.u.v2
        t.name = v.name.decode() if v.name else ""
        t.id = v.id
        t.dims = [v.dimensions[i] for i in range(v.rank)]
        t.dtype = v.dataType
        es = DTYPE_SIZE.get(v.dataType)
        if es is None:
            raise RuntimeError("unsupported dataType 0x%x (%s)"
                               % (v.dataType, t.name))
        n = es
        for d in t.dims:
            n *= d
        t.bytes = n
        t.buf = np.zeros(n, np.uint8)
        # Copy the descriptor verbatim from the context -- the backend matches
        # tensors by id, not by name -- and only attach our own buffer.
        desc = Tensor.from_buffer_copy(
            ctypes.string_at(ctypes.addressof(src), ctypes.sizeof(Tensor)))
        vv = desc.u.v1 if desc.version == QNN_TENSOR_VERSION_1 else desc.u.v2
        vv.memType = QNN_TENSORMEMTYPE_RAW
        vv.u.clientBuf.data = ctypes.cast(t.buf.ctypes.data, VP)
        vv.u.clientBuf.dataSize = n
        t.desc = desc
        return t

    def _encoding(self, t):
        """(scale, offset) of a tensor, or None when it is not quantised."""
        # t.desc is the descriptor struct until __init__ swaps in a pointer
        # to the array it passed to graphExecute.
        d = t.desc.contents if hasattr(t.desc, "contents") else t.desc
        v = d.u.v1 if d.version == QNN_TENSOR_VERSION_1 else d.u.v2
        qp = v.quantizeParams
        if qp.definition != QNN_QUANTIZATION_ENCODING_SCALE_OFFSET:
            return None
        so = qp.u.scaleOffset
        if not so.scale:
            return None
        return float(so.scale), int(so.offset)

    def zero_inputs(self):
        """Fill every input with the code that *means* 0.0.

        The client buffers hold quantised codes, so memset(0) does not mean
        zero: x = scale*(q + offset) makes code 0 decode to -scale*offset
        (-0.0967 for hidden_in, -2.09 for the mask lanes, -1.0 for the sine
        lane, -0.94 for every 16-bit state).  Starting the 48 state and KV
        tensors at raw 0 therefore injected a large negative constant into
        the recurrence and the model babbled.  Writing the code for 0.0,
        q = round(0/scale - offset) = -offset, fixes it, and the encoding
        comes from the context itself so no layout file is needed.
        """
        for t in self.ins:
            t.buf[:] = 0
            enc = self._encoding(t)
            if enc is None:
                continue
            code = -enc[1]
            es = DTYPE_SIZE.get(t.dtype, 2)
            if es == 2:
                t.buf.view(np.uint16)[:] = max(0, min(65535, code))
            elif es == 1:
                t.buf.view(np.uint8)[:] = max(0, min(255, code))
        return len(self.ins)

    def _build_carries(self):
        for o in self.outs:
            if not o.name.endswith("_out"):
                continue
            want = o.name[:-4] + "_in"
            for i in self.ins:
                if i.name != want:
                    continue
                c = {"src": o, "dst": i, "transpose": False}
                if o.dims != i.dims:
                    if (len(o.dims) == 3 and len(i.dims) == 3
                            and o.dims[0] == i.dims[0]
                            and o.dims[1] == i.dims[2]
                            and o.dims[2] == i.dims[1]):
                        c["transpose"] = True
                    elif self.verbose:
                        print("qnnlib: %s layout differs, copying raw"
                              % i.name)
                self._carry.append(c)
                break

    # ----------------------------------------------------------------- views
    def _view(self, tensor, key_dims, key_dtype):
        spec = self._by_qnn.get(tensor.name) or {}
        dims = spec.get(key_dims) or tensor.dims
        dt = spec.get(key_dtype) or ""
        np_dt = np.uint16 if "16" in dt else np.float32
        if np.dtype(np_dt).itemsize * int(np.prod(dims)) != tensor.bytes:
            np_dt = np.uint8
        return np.frombuffer(tensor.buf, np_dt).reshape(dims)

    def view_in(self, name):
        if name not in self._in:
            for t in self.ins:
                if t.name == qnn_name(name):
                    self._in[name] = self._view(t, "qnn_in", "qnn_dtype")
                    break
            else:
                return None
        return self._in[name]

    def view_out(self, name):
        if name not in self._out:
            for t in self.outs:
                if t.name == qnn_name(name):
                    self._out[name] = self._view(t, "qnn_out", "out_dtype")
                    break
            else:
                return None
        return self._out[name]

    # ------------------------------------------------------------- execution
    def exec(self):
        t0 = time.perf_counter()
        err = self.qnn.graphExecute(
            self.graph, self.in_desc, len(self.in_desc),
            self.out_desc, len(self.out_desc), None, None)
        self._last_ms = (time.perf_counter() - t0) * 1000.0
        self._total_ms += self._last_ms
        self.steps += 1
        if err != QNN_SUCCESS:
            raise RuntimeError("graphExecute failed (0x%x)" % err)
        return self._last_ms

    def carry(self):
        for c in self._carry:
            s, d = c["src"].buf, c["dst"].buf
            if not c["transpose"]:
                n = min(s.size, d.size)
                d[:n] = s[:n]
            else:
                si = c["src"]
                es = DTYPE_SIZE.get(si.dtype, 1)
                d1, d2 = si.dims[1], si.dims[2]
                sv = s.view(np.uint8).reshape(-1, d2, d1, es)
                dv = d.view(np.uint8).reshape(-1, d1, d2, es)
                dv[...] = sv.transpose(0, 2, 1, 3)
        return len(self._carry)

    @property
    def last_ms(self):
        return self._last_ms

    @property
    def mean_ms(self):
        return self._total_ms / self.steps if self.steps else 0.0

    def reset_stats(self):
        self._total_ms = 0.0
        self.steps = 0

    def close(self):
        for fn, arg in ((getattr(self.qnn, "logFree", None), self.logger),
                        (getattr(self.qnn, "contextFree", None), self.context),
                        (getattr(self.qnn, "deviceFree", None), self.device),
                        (getattr(self.qnn, "backendFree", None), self.backend)):
            try:
                if fn and arg.value:
                    fn(arg, None) if fn in (self.qnn.contextFree,
                                            self.qnn.deviceFree) else fn(arg)
            except Exception:
                pass
        try:
            if self.system_ctx.value:
                self.sys.systemContextFree(self.system_ctx)
        except Exception:
            pass


class _T(object):
    """Plain attribute bag for a graph tensor."""
    pass


class _Bind(object):
    """Turn a QNN function-pointer table into callable python attributes."""

    def __init__(self, iface, sigs):
        object.__setattr__(self, "_iface", iface)
        object.__setattr__(self, "_sigs", sigs)
        object.__setattr__(self, "_cache", {})

    def __getattr__(self, name):
        cache = object.__getattribute__(self, "_cache")
        if name in cache:
            return cache[name]
        sigs = object.__getattribute__(self, "_sigs")
        if name not in sigs:
            raise AttributeError(name)
        addr = getattr(object.__getattribute__(self, "_iface"), name)
        if not addr:
            raise RuntimeError("QNN interface has no %s" % name)
        fn = ctypes.CFUNCTYPE(U32, *sigs[name])(addr)
        cache[name] = fn
        return fn
