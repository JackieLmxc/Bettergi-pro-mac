#!/bin/bash
# ② 启动 BetterGI（原生 macOS 应用 + 内存补丁注入）
# 建议顺序：先「① 启动原神」并进到大世界，再双击本文件。
# 若原神尚未运行，本脚本会尝试自动先去启动它；若仍失败会提示你先手动启动。
# 补丁为「内存注入、零磁盘修改」：v7 = 白条/分辨率 + 幽境危战按键；FIX = 一条龙保存修复。

set -u
DIR="$(cd "$(dirname "$0")" && pwd)"
APP="$DIR/BetterGI-mac.app"
BIN="$APP/Contents/MacOS/betterGI-mac"
V7="$DIR/patches/libqa_patch_wine_gate_v7.dylib"
FIX="$DIR/patches/libbgi_onedragon_fix.dylib"
PRE="$DIR/preflight"
QA_LOG=/tmp/qa_patch_wine_gate.log
FIXLOG=/tmp/bgi_onedragon_fix.log

msg()   { echo "$1"; }
pause() { echo; read -n 1 -s -r -p "按任意键关闭窗口…"; echo; }

# ---------- 0. 组件自检 ----------
[ -x "$BIN" ] || { msg "❌ 找不到 ${APP}（BetterGI-mac.app 是否完整？）"; pause; exit 1; }
[ -f "$V7" ]  || { msg "❌ 缺少补丁：$V7"; pause; exit 1; }
[ -f "$FIX" ] || { msg "❌ 缺少补丁：$FIX"; pause; exit 1; }

if ! codesign -d --entitlements - "$APP" 2>/dev/null | grep -q "allow-dyld-environment-variables"; then
  msg "⚠️ 该 BetterGI-mac.app 未重签「允许 dyld 注入」，补丁可能不生效。"
  msg "   若界面正常但白条/一条龙有问题，请见 使用说明.md 的「重签」小节。"
fi

game_running() {
  pgrep -f "YuanShen.exe" >/dev/null 2>&1 && return 0
  [ -x "$PRE" ] && "$PRE" 2>/dev/null | grep -q "owner = YuanShen.exe" && return 0
  return 1
}

# ---------- 1. 确保原神在运行 ----------
if ! game_running; then
  msg "ℹ️ 未检测到原神窗口，尝试先启动原神…"
  SIB="$DIR/../原神-mac/① 启动原神.command"
  if [ -f "$SIB" ]; then
    nohup bash "$SIB" >/tmp/genshin_launch.log 2>&1 &
    disown 2>/dev/null || true
    for i in $(seq 1 30); do game_running && break; sleep 3; done
  fi
  if ! game_running; then
    msg "⚠️ 仍未检测到原神。请先双击「原神-mac/① 启动原神.command」并进入游戏大世界，再回来双击本文件。"
    pause; exit 1
  fi
  msg "✅ 原神窗口已出现"
fi

# ---------- 2. 退出已运行的 BetterGI（以便重新注入补丁） ----------
if pgrep -f "betterGI-mac" >/dev/null 2>&1; then
  msg "⟳ 退出已运行的 BetterGI…"
  osascript -e 'quit app "BetterGI-mac"' >/dev/null 2>&1 || true
  for i in $(seq 1 10); do pgrep -f "betterGI-mac" >/dev/null 2>&1 || break; sleep 1; done
  pkill -9 -f "betterGI-mac" >/dev/null 2>&1 || true
  pkill -9 -f "BetterGenshinImpact.Core.Host" >/dev/null 2>&1 || true
  sleep 2
fi

# ---------- 3. 直接 exec 注入（关键：不能走 open -a，open -a 不传 DYLD） ----------
rm -f "$QA_LOG" "$FIXLOG"
msg "▶ 带补丁启动 BetterGI（原生 macOS + v7 + 一条龙修复）…"
cd "$APP/Contents/MacOS" || { msg "❌ 无法进入 $APP/Contents/MacOS"; pause; exit 1; }
nohup env DYLD_INSERT_LIBRARIES="$V7:$FIX" ./betterGI-mac --input-backend foreground-cgevent \
  > /tmp/bgi_launch_patched.log 2>&1 &
disown 2>/dev/null || true

# ---------- 4. 校验注入结果 ----------
V7OK=0; FIXOK=0
for i in $(seq 1 20); do
  [ "$V7OK" = 0 ]  && grep -qE "wine=3/3 APPLIED|OK: 3x wine->yuan" "$QA_LOG" 2>/dev/null && V7OK=1
  [ "$FIXOK" = 0 ] && grep -q "swizzle installed OK" "$FIXLOG" 2>/dev/null && FIXOK=1
  [ "$V7OK" = 1 ] && [ "$FIXOK" = 1 ] && break
  sleep 1
done
[ "$V7OK" = 1 ]  && msg "✅ 白条/分辨率补丁(v7) 已生效" || msg "⚠️ 未检测到 v7 生效标记（详见 ${QA_LOG}）"
[ "$FIXOK" = 1 ] && msg "✅ 一条龙保存修复 已注入"    || msg "⚠️ 未检测到一条龙修复标记（详见 ${FIXLOG}）"
msg "ℹ️ BetterGI 主界面稍后会自动出现。"
sleep 2
