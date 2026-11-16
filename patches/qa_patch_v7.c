/*
 * qa_patch_wine_gate — v7
 *
 * In-memory, fully revertible patches for the BetterGI-mac main executable.
 *
 * Sites 1-3 (UNCHANGED from v6): patch the inline string literal "wine" to
 *   "yuan" at file offsets 0x1b0868 / 0x2b9ee0 / 0x2ba06c.
 *     OLD = e8 2e 8d 52 c8 ad ac 72   ("wine")
 *     NEW = 28 af 8e 52 28 cc ad 72   ("yuan")
 *
 * Site 4 (NEW in v7): make windowsVirtualKey == 1 behave EXACTLY like
 *   VK 0x46 ('F') inside
 *     BetterGICoreInputKeyMapper.keyCode(fromWindowsVirtualKey:)
 *   VA 0x1000b8d68 / file 0xB8D68.
 *
 *   Root cause (read-only investigation, 2026-11-14/15):
 *     AutoStygianOnslaughtTask -> HandleDomainLobbyState ("步行前往钥匙")
 *     -> WalkToFTask.Start -> TaskControl.Current.PressKey(pickAssets.PickVk).
 *     AutoPickAssets.PickVk is a BgiKey enum whose tag 1 == "F"; macOS port
 *     forgot BgiKeyMapper.ToWindowsVirtualKey() (tag 1 -> VK 0x46) and sent
 *     the raw enum tag as windowsVirtualKey. VK 1 < 0x08 falls through the
 *     mapper's range check (sub x8,x0,#8 / cmp x8,#0xd6 / b.hi) to
 *     "mov w0,#0x4e" (nil sentinel) -> "Unsupported BetterGI virtual key."
 *
 *   Site 4 is TWO writes forming ONE logical patch (a code cave):
 *     P4a @ file 0xB8F54 (VA 0x1000B8F54):
 *       OLD  c0 09 80 52   mov w0, #0x4e        (nil sentinel)
 *       NEW  e5 0f 0c 14   b <CAVE>
 *       (the following "b 0x1000b8e54" at 0xB8F58 is left untouched; it is
 *        dead code after the redirect, and keeping it avoids a 2nd write)
 *     P4b @ file 0x3BCEE8 (VA 0x1003BCEE8): 24-byte cave in the 12564-byte
 *       zero padding AFTER the last __TEXT section (__eh_frame ends at
 *       0x3BCEE8; __TEXT filesize 0x3C0000). Executable (initprot r-x),
 *       file-backed, not referenced by any section.
 *         +0x00  cmp  x0, #1
 *         +0x04  b.eq +0x0C                (-> vk1)
 *         +0x08  mov  w0, #0x4e            ; original nil sentinel
 *         +0x0C  b    0x1000B8E54         ; common epilogue
 *         +0x10  mov  x0, #0x46  ('F')    ; vk1:
 *         +0x14  b    0x1000B8E20         ; letter branch -> "f" -> tag 5
 *
 *   Only VK 1 changes behavior. Every other VK keeps the original path
 *   bit-for-bit: table-path VKs arrive with x0 = 0x39, all out-of-range VKs
 *   other than 1 keep nil -> error. Reusing the existing letter branch means
 *   the F result is computed by the app itself (String -> KeyCode(rawValue:)
 *   -> tag 5 -> cgKeyCode 3 = kVK_ANSI_F), not by a hard-coded tag.
 *
 *   f tag evidence (static, all from the shipped binary):
 *     KeyCode.rawValue getter 0x100244a44: jump table @0x3106fe, tag 5 ->
 *       0x100244d50 "mov w0,#0x66" = "f".
 *     KeyCode.cgKeyCode getter 0x10012423c: table @0x30B71C, tag 5 -> 3
 *       (kVK_ANSI_F).
 *     keyCode(fromWindowsVirtualKey:) letter branch 0x1000b8e20 builds a
 *       1-char String, lowercases, calls KeyCode(rawValue:) -> tag 5.
 *
 * Revert: do NOT inject libqa_patch_wine_gate_v7.dylib. All changes are
 *   in-memory only; nothing on disk, in config.json, in scripts, or in the
 *   app bundle is modified.
 *
 * Safety:
 *   - Target located by CONTENT (3-site wine signature), never by name/index.
 *   - Every patch site has an OLD-byte drift check BEFORE any write.
 *     * If any of the 3 wine sites drift -> ABORT EVERYTHING, write nothing.
 *     * If site 4 (P4a or cave) drifts -> SKIP ONLY SITE 4 (log clearly),
 *       still apply the 3 wine patches.
 *   - Per unique page: make RW (VM_PROT_COPY for Apple Silicon W^X) ->
 *     write -> readback verify -> restore RX -> sys_icache_invalidate.
 *   - Rollback on any readback failure; cave is written BEFORE P4a so the
 *     "b <CAVE>" is never installed pointing at a half-written cave.
 *   - Log is append-mode with pid (shell + child processes all visible).
 */

