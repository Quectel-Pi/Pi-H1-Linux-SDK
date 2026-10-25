"""Return free glibc heap pages once after startup; never unload a live model."""
import ctypes
import gc
import sys
import time
from pathlib import Path

def release_startup_heap():
    started=time.perf_counter()
    def rss():
        try:
            return int(next(s.split()[1] for s in Path('/proc/self/status').read_text().splitlines() if s.startswith('VmRSS:')))*1024
        except (OSError,StopIteration):return None
    before=rss()
    gc.collect()
    status='unavailable'
    if sys.platform.startswith('linux'):
        libc=ctypes.CDLL(None)
        trim=getattr(libc,'malloc_trim',None)
        if trim is not None:
            trim.argtypes=[ctypes.c_size_t];trim.restype=ctypes.c_int
            status='released' if trim(0) else 'no_free_pages'
    return {'method':'startup_only_malloc_trim','status':status,'rss_before':before,
            'rss_after':rss(),'seconds':time.perf_counter()-started,'session_unloaded':False}
