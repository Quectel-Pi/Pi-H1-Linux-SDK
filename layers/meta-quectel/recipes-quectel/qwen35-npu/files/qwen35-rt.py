#!/usr/bin/env python3
"""CLI for the resident runner; the implementation is qwen35rt.py."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from qwen35rt import main  # noqa: E402

if __name__ == "__main__":
    main()
