// onedragon_fix_core.h
// Shared coercion logic for the BetterGI-mac OneDragon save fix.
// Problem: on save, BetterGI-mac serializes the OneDragon config, but some
// integer fields (e.g. MinResinToKeep) are held in memory as a Swift Bool
// instead of an Int. Core's C# model expects int -> Newtonsoft throws
// JsonReaderException ("Unexpected character ... f. Path 'MinResinToKeep'").
//
// Fix: when serializing the OneDragon document, coerce any field that is a
// BOOLEAN in the in-memory payload but a NUMBER in the on-disk config back to
// its correct numeric value (looked up by name in the on-disk config). Legitimate
// booleans (bool on both sides) and correctly-typed numbers are left untouched.
//
// This header is included by both the injected dylib and the standalone test.

#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <dispatch/dispatch.h>

// One-line description of a value (for diagnostics).
static inline NSString *od_describe(id v) {
    if ([v isKindOfClass:[NSNumber class]]) {
        NSNumber *n = (NSNumber *)v;
        const char *t = [n objCType];
        return [NSString stringWithFormat:@"NSNumber(objCType=%s,val=%@)",
                t ? t : "?", n];
    } else if ([v isKindOfClass:[NSString class]]) {
        return [NSString stringWithFormat:@"NSString=%@", v];
    } else if ([v isKindOfClass:[NSDictionary class]]) return @"Dict";
    else if ([v isKindOfClass:[NSArray class]])    return @"Array";
    else if ([v isKindOfClass:[NSNull class]])     return @"Null";
    else return [NSString stringWithFormat:@"%s@%p", object_getClassName(v), v];
}

// A "boolean NSNumber" is one whose objCType is 'c' (char) -- this is what
// Swift Bool / [NSNumber numberWithBool:] bridge to (__NSCFBoolean). Real
// integers serialize with objCType 'i'/'q'/'l', etc.
static inline BOOL od_is_bool_number(NSNumber *n) {
    if (!n) return NO;
    const char *t = [n objCType];
    if (!t || !t[0]) return NO;
    return (t[0] == 'c');
}

// Recursive lookup of a key anywhere in the on-disk config tree. Field names in
// the OneDragon config are unique, so the first match is the right one.
static inline id od_find_in_disk(NSString *key, id disk) {
    if ([disk isKindOfClass:[NSDictionary class]]) {
        NSDictionary *d = (NSDictionary *)disk;
        id v = d[key];
        if (v) return v;
        for (id sub in [d allValues]) {
            id r = od_find_in_disk(key, sub);
            if (r) return r;
        }
    } else if ([disk isKindOfClass:[NSArray class]]) {
        for (id sub in (NSArray *)disk) {
            id r = od_find_in_disk(key, sub);
            if (r) return r;
        }
    }
    return nil;
}

// Recursive presence check (used to cheaply detect the OneDragon payload).
static inline BOOL od_contains_key(id obj, NSString *key) {
    if ([obj isKindOfClass:[NSDictionary class]]) {
        if (((NSDictionary *)obj)[key] != nil) return YES;
        for (id v in [(NSDictionary *)obj allValues])
            if (od_contains_key(v, key)) return YES;
    } else if ([obj isKindOfClass:[NSArray class]]) {
        for (id v in (NSArray *)obj)
            if (od_contains_key(v, key)) return YES;
    }
    return NO;
}

// ---------------------------------------------------------------------------
// SereniteaPot (尘歌壶) purchase list normalization.
//
// Core contract (GoToSereniteaPotTask.cs): SecretTreasureObjects[0] is the
// purchase DAY ("每天重复" or "星期一".."星期日"); items start at index 1 and
// Core buys `Skip(1)`. The macOS Swift settings UI has no day picker and saves
// only the checked item names, so index 0 is a product name -> Core parses it
// as a day -> null -> "未配置购买商店物品" and buys nothing.
//
// Fix: at serialization time, if the array exists and its first element is NOT
// a valid day, insert "每天重复" at index 0. We deliberately leave an already
// valid day untouched (no overwrite of correct configs), and leave empty /
// missing / non-array values alone (no collateral damage).
// ---------------------------------------------------------------------------
static inline BOOL od_is_valid_purchase_day(id first) {
    if (![first isKindOfClass:[NSString class]]) return NO;
    NSString *s = (NSString *)first;
    if ([s isEqualToString:@"每天重复"]) return YES;
    static NSSet *days = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        days = [NSSet setWithArray:@[@"星期一", @"星期二", @"星期三", @"星期四",
                                     @"星期五", @"星期六", @"星期日"]];
    });
    return [days containsObject:s];
}

// Returns a NEW array with "每天重复" prepended when needed, or the original
// array when nothing should change. `logf` may be NULL.
static inline id od_normalize_secret_treasure_objects(id value, FILE *logf) {
    if (![value isKindOfClass:[NSArray class]]) return value;
    NSArray *arr = (NSArray *)value;
    if (arr.count == 0) return value;                 // empty: leave alone
    id first = arr[0];
    if (od_is_valid_purchase_day(first)) return value; // already correct: untouched
    if (![first isKindOfClass:[NSString class]]) {
        // First element is not even a string (unexpected shape) — do not guess.
        if (logf) fprintf(logf, "[ODfix]   STPOT skip: first not NSString\n");
        return value;
    }
    if (logf) fprintf(logf, "[ODfix]   STPOT first='%s' is not a day -> prepend 每天重复\n",
                      [(NSString *)first UTF8String]);
    NSMutableArray *m = [NSMutableArray arrayWithCapacity:arr.count + 1];
    [m addObject:@"每天重复"];
    [m addObjectsFromArray:arr];
    return m;
}

