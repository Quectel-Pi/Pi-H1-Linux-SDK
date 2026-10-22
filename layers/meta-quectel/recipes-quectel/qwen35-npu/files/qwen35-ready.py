#!/usr/bin/env python3
"""Probe the resident server's /v1/models endpoint.

Used by qwen35-model-install.sh and qwen35-launch.sh: the marker file is
root-owned and can be stale after a restart, an HTTP answer cannot.
"""
import json
import os
import sys
import urllib.request


def main():
    host = os.environ.get("QWEN35_HOST", "127.0.0.1")
    port = os.environ.get("QWEN35_PORT", "8000")
    timeout = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
    url = "http://%s:%s/v1/models" % (host, port)
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            json.load(r)
    except Exception:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
