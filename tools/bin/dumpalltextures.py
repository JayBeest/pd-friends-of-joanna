#!/usr/bin/env python3

import subprocess
from pathlib import Path

"""
dumps all textures from gex
"""

for i in range(0, int(hex(int("ffff", 16)), 16)):
    t = '{:04X}'.format(i).lower()
    if Path(f"{t}.bin").exists() and not Path(f"{t}.png").exists():
        print(t)
        cmd = "PD=$(realpath .) pdt tex2png " + t + ".bin " + t + ".png"
        print(cmd)
        subprocess.run(cmd, shell=True)