// Recursively walk payload; for every key whose value is a boolean NSNumber but
// the on-disk config holds a numeric NSNumber for the same key name, replace the
// payload value with the on-disk numeric value. Returns a new object if anything
// changed, otherwise the original object unchanged.
static inline id od_fix_object(id payload, id diskGlobal) {
    if ([payload isKindOfClass:[NSDictionary class]]) {
        NSMutableDictionary *m = [NSMutableDictionary dictionaryWithDictionary:payload];
        BOOL changed = NO;
        for (NSString *key in payload) {
            id pv = payload[key];
            if ([key isEqualToString:@"SecretTreasureObjects"]) {
                // 尘歌壶购买清单：首项必须是日期，否则前插"每天重复"。
                // 规范化后的数组不带嵌套结构，无需再走通用递归。
                id r = od_normalize_secret_treasure_objects(pv, NULL);
                if (r != pv) { m[key] = r; changed = YES; }
            } else if ([pv isKindOfClass:[NSDictionary class]] ||
                [pv isKindOfClass:[NSArray class]]) {
                id r = od_fix_object(pv, diskGlobal);
                if (r != pv) { m[key] = r; changed = YES; }
            } else if ([pv isKindOfClass:[NSNumber class]] && od_is_bool_number(pv)) {
                id dv = od_find_in_disk(key, diskGlobal);
                if (dv && [dv isKindOfClass:[NSNumber class]] && !od_is_bool_number(dv)) {
                    m[key] = dv; changed = YES;
                }
            }
        }
        return changed ? m : payload;
    }
    if ([payload isKindOfClass:[NSArray class]]) {
        NSMutableArray *m = [NSMutableArray arrayWithArray:payload];
        BOOL changed = NO;
        for (NSUInteger i = 0; i < m.count; i++) {
            id r = od_fix_object(m[i], diskGlobal);
            if (r != m[i]) { m[i] = r; changed = YES; }
        }
        return changed ? m : payload;
    }
    return payload;
}

// Diagnostic variant: same logic as od_fix_object but logs each boolean-field
// consideration (what the payload value is, and what the on-disk value is) so we
// can see exactly why a given field was or wasn't coerced.
static inline id od_fix_object_diag(id payload, id diskGlobal, FILE *logf) {
    if ([payload isKindOfClass:[NSDictionary class]]) {
        NSMutableDictionary *m = [NSMutableDictionary dictionaryWithDictionary:payload];
        BOOL changed = NO;
        for (NSString *key in payload) {
            id pv = payload[key];
            if ([key isEqualToString:@"SecretTreasureObjects"]) {
                // 尘歌壶购买清单：首项必须是日期，否则前插"每天重复"。
                id r = od_normalize_secret_treasure_objects(pv, logf);
                if (r != pv) {
                    m[key] = r; changed = YES;
                    if (logf) fprintf(logf, "[ODfix]     -> STPOT normalized\n");
                }
            } else if ([pv isKindOfClass:[NSDictionary class]] ||
                [pv isKindOfClass:[NSArray class]]) {
                id r = od_fix_object_diag(pv, diskGlobal, logf);
                if (r != pv) { m[key] = r; changed = YES; }
            } else if ([pv isKindOfClass:[NSNumber class]]) {
                BOOL pvBool = od_is_bool_number(pv);
                id dv = od_find_in_disk(key, diskGlobal);
                BOOL dvIsNum = [dv isKindOfClass:[NSNumber class]];
                if (logf) {
                    if (!dv) {
                        fprintf(logf, "[ODfix]     consider key=%-32s pv=%s disk=nil\n",
                            [key UTF8String], [od_describe(pv) UTF8String]);
                    } else if (dvIsNum) {
                        fprintf(logf, "[ODfix]     consider key=%-32s pv=%s disk=NSNumber(%s)=%s\n",
                            [key UTF8String], [od_describe(pv) UTF8String],
                            [dv objCType], [[dv stringValue] UTF8String]);
                    } else {
                        fprintf(logf, "[ODfix]     consider key=%-32s pv=%s disk=nonNumber(%s)\n",
                            [key UTF8String], [od_describe(pv) UTF8String],
                            object_getClassName(dv));
                    }
                }
                if (pvBool && dvIsNum && !od_is_bool_number(dv)) {
                    m[key] = dv; changed = YES;
                    if (logf) fprintf(logf,
                        "[ODfix]     -> COERCED %s to number\n", [key UTF8String]);
                }
            }
        }
        return changed ? m : payload;
    }
    if ([payload isKindOfClass:[NSArray class]]) {
        NSMutableArray *m = [NSMutableArray arrayWithArray:payload];
        BOOL changed = NO;
        for (NSUInteger i = 0; i < m.count; i++) {
            id r = od_fix_object_diag(m[i], diskGlobal, logf);
            if (r != m[i]) { m[i] = r; changed = YES; }
        }
        return changed ? m : payload;
    }
    return payload;
}