#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

extern void sys_icache_invalidate(void *start, size_t length);

/* ---- sites 1-3: wine -> yuan (identical to v6) ---- */
static const uint64_t WINE_OFF[3] = {0x1b0868ULL, 0x2b9ee0ULL, 0x2ba06cULL};
static const uint32_t WINE_OLD[2] = {0x528d2ee8u, 0x72acadc8u}; /* "wine" */
static const uint32_t WINE_NEW[2] = {0x528eaf28u, 0x72adcc28u}; /* "yuan" */

/* ---- site 4: VK 1 -> F (offsets are FILE offsets; slide-independent) ---- */
#define V_SITE_OFF    0x0B8F54ULL  /* mov w0,#0x4e (nil sentinel)       */
#define V_CAVE_OFF    0x3BCEE8ULL  /* __TEXT trailing zero padding      */
#define V_LETTER_OFF  0x0B8E20ULL  /* letter branch                     */
#define V_EP_OFF      0x0B8E54ULL  /* common epilogue                   */
#define V_SITE_OLD    0x528009C0u  /* mov w0, #0x4e                     */
#define V_CAVE_WORDS  6           /* 24 bytes of cave code             */
#define V_GUARD_WORDS 8           /* verify 32 bytes of OLD cave = 0   */

/* opcodes fixed, branch words computed from file offsets */
#define OP_CMP_X0_1   0xF100041Fu  /* cmp  x0, #1                       */
#define OP_MOV_W0_NIL 0x528009C0u  /* mov  w0, #0x4e                    */
#define OP_MOV_X0_F   0xD28008C0u  /* mov  x0, #0x46  ('F')             */

static FILE* g_log;
#define LOG(...) do { if (g_log) { fprintf(g_log, "[pid=%d] ", getpid()); \
                                   fprintf(g_log, __VA_ARGS__); fflush(g_log); } } while (0)

/* ARM64 unconditional branch: PC-relative, slide-independent within image. */
static uint32_t enc_b(uint64_t from, uint64_t to) {
    int64_t delta = ((int64_t)to - (int64_t)from) / 4;
    return (uint32_t)(0x14000000u | (uint64_t)(delta & 0x03ffffff));
}
/* ARM64 b.eq (cond=0): range +/-1MB, only used inside the cave. */
static uint32_t enc_beq(uint64_t from, uint64_t to) {
    int64_t delta = ((int64_t)to - (int64_t)from) / 4;
    return (uint32_t)(0x54000000u | (uint64_t)((delta & 0x7ffff) << 5));
}

static uint64_t image_mapped_file_size(const struct mach_header_64* h, int* has_zero) {
    uint64_t max_end = 0;
    *has_zero = 0;
    if (!h || h->magic != MH_MAGIC_64) return 0;
    const uint8_t* p = (const uint8_t*)h + sizeof(struct mach_header_64);
    for (uint32_t c = 0; c < h->ncmds; c++) {
        const struct load_command* lc = (const struct load_command*)p;
        if (lc->cmdsize == 0) break;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64* sg = (const struct segment_command_64*)p;
            if (sg->fileoff == 0) *has_zero = 1;
            uint64_t end = (uint64_t)sg->fileoff + (uint64_t)sg->filesize;
            if (end > max_end) max_end = end;
        }
        p += lc->cmdsize;
    }
    return max_end;
}

