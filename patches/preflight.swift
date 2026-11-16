import CoreGraphics
import AppKit
import Foundation

// 只读预检：判断 wine->yuan 补丁在当前窗口上是否真的会触发白条裁剪。
// 不改任何文件、不动任何进程。需先启动原神（窗口化）。

func num(_ v: Any?) -> Double { (v as? NSNumber)?.doubleValue ?? 0 }

print("========== BetterGI 白条裁剪预检 ==========")
guard let list = CGWindowListCopyWindowInfo(
        [.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] else {
    print("❌ 无法读取窗口列表（屏幕录制权限？）")
    exit(1)
}

var found = false
for w in list {
    let owner = w[kCGWindowOwnerName as String] as? String ?? ""
    let title = w[kCGWindowName as String] as? String ?? ""
    let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
    let W = num(b["Width"]), H = num(b["Height"])
    let layer = (w[kCGWindowLayer as String] as? NSNumber)?.intValue ?? -1
    if layer != 0 || W < 800 { continue }

    let lo = owner.lowercased(), lt = title.lowercased()
    let isGame = lo.contains("yuan") || lo.contains("wine") || lo.contains("crossover")
              || lt.contains("原神") || lt.contains("genshin") || lt.contains("yuanshen")
    if !isGame { continue }
    found = true

    let h16 = W * 9.0 / 16.0
    let bar = H - h16
    let cap = min(96.0, H * 0.08)
    let geoOK = (bar >= 16.0 && bar <= cap)
    let s = NSScreen.main?.backingScaleFactor ?? 2.0

    print("owner = \(owner)")
    print("title = \(title)")
    print("窗口(逻辑点) = \(Int(W)) x \(Int(H))   Retina scale = \(String(format: "%.1f", s))x")
    print("窗口(物理像素) = \(Int(W*s)) x \(Int(H*s))   ← 游戏实际渲染尺寸")
    print("   裁剪后(点) = \(Int(W)) x \(Int(h16))  →  物理像素 \(Int(W*s)) x \(Int(h16*s))")
    print("")
    print("① 门1/门2 owner 关键字（补丁后 needle = \"yuan\"）")
    print("   owner.lowercased = \(lo)")
    print("   \(lo.contains("yuan") ? "✅ 含 yuan → 两道门放行" : "❌ 不含 yuan → 裁剪分支仍被 gate 关住，补丁无效")")
    print("")
    print("② 几何 guard（captureRect 内置保护，按比例判，与缩放无关）")
    print("   白条高度 = H - W*9/16 = \(String(format: "%.1f", bar)) 点 = \(String(format: "%.1f", bar*s)) 物理像素")
    print("   允许区间(点) = [16.0, min(96, H*0.08)=\(String(format: "%.1f", cap))]")
    print("   \(geoOK ? "✅ 通过 → 裁成 \(Int(W)) x \(Int(h16)) 点 = \(Int(W*s)) x \(Int(h16*s)) 物理像素（从顶部裁掉白条）" : "❌ 拒绝 → 返回整窗，白条仍在帧内")")
    print("")
    print("③ A0 自动识别（辅助参考项，不参与补丁判定）")
    if title.isEmpty {
        // 从 Finder 双击的 .app 上下文里，TCC 常只允许读 kCGWindowOwnerName，
        // kCGWindowName 返回 nil —— 这不代表游戏窗口真的没有标题。
        // 真机判据以 BetterGI 自身日志 "Selected game window: YuanShen.exe — 原神" 为准。
        print("   ⚠️ 本进程读不到窗口标题（kCGWindowName 受屏幕录制权限粒度限制），非游戏侧异常")
        print("   请以 BetterGI 日志 'Selected game window: YuanShen.exe — 原神' 为准")
    } else {
        print("   ✅ title = \(title) → priority>0")
    }
    print("")
    let verdict = lo.contains("yuan") && geoOK
    print(">>> 综合判定：\(verdict ? "✅ 补丁应生效，识别框 = 底部 16:9 区域、白条被裁、点击零偏移" : "❌ 补丁不会生效，请把上面输出发给助手定位")")
    print("------------------------------------------")
}

if !found {
    print("未找到候选游戏窗口。")
    print("请确认：原神已启动、且以窗口化 1920x1080 运行（CrossOver 会在上方加白条）。")
}
