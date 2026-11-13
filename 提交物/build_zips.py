# -*- coding: utf-8 -*-
"""按课程要求打包提交 zip（排除 build/managed_components/sdkconfig/.venv/__pycache__ 等）"""
import os
import sys
import zipfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

OUT = os.path.dirname(os.path.abspath(__file__))

def zip_dir(src, out, exclude_dirs=(), exclude_files=(), prefix=""):
    count = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for root, dirs, files in os.walk(src):
            dirs[:] = [d for d in dirs if d not in exclude_dirs]
            for f in files:
                if f in exclude_files or f.endswith(".pyc"):
                    continue
                full = os.path.join(root, f)
                arc = os.path.join(prefix, os.path.relpath(full, src))
                z.write(full, arc)
                count += 1
    return count

def main():
    # 1) 项目一固件（排除真实 WiFi 密码的 sdkconfig）
    n = zip_dir(r"D:\esp\projects\k10_level_control\firmware",
                os.path.join(OUT, "项目一_固件_K10液位控制站.zip"),
                exclude_dirs=("build", "managed_components"),
                exclude_files=("sdkconfig", "dependencies.lock"),
                prefix="firmware")
    print(f"项目一固件: {n} 个文件")

    # 2) 项目一上位机（保留实验数据与曲线作为证据；路径由脚本位置推导，可移植）
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    n = zip_dir(os.path.join(ROOT, "项目一", "pc_master"),
                os.path.join(OUT, "项目一_上位机_Modbus主站.zip"),
                exclude_dirs=(".venv", "__pycache__"),
                prefix="pc_master")
    print(f"项目一上位机: {n} 个文件")

    # 3) 项目二网关（含 DI NodeSet 信息模型与方案文档）
    n = zip_dir(os.path.join(ROOT, "项目二", "gateway"),
                os.path.join(OUT, "项目二_OPCUA网关.zip"),
                exclude_dirs=(".venv", "__pycache__"),
                prefix="gateway")
    print(f"项目二网关: {n} 个文件")

    # 方案文档单独打进网关 zip 的 docs 目录
    with zipfile.ZipFile(os.path.join(OUT, "项目二_OPCUA网关.zip"), "a",
                         zipfile.ZIP_DEFLATED) as z:
        z.write(os.path.join(ROOT, "项目二", "docs", "方案.md"),
                "gateway/docs/方案.md")
    print("方案文档已附加")

    for f in sorted(os.listdir(OUT)):
        if f.endswith(".zip"):
            print(f"  {f} ({os.path.getsize(os.path.join(OUT, f))/1024:.0f} KB)")

if __name__ == "__main__":
    main()
