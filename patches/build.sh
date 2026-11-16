#!/bin/bash
# 从源码重新编译两个注入补丁与预检工具。
# Requires: Xcode command line tools (clang, swiftc). Target arch: arm64 (Apple Silicon).
set -euo pipefail
cd "$(dirname "$0")"

# v7 补丁：wine->yuan 白条/分辨率修复 + 幽境危战 VK1->F 按键修复
clang -shared -fPIC -arch arm64 -o libqa_patch_wine_gate_v7.dylib qa_patch_v7.c

# 一条龙保存修复补丁（Objective-C，依赖 Foundation / objc runtime）
clang -shared -fPIC -arch arm64 -framework Foundation \
  -o libbgi_onedragon_fix.dylib bgi_onedragon_fix.m

# 预检工具（只读窗口检测）—— 输出到上级目录，与启动脚本 DIR/preflight 保持一致
swiftc -O -o ../preflight preflight.swift

echo "编译完成：patches/ 下两个 .dylib + 上级目录的 preflight"
