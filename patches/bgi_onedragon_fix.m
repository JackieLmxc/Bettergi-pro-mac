// bgi_onedragon_fix.m
// Runtime dylib (merged with qa_patch_v7.c into a SINGLE injected dylib) that
// fixes the OneDragon (一条龙) config save failure by swizzling
// +[NSJSONSerialization dataWithJSONObject:options:error:] and, ONLY for the
// OneDragon config payload(s), coercing in-memory Bool fields back to the
// correct numeric value read from the on-disk config (which is type-correct).
//
// Deployed MERGED with the proven v7 dylib (wine->yuan + VK1->F), so all prior
// fixes are preserved AND there is only ONE injected dylib (the known-good
// single-dylib form). In-memory only; nothing on disk is modified.
//
// v1.3:
//  - OneDragon DETECTION is now RECURSIVE: we search the ENTIRE object tree for
//    any known OneDragon marker key, not just the top-level dict. This catches
//    sub-configs where the failing key (e.g. AutoBossCompletedRunCount) is
//    nested and the top level carries no marker (the reason v1.0/1.2 missed it).
//  - On-disk config is loaded ONCE and cached (no per-serialization disk I/O).
//  - Coercion remains disk-scoped: only a payload field that is a Bool NSNumber
//    while the on-disk config holds a number for the same key is rewritten.
//    Legitimate bools (bool on both sides) and correct ints are left untouched.
//
// v1.4 (尘歌壶购买修复):
//  - Added SecretTreasureObjects normalization: Core requires index 0 to be a
//    purchase day ("每天重复"/"星期一".."星期日") and buys Skip(1). The macOS
//    Swift settings UI has no day picker and saved only product names, so Core
//    parsed a product name as a day -> "未配置购买商店物品" -> bought nothing.
//    We now prepend "每天重复" when (and only when) the first element is not a
//    valid day. Existing correct configs and empty/missing arrays are untouched.
//  - Added SecretTreasureObjects to the OneDragon marker keys so a payload that
//    only carries this field is still recognized.
//  - The Bool->number coercion logic is unchanged.

#import "onedragon_fix_core.h"
#import <stdio.h>
#import <time.h>

static FILE *g_log;
#define LOG(...) do { if (g_log) { \
    fprintf(g_log, "[pid=%d] ", getpid()); \
    fprintf(g_log, __VA_ARGS__); fflush(g_log); } } while (0)

// Original IMP of +[NSJSONSerialization dataWithJSONObject:options:error:].
static id (*g_orig_dataWithJSONObject)(id, SEL, id, NSUInteger, NSError **) = NULL;

// OneDragon config marker keys. The save path may serialize the config (or a
// sub-config) in which the failing key is nested; if ANY of these appears
// anywhere in the object tree, it is an OneDragon config and gets coercion.
static NSArray<NSString *> *kODMarkers = nil;

static NSString *g_diskPath = nil;
static id g_diskCache = nil;   // cached parsed on-disk config (type-correct)

// Recursive search: does the object tree contain any known OneDragon marker key?
// Returns YES on the first hit (cheap for the common case).
static BOOL od_tree_has_marker(id obj) {
    if ([obj isKindOfClass:[NSDictionary class]]) {
        NSDictionary *d = (NSDictionary *)obj;
        for (NSString *mk in kODMarkers)
            if (d[mk] != nil) return YES;
        for (id v in [d allValues])
            if (od_tree_has_marker(v)) return YES;
    } else if ([obj isKindOfClass:[NSArray class]]) {
        for (id v in (NSArray *)obj)
            if (od_tree_has_marker(v)) return YES;
    }
    return NO;
}

// Load and cache the on-disk OneDragon config (the type-correct reference).
static id od_load_disk(void) {
    if (g_diskCache) return g_diskCache;
    if (!g_diskPath) {
        NSString *home = NSHomeDirectory();
        g_diskPath = [home stringByAppendingPathComponent:
            @"Library/Application Support/betterGI-mac/User/OneDragon/默认配置.json"];
    }
    NSData *dd = [NSData dataWithContentsOfFile:g_diskPath];
    if (dd) g_diskCache = [NSJSONSerialization JSONObjectWithData:dd options:0 error:nil];
    return g_diskCache;
}

