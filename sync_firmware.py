# -*- coding: utf-8 -*-
"""把固件源码同步到本仓库（项目一/firmware/）。

为什么固件不在仓库里直接开发：ESP-IDF 的 cmake/ccache 无法处理中文路径，
固件实际开发目录必须在纯英文路径。
本脚本把源码副本同步进仓库用于提交，排除构建产物与含真实 WiFi 密码的 sdkconfig。
用法：
    python sync_firmware.py                          # 使用默认源目录（本机）
    python sync_firmware.py <你的固件开发目录>        # 组员指定自己的路径
改完固件代码后运行，再 git commit。
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# 默认源目录（本机）；组员可传参覆盖，路径必须纯英文（ESP-IDF 要求）
SRC = sys.argv[1] if len(sys.argv) > 1 else r"D:\esp\projects\k10_level_control\firmware"
DST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "项目一", "firmware")

EXCLUDE_DIRS = {"build", "managed_components"}
EXCLUDE_FILES = {"sdkconfig", "dependencies.lock"}

def main():
    if not os.path.isdir(SRC):
        print(f"源目录不存在: {SRC}")
        return 1
    if os.path.isdir(DST):
        shutil.rmtree(DST)
    os.makedirs(DST)
    n = 0
    for root, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
        for f in files:
            if f in EXCLUDE_FILES or f.endswith(".pyc"):
                continue
            full = os.path.join(root, f)
            rel = os.path.relpath(full, SRC)
            target = os.path.join(DST, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(full, target)
            n += 1
    print(f"同步完成：{n} 个文件 -> {DST}")
    print("（已排除 build/managed_components/sdkconfig/dependencies.lock）")
    return 0

if __name__ == "__main__":
    sys.exit(main())