/* Try several ways to obtain write access to one page. 0 = success. */
static int make_page_writable(uintptr_t pg, vm_size_t sz, int* strategy) {
    vm_address_t a = (vm_address_t)pg;
    kern_return_t kr;

    kr = vm_protect(mach_task_self(), a, sz, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    LOG("[qa]   protect A (RW)              kr=%d\n", kr);
    if (kr == KERN_SUCCESS) { *strategy = 1; return 0; }

    kr = vm_protect(mach_task_self(), a, sz, TRUE,
                    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    LOG("[qa]   protect B1 (setmax RWX)      kr=%d\n", kr);
    if (kr == KERN_SUCCESS) {
        kr = vm_protect(mach_task_self(), a, sz, FALSE, VM_PROT_READ | VM_PROT_WRITE);
        LOG("[qa]   protect B2 (RW)            kr=%d\n", kr);
        if (kr == KERN_SUCCESS) { *strategy = 2; return 0; }
    }

    kr = vm_protect(mach_task_self(), a, sz, FALSE,
                    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    LOG("[qa]   protect C (RW|COPY)         kr=%d\n", kr);
    if (kr == KERN_SUCCESS) { *strategy = 3; return 0; }

    kr = vm_protect(mach_task_self(), a, sz, TRUE,
                    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    LOG("[qa]   protect D (setmax RW|COPY)  kr=%d\n", kr);
    if (kr == KERN_SUCCESS) {
        kr = vm_protect(mach_task_self(), a, sz, FALSE,
                        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        LOG("[qa]   protect D2 (RW|COPY)       kr=%d\n", kr);
        if (kr == KERN_SUCCESS) { *strategy = 4; return 0; }
    }
    return -1;
}

static void restore_rx(uintptr_t pg, vm_size_t sz) {
    kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)pg, sz, FALSE,
                                  VM_PROT_READ | VM_PROT_EXECUTE);
    LOG("[qa]   restore page 0x%llx RX kr=%d\n", (unsigned long long)pg, kr);
}

static int add_page(uintptr_t* arr, int* n, int cap, uintptr_t pg) {
    for (int i = 0; i < *n; i++) if (arr[i] == pg) return 0;
    if (*n >= cap) return 0;
    arr[(*n)++] = pg;
    return 1;
}

static volatile uint32_t* word_at(const struct mach_header_64* h, uint64_t off) {
    return (volatile uint32_t*)((uintptr_t)h + off);
}

static void log_hex(const char* tag, uint64_t off, const uint32_t* w, int n) {
    if (!g_log) return;
    fprintf(g_log, "[pid=%d] [qa]   %s off=0x%llx :", getpid(), tag,
            (unsigned long long)off);
    for (int i = 0; i < n; i++) {
        const uint8_t* b = (const uint8_t*)&w[i];
        fprintf(g_log, " %02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
    }
    fprintf(g_log, "\n");
    fflush(g_log);
}

__attribute__((constructor))
static void qa_patch(void) {
    g_log = fopen("/tmp/qa_patch_wine_gate.log", "a");
    if (!g_log) return;
    LOG("[qa] ===== v7 constructor entered (t=%ld) =====\n", (long)time(NULL));

    /* Site-4 NEW words, computed from file offsets (ASLR-slide independent). */
    uint32_t v_site_new = enc_b(V_SITE_OFF, V_CAVE_OFF);
    uint32_t v_cave_new[V_CAVE_WORDS];
    v_cave_new[0] = OP_CMP_X0_1;                                  /* cmp x0,#1        */
    v_cave_new[1] = enc_beq(V_CAVE_OFF + 0x04, V_CAVE_OFF + 0x10); /* b.eq vk1    */
    v_cave_new[2] = OP_MOV_W0_NIL;                                /* mov w0,#0x4e     */
    v_cave_new[3] = enc_b(V_CAVE_OFF + 0x0C, V_EP_OFF);           /* b epilogue      */
    v_cave_new[4] = OP_MOV_X0_F;                                  /* mov x0,#0x46     */
    v_cave_new[5] = enc_b(V_CAVE_OFF + 0x14, V_LETTER_OFF);       /* b letter branch */

    /* Explicit plan: every patch site's offset / OLD / NEW. */
    LOG("[qa] v7 PLAN (4 sites):\n");
    for (int s = 0; s < 3; s++) {
        uint32_t old_pair[2] = {WINE_OLD[0], WINE_OLD[1]};
        uint32_t new_pair[2] = {WINE_NEW[0], WINE_NEW[1]};
        log_hex("PLAN wine[%d] OLD", WINE_OFF[s], old_pair, 2);
        log_hex("PLAN wine[%d] NEW", WINE_OFF[s], new_pair, 2);
    }
    {
        uint32_t one_old[1] = {V_SITE_OLD};
        uint32_t one_new[1] = {v_site_new};
        log_hex("PLAN site4 P4a OLD", V_SITE_OFF, one_old, 1);
        log_hex("PLAN site4 P4a NEW", V_SITE_OFF, one_new, 1);
        log_hex("PLAN site4 CAVE NEW", V_CAVE_OFF, v_cave_new, V_CAVE_WORDS);
    }
    LOG("[qa] v7 site4 plan: P4a off=0x%llx OLD=%08x NEW=%08x ; "
        "CAVE off=0x%llx (%d words, guard %d zero words)\n",
        (unsigned long long)V_SITE_OFF, V_SITE_OLD, v_site_new,
        (unsigned long long)V_CAVE_OFF, V_CAVE_WORDS, V_GUARD_WORDS);

    /* Scan threshold is based on WINE sites only, so a too-small/different
       image can still be found and site4 skipped WITHOUT losing wine patches. */
    const uint64_t need = WINE_OFF[2] + 8;
    const uint64_t need_cave = V_CAVE_OFF + V_GUARD_WORDS * 4;
    const struct mach_header_64* hdr = NULL;
    int target = -1, skipped = 0;
    uint32_t cnt = _dyld_image_count();

    /* ---- locate the target image by the 3-site wine CONTENT signature ---- */
    for (uint32_t i = 0; i < cnt; i++) {
        const struct mach_header_64* h =
            (const struct mach_header_64*)_dyld_get_image_header(i);
        if (!h || h->magic != MH_MAGIC_64) { skipped++; continue; }
        int has_zero = 0;
        uint64_t mapped = image_mapped_file_size(h, &has_zero);
        if (!has_zero || mapped < need) { skipped++; continue; }

        LOG("[qa] candidate image[%u]=%s mapped=0x%llx need=0x%llx\n",
            i, _dyld_get_image_name(i), (unsigned long long)mapped,
            (unsigned long long)need);
        int ok_old = 1, ok_new = 1;
        for (int s = 0; s < 3; s++) {
            volatile uint32_t* a = word_at(h, WINE_OFF[s]);
            volatile uint32_t* b = word_at(h, WINE_OFF[s] + 4);
            if (*a != WINE_OLD[0] || *b != WINE_OLD[1]) ok_old = 0;
            if (*a != WINE_NEW[0] || *b != WINE_NEW[1]) ok_new = 0;
        }
        if (ok_new) { hdr = h; target = (int)i; break; }
        if (ok_old) { hdr = h; target = (int)i; break; }
        LOG("[qa]   rejected (3-site wine signature mismatch)\n");
    }
    LOG("[qa] scanned %u images, %d skipped\n", cnt, skipped);

    if (target < 0) {
        LOG("[qa] ABORT: no loaded image carries the 3-site 'wine' signature "
            "(nothing written)\n");
        fclose(g_log);
        return;
    }
    LOG("[qa] target = image[%d] %s base=%p\n", target,
        _dyld_get_image_name(target), (void*)hdr);

    /* ---- PRE-FLIGHT drift checks for ALL sites, before ANY write ---- */
    int wine_state = -1; /* 0=all OLD, 1=all NEW, -1=drift */
    {
        int n_old = 0, n_new = 0;
        for (int s = 0; s < 3; s++) {
            volatile uint32_t* a = word_at(hdr, WINE_OFF[s]);
            volatile uint32_t* b = word_at(hdr, WINE_OFF[s] + 4);
            uint32_t pair[2] = {*a, *b};
            log_hex("WINE OLD/seen", WINE_OFF[s], pair, 2);
            if (*a == WINE_OLD[0] && *b == WINE_OLD[1]) n_old++;
            else if (*a == WINE_NEW[0] && *b == WINE_NEW[1]) n_new++;
        }
        if (n_old == 3) wine_state = 0;
        else if (n_new == 3) wine_state = 1;
        LOG("[qa] wine drift check: 3 sites, old=%d new=%d state=%d\n",
            n_old, n_new, wine_state);
    }
    if (wine_state == -1) {
        LOG("[qa] ABORT: one or more wine sites drifted -> write NOTHING "
            "(per-site OLD check failed)\n");
        fclose(g_log);
        return;
    }

    /* Site-4 state: check P4a + cave (incl. 2 guard words). */
    int v_state = -1; /* 0=all OLD, 1=all NEW, -1=drift */
    const char* v_skip_reason = NULL;
    {
        /* The cave lives high in __TEXT; if this image's mapped file range is
           too short, skip ONLY site4 (never touch out-of-range memory). */
        int hz = 0;
        uint64_t mapped = image_mapped_file_size(hdr, &hz);
        LOG("[qa] site4 mapped-size check: mapped=0x%llx need_cave=0x%llx %s\n",
            (unsigned long long)mapped, (unsigned long long)need_cave,
            mapped >= need_cave ? "OK" : "TOO SMALL");
        if (mapped < need_cave) {
            v_skip_reason = "mapped image too small for cave -> skipped only site4";
            LOG("[qa] WARN: cave offset beyond mapped image -> SKIP ONLY SITE 4\n");
        } else {
            volatile uint32_t* site = word_at(hdr, V_SITE_OFF);
            uint32_t seen_site = *site;
            uint32_t seen_cave[V_GUARD_WORDS];
            for (int i = 0; i < V_GUARD_WORDS; i++)
                seen_cave[i] = *word_at(hdr, V_CAVE_OFF + (uint64_t)i * 4);
            log_hex("SITE4 seen", V_SITE_OFF, &seen_site, 1);
            log_hex("CAVE seen", V_CAVE_OFF, seen_cave, V_GUARD_WORDS);

            int guard_zero = 1;
            for (int i = V_CAVE_WORDS; i < V_GUARD_WORDS; i++)
                if (seen_cave[i] != 0) guard_zero = 0;

            if (guard_zero) {
                if (seen_site == V_SITE_OLD) {
                    int cave_old = 1;
                    for (int i = 0; i < V_CAVE_WORDS; i++)
                        if (seen_cave[i] != 0) cave_old = 0;
                    if (cave_old) v_state = 0;
                } else if (seen_site == v_site_new) {
                    int cave_new = 1;
                    for (int i = 0; i < V_CAVE_WORDS; i++)
                        if (seen_cave[i] != v_cave_new[i]) cave_new = 0;
                    if (cave_new) v_state = 1;
                }
            }
            LOG("[qa] site4 drift check: P4a=0x%08x (old=%08x new=%08x) "
                "guard_zero=%d state=%d\n",
                seen_site, V_SITE_OLD, v_site_new, guard_zero, v_state);
        }
    }

    int do_wine = (wine_state == 0);
    int do_v = (v_state == 0);
    const char* v_reason = "n/a";
    if (v_state == 1) v_reason = "already patched (idempotent)";
    else if (v_skip_reason) v_reason = v_skip_reason;
    else if (v_state == -1) {
        v_reason = "OLD mismatch -> SKIP ONLY SITE 4 (wine patches still applied)";
        LOG("[qa] WARN: site4 OLD bytes do not match the expected unpatched or "
            "already-patched state. SKIP ONLY SITE 4; wine patches proceed.\n");
    }
    LOG("[qa] decision: do_wine=%d do_site4=%d site4_reason=%s\n",
        do_wine, do_v, v_reason);

    if (!do_wine && !do_v) {
        LOG("[qa] OK: nothing to do (wine %s, site4 %s)\n",
            wine_state == 1 ? "already patched" : "state", v_reason);
        fclose(g_log);
        return;
    }

    /* ---- collect UNIQUE pages per group ---- */
    uintptr_t wp[3]; int nw = 0;
    uintptr_t vp[2]; int nv = 0;
    uintptr_t allp[8]; int na = 0;   /* headroom: 3 wine + 2 site4 = 5 unique max */
    size_t pgsz = (size_t)getpagesize();
    if (do_wine) {
        for (int s = 0; s < 3; s++) {
            uintptr_t pg = ((uintptr_t)hdr + WINE_OFF[s]) & ~(uintptr_t)(pgsz - 1);
            if (add_page(wp, &nw, 3, pg)) add_page(allp, &na, 8, pg);
        }
    }
    if (do_v) {
        uintptr_t pg1 = ((uintptr_t)hdr + V_SITE_OFF) & ~(uintptr_t)(pgsz - 1);
        uintptr_t pg2 = ((uintptr_t)hdr + V_CAVE_OFF) & ~(uintptr_t)(pgsz - 1);
        if (add_page(vp, &nv, 2, pg1)) add_page(allp, &na, 8, pg1);
        if (add_page(vp, &nv, 2, pg2)) add_page(allp, &na, 8, pg2);
    }
    LOG("[qa] pages: wine=%d site4=%d total_unique=%d pagesize=%zu\n",
        nw, nv, na, pgsz);

    /* ---- make pages writable: wine failure = ABORT ALL; site4 failure = SKIP 4 ---- */
    if (do_wine) {
        for (int k = 0; k < nw; k++) {
            int strategy = 0;
            LOG("[qa] making wine page[%d]=0x%llx writable\n", k,
                (unsigned long long)wp[k]);
            if (make_page_writable(wp[k], (vm_size_t)pgsz, &strategy) != 0) {
                LOG("[qa] ABORT: wine page protect failed -> write NOTHING\n");
                fclose(g_log);
                return;
            }
            LOG("[qa]   wine page[%d] writable via strategy %d\n", k, strategy);
        }
    }
    if (do_v) {
        for (int k = 0; k < nv; k++) {
            int strategy = 0;
            LOG("[qa] making site4 page[%d]=0x%llx writable\n", k,
                (unsigned long long)vp[k]);
            if (make_page_writable(vp[k], (vm_size_t)pgsz, &strategy) != 0) {
                LOG("[qa] WARN: site4 page protect failed -> SKIP ONLY SITE 4; "
                    "wine patches proceed\n");
                do_v = 0;
                v_reason = "page protect failed -> skipped only site4";
                break;
            }
            LOG("[qa]   site4 page[%d] writable via strategy %d\n", k, strategy);
        }
    }

    /* ---- write sites 1-3 (wine -> yuan), readback + rollback ---- */
    long wine_done = 0;
    if (do_wine) {
        for (int s = 0; s < 3; s++) {
            volatile uint32_t* a = word_at(hdr, WINE_OFF[s]);
            volatile uint32_t* b = word_at(hdr, WINE_OFF[s] + 4);
            uint32_t old_pair[2] = {WINE_OLD[0], WINE_OLD[1]};
            uint32_t new_pair[2] = {WINE_NEW[0], WINE_NEW[1]};
            log_hex("WINE OLD", WINE_OFF[s], old_pair, 2);
            log_hex("WINE NEW", WINE_OFF[s], new_pair, 2);
            *a = WINE_NEW[0];
            *b = WINE_NEW[1];
            if (*a != WINE_NEW[0] || *b != WINE_NEW[1]) {
                LOG("[qa] ABORT: wine readback failed at site %d -> rolled back "
                    "all wine sites, site4 NOT applied\n", s);
                for (int t = 0; t < 3; t++) {
                    *word_at(hdr, WINE_OFF[t]) = WINE_OLD[0];
                    *word_at(hdr, WINE_OFF[t] + 4) = WINE_OLD[1];
                }
                for (int k = 0; k < na; k++) restore_rx(allp[k], (vm_size_t)pgsz);
                fclose(g_log);
                return;
            }
            wine_done++;
            LOG("[qa] wine site[%d] off=0x%llx applied OK\n", s,
                (unsigned long long)WINE_OFF[s]);
        }
    }

    /* ---- write site 4: CAVE FIRST, then P4a. Never install a branch to a
             half-written cave. Rollback restores original OLD bytes. ---- */
    int v_done = 0;
    if (do_v) {
        for (int i = 0; i < V_CAVE_WORDS; i++)
            *word_at(hdr, V_CAVE_OFF + (uint64_t)i * 4) = v_cave_new[i];
        int cave_ok = 1;
        for (int i = 0; i < V_CAVE_WORDS; i++)
            if (*word_at(hdr, V_CAVE_OFF + (uint64_t)i * 4) != v_cave_new[i])
                cave_ok = 0;
        LOG("[qa] site4 CAVE readback %s\n", cave_ok ? "OK" : "FAILED");
        if (cave_ok) {
            *word_at(hdr, V_SITE_OFF) = v_site_new;
            if (*word_at(hdr, V_SITE_OFF) != v_site_new) cave_ok = 0;
            LOG("[qa] site4 P4a readback %s\n", cave_ok ? "OK" : "FAILED");
        }
        if (cave_ok) {
            v_done = 1;
            LOG("[qa] site4 applied: P4a off=0x%llx OLD=%08x NEW=%08x ; "
                "CAVE off=0x%llx OLD=00*32 NEW=24 bytes above\n",
                (unsigned long long)V_SITE_OFF, V_SITE_OLD, v_site_new,
                (unsigned long long)V_CAVE_OFF);
        } else {
            /* rollback site4 only; keep wine patches */
            for (int i = 0; i < V_GUARD_WORDS; i++)
                *word_at(hdr, V_CAVE_OFF + (uint64_t)i * 4) = 0;
            *word_at(hdr, V_SITE_OFF) = V_SITE_OLD;
            LOG("[qa] WARN: site4 readback failed -> rolled back site4 ONLY "
                "(cave zeroed, P4a restored to mov w0,#0x4e); wine kept\n");
        }
    }

    /* ---- restore RX (drop W for W^X) and flush icache ---- */
    for (int k = 0; k < na; k++) restore_rx(allp[k], (vm_size_t)pgsz);
    if (wine_done)
        for (int s = 0; s < 3; s++)
            sys_icache_invalidate((void*)((uintptr_t)hdr + WINE_OFF[s]), 8);
    if (v_done) {
        sys_icache_invalidate((void*)((uintptr_t)hdr + V_SITE_OFF), 4);
        sys_icache_invalidate((void*)((uintptr_t)hdr + V_CAVE_OFF),
                              V_CAVE_WORDS * 4);
    }

    LOG("[qa] ===== v7 RESULT: wine=%ld/3 %s, site4=%s (%s) =====\n",
        wine_done,
        wine_done ? "APPLIED" : (wine_state == 1 ? "already patched" : "not applicable"),
        v_done ? "APPLIED" : "SKIPPED",
        v_done ? "VK1->F active" : v_reason);
    LOG("[qa] revert: do not inject libqa_patch_wine_gate_v7.dylib "
        "(all edits are in-memory only)\n");
    fclose(g_log);
}