static id swizzled_dataWithJSONObject(id self, SEL _cmd, id obj,
                                      NSUInteger opts, NSError **err) {
    // Call the real implementation first (no recursion: we call the saved IMP).
    id data = g_orig_dataWithJSONObject(self, _cmd, obj, opts, err);
    @try {
        if (od_tree_has_marker(obj)) {
            // Which markers matched (top-level, for diagnostics only).
            NSMutableArray *hit = [NSMutableArray array];
            if ([obj isKindOfClass:[NSDictionary class]]) {
                for (NSString *mk in kODMarkers)
                    if (((NSDictionary *)obj)[mk] != nil) [hit addObject:mk];
            }
            LOG("[ODfix] OneDragon payload matched; topMarkers=%s\n",
                [[hit componentsJoinedByString:@","] UTF8String]);

            id disk = od_load_disk();
            LOG("[ODfix] disk loaded=%s\n", disk ? "yes" : "NO");

            // ---- DIAGNOSTIC: dump raw payload (via original IMP) ----
            NSData *raw = g_orig_dataWithJSONObject(self, _cmd, obj, opts, nil);
            if (raw) {
                NSString *s = [[NSString alloc] initWithData:raw
                                                    encoding:NSUTF8StringEncoding];
                LOG("[ODfix] RAW PAYLOAD: %s\n", [s UTF8String]);
            } else {
                LOG("[ODfix] RAW PAYLOAD: (original IMP returned nil — non-plist type present!)\n");
            }

            // ---- coerce (with per-key diagnostics) ----
            id fixed = od_fix_object_diag(obj, disk, g_log);
            if (fixed != obj) {
                LOG("[ODfix] coercing Bool->number via on-disk types\n");
                NSData *fx = g_orig_dataWithJSONObject(self, _cmd, fixed, opts, nil);
                NSString *fs = [[NSString alloc] initWithData:fx
                                                    encoding:NSUTF8StringEncoding];
                LOG("[ODfix] FIXED PAYLOAD: %s\n", [fs UTF8String]);
                NSError *e2 = nil;
                id newData = g_orig_dataWithJSONObject(self, _cmd, fixed, opts, &e2);
                if (newData) { LOG("[ODfix] rewrote OneDragon payload OK\n"); return newData; }
                LOG("[ODfix] rewrite failed: %s\n",
                    e2 ? [[e2 localizedDescription] UTF8String] : "?");
            } else {
                LOG("[ODfix] no Bool->number coercion needed\n");
            }
        }
    } @catch (NSException *ex) {
        LOG("[ODfix] exception: %s\n", [[ex description] UTF8String]);
    }
    return data;
}

__attribute__((constructor))
static void od_init(void) {
    g_log = fopen("/tmp/bgi_onedragon_fix.log", "a");
    LOG("[ODfix] constructor entered t=%ld pid=%d\n", (long)time(NULL), getpid());
    kODMarkers = @[
        @"MinResinToKeep",
        @"AutoBossRunCount",
        @"AutoBossCompletedRunCount",
        @"AutoBossTotalRunCountLimit",
        @"AutoBossReviveRetryCount",
        @"AutoBossTimeout",
        @"LeyLineRunCount",
        @"TaskEnabledList",
        @"TaskOrder",
        @"TaskDefinitions",
        @"WeeklyDomainEnabled",   // structural bool unique to OneDragon
        @"SecretTreasureObjects", // 尘歌壶购买清单（首项须为日期）
    ];
    Class cls = NSClassFromString(@"NSJSONSerialization");
    if (!cls) { LOG("[ODfix] NSJSONSerialization not found\n"); return; }
    SEL sel = @selector(dataWithJSONObject:options:error:);
    Method m = class_getClassMethod(cls, sel);
    if (!m) { LOG("[ODfix] method not found\n"); return; }
    g_orig_dataWithJSONObject =
        (id(*)(id, SEL, id, NSUInteger, NSError **))method_getImplementation(m);
    method_setImplementation(m, (IMP)swizzled_dataWithJSONObject);
    LOG("[ODfix] swizzle installed OK (NSJSONSerialization.dataWithJSONObject:)\n");
    // NOTE: keep g_log open for the process lifetime so save-time events are
    // captured. It is flushed automatically on process exit.
}
