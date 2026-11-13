# -*- coding: utf-8 -*-
"""实验报告曲线生成：读取 实验数据/*.csv，输出 PNG 插图（无界面，可直接调用）。
用于实验报告：稳态基线、设定值阶跃响应、出水阀扰动恢复三张图。
"""
import csv
import glob
import os
import sys

import matplotlib
matplotlib.use("Agg")   # 无界面后端，直接出图
import matplotlib.pyplot as plt

plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei"]
plt.rcParams["axes.unicode_minus"] = False

OUT_DIR = "实验数据/曲线"

def plot_csv(path, title, out_name, ylim=None):
    with open(path, encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    t = list(range(len(rows)))            # 采样点（2Hz → 秒）
    level = [float(r["液位mm"]) for r in rows]
    sp = [float(r["设定值mm"]) for r in rows]
    vin = [float(r["进水阀开度%"]) for r in rows]
    vout = [float(r["出水阀开度%"]) for r in rows]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6), sharex=True,
                                   gridspec_kw={"height_ratios": [3, 1]})
    ax1.plot(t, level, lw=1.6, label="实际液位", color="#1f77b4")
    ax1.plot(t, sp, lw=1.2, linestyle="--", label="设定值", color="#ff7f0e")
    ax1.set_ylabel("液位 (mm)")
    ax1.set_title(title)
    ax1.legend(loc="best")
    ax1.grid(alpha=0.3)
    if ylim:
        ax1.set_ylim(*ylim)

    ax2.plot(t, vin, lw=1.0, label="进水阀开度", color="#2ca02c")
    ax2.plot(t, vout, lw=1.0, label="出水阀开度", color="#d62728")
    ax2.set_xlabel("时间 (s)")
    ax2.set_ylabel("阀门开度 (%)")
    ax2.legend(loc="best")
    ax2.grid(alpha=0.3)

    fig.tight_layout()
    os.makedirs(OUT_DIR, exist_ok=True)
    out = f"{OUT_DIR}/{out_name}"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"已生成 {out}")

def main():
    jobs = [
        ("实验数据/A_稳态基线.csv", "稳态基线（设定 250mm）", "A_稳态.png", (245, 255)),
        ("实验数据/B_设定值阶跃.csv", "设定值阶跃响应（250→350mm）", "B_阶跃.png", (240, 360)),
        ("实验数据/C_出水阀扰动.csv", "出水阀扰动与恢复（30→60%）", "C_扰动.png", (240, 360)),
    ]
    for path, title, out, ylim in jobs:
        if not os.path.exists(path):
            print(f"缺失 {path}，跳过")
            continue
        plot_csv(path, title, out, ylim)

if __name__ == "__main__":
    main()
