// SPDX-License-Identifier: GPL-2.0
/*
 * auto_classifier_kern.c — arm64-mthp-autopilot v3.0
 * Target: Raspberry Pi 5 (BCM2712, Cortex-A76), Linux 6.12
 *
 * ═══════════════════════════════════════════════════════════════════
 * DESIGN PRINCIPLE: REGION FILL DENSITY
 * ═══════════════════════════════════════════════════════════════════
 *
 * Reference: "eBPF-mm: Userspace-guided memory management in Linux
 * with eBPF", Mores et al., arXiv:2409.11220v1
 *
 * The paper says huge-page promotion should be driven by ACCESS
 * FREQUENCY — not allocation size, not process name.  Promote when
 * the benefit (TLB reach improvement) outweighs the cost (zeroing +
 * compaction).
 *
 * Our proxy for access frequency at fault time:
 *
 *   REGION FILL COUNT — the number of distinct 4KB pages that have
 *   been major-faulted within a 2MB-aligned region.
 *
 *   Why this works:
 *     A 2MB region contains 512 4KB pages.
 *     A major fault means the page was not present — first access.
 *     Each page can only generate one major fault (until eviction).
 *     So fill_count = distinct pages touched = width of the access.
 *
 *     A sparse 2MB region with 3 pages faulted: probably not worth
 *     promoting.  A dense region with 400 pages faulted: sequential
 *     walk in progress — hugepage will be fully used.
 *
 *   This directly captures what the paper calls access frequency
 *   without needing DAMON: we observe it at fault time, inline.
 *
 * PROMOTION SCHEDULE (per 2MB region):
 *
 *   fill_count == 1 (first fault)  -> 4KB
 *     Zero evidence yet.  Cost > benefit.  Return without promoting.
 *
 *   fill_count >= FILL_WARM (2)    -> 16KB
 *     Second fault to the region.  Contiguous PTE x4.  Near-zero
 *     compaction cost.  Start collecting TLB benefit immediately.
 *
 *   fill_count >= FILL_HOT  (16)   -> 64KB
 *     16 of 512 pages faulted = 3% fill.  Sequential pattern is clear.
 *     Contiguous PTE x16 = 16x TLB reach on Cortex-A76.
 *
 *   fill_count >= FILL_VHOT (64)   -> 2MB
 *     64 of 512 pages faulted = 12.5% fill.  PMD-level promotion.
 *     At this point 87% of the remaining first-pass faults get 2MB.
 *
 * FOR YOLO (111MB weight tensor, 56 x 2MB regions, 512 faults/region):
 *   fault 1:  4KB (no evidence)
 *   faults 2-15:   16KB
 *   faults 16-63:  64KB
 *   faults 64-511: 2MB  <- 87% of the first pass at full PMD size
 *
 *   Works identically for darknet, onnxruntime, my_custom_model,
 *   or any binary name.  Zero application code modification needed.
 *
 * KEY STRUCTURAL CHANGE vs v2.0 and v3.0-draft:
 *
 *   hint_map is now keyed by (pid << 32) | (addr >> 21) — per region.
 *   v2.0 used hint_map[pid] — a single per-process order that caused
 *   a hot region's order to contaminate brand-new cold regions.
 *   Per-region keying means each 2MB window earns its own order
 *   independently.
 *
 * REMOVED vs v2.0:
 *   - classify_comm() — process name matching
 *   - classify_vma()  — VMA size ladder
 *   - APPCLASS_*      — application categories
 *   - hot_region_adjust() — replaced by fill_count in density_map
 *   - early-exit for GENERIC — no categories, no early exit
 *
 * KEPT:
 *   - buddy_guard_ok()  — cost model, unchanged
 *   - vma_cache         — skip bpf_find_vma() on repeated faults
 *   - system_map        — live frag_index from userspace every 250ms
 *   - direct_reclaim guard — back off under memory pressure
 *   - stack / exec exclusion — these are never sequentially faulted
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "profile.h"

/* ── Fill-density promotion thresholds ──────────────────────── */
/*
 * Per 2MB region (512 x 4KB pages):
 *   FILL_WARM: 2nd fault  → 16KB  (any repeated access = start promoting)
 *   FILL_HOT:  16th fault → 64KB  (3% fill = clearly sequential)
 *   FILL_VHOT: 64th fault → 2MB   (12.5% fill = PMD worth promoting)
 */
#define FILL_WARM   2
#define FILL_HOT   16
#define FILL_VHOT  64

/* ── Per-region density record ──────────────────────────────── */
struct region_density {
    __u32 pid;
    __u32 fill_count;    /* distinct 4KB pages faulted in this 2MB region */
    __u64 region_base;   /* 2MB-aligned base                              */
    __u32 current_order; /* last chosen order, for observability          */
    __u32 _pad;
};

/* ── VMA scratch: bpf_find_vma callback result ──────────────── */
struct vma_scratch {
    __u64 vm_start;
    __u64 vm_end;
    __u64 vm_flags;
    __u8  has_file;
    __u8  found;
    __u8  _pad[6];
};

/* ── VMA cache entry ────────────────────────────────────────── */
struct vma_cache_entry {
    __u32 pid;
    __u32 hit_count;
    __u64 vma_start;
    __u64 vma_end;
    __u64 vm_flags;
    __u8  has_file;
    __u8  is_stack;
    __u8  is_exec;
    __u8  _pad[5];
};

/* ── Maps ───────────────────────────────────────────────────── */

/*
 * density_map — the ONLY promotion signal.
 * Key   = (pid << 32) | (addr >> 21)  [2MB-aligned region per process]
 * Value = region_density.fill_count drives the order decision.
 */
struct {
    __uint(type,        BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 16384);   /* 8192 regions x 2 for headroom */
    __type(key,         __u64);
    __type(value,       struct region_density);
} density_map SEC(".maps");

/*
 * hint_map — output to the kernel THP path.
 *
 * KEY CHANGE vs v2.0: keyed by (pid<<32)|(addr>>21), NOT by pid alone.
 * Per-region keying means each 2MB window gets the order it has earned.
 * A hot tensor region at ORDER_2MB does not force fresh stack regions
 * to also request ORDER_2MB on their first fault.
 *
 * The kernel hook (mm/huge_memory.c) must read this map with the same
 * key: (current->pid << 32) | (fault_addr >> 21).
 */
struct {
    __uint(type,        BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 16384);
    __type(key,         __u64);   /* (pid << 32) | (addr >> 21) */
    __type(value,       __u32);   /* chosen order               */
} hint_map SEC(".maps");

/* system_map — live buddy/frag state from userspace every 250ms */
struct {
    __uint(type,        BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key,         __u32);
    __type(value,       struct system_info);
} system_map SEC(".maps");

/* vma_cache — skip bpf_find_vma() on repeated faults to same VMA */
struct {
    __uint(type,        BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, VMA_CACHE_MAX_ENTRIES);
    __type(key,         __u64);
    __type(value,       struct vma_cache_entry);
} vma_cache SEC(".maps");

/* vma_scratch_map — bpf_find_vma writes here via callback */
struct {
    __uint(type,        BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2048);
    __type(key,         __u32);
    __type(value,       struct vma_scratch);
} vma_scratch_map SEC(".maps");

/* classify_rb — promoted fault events to userspace dashboard */
struct {
    __uint(type,        BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 512 * 1024);
} classify_rb SEC(".maps");

/* ── bpf_find_vma callback ──────────────────────────────────── */
static int vma_fill_callback(struct task_struct *task,
                             struct vm_area_struct *vma,
                             void *callback_ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    struct vma_scratch sc = {};
    sc.vm_start = BPF_CORE_READ(vma, vm_start);
    sc.vm_end   = BPF_CORE_READ(vma, vm_end);
    sc.vm_flags = (__u64)BPF_CORE_READ(vma, vm_flags);
    sc.has_file = (BPF_CORE_READ(vma, vm_file) != NULL) ? 1 : 0;
    sc.found    = 1;
    bpf_map_update_elem(&vma_scratch_map, &pid, &sc, BPF_ANY);
    return 0;
}

/* ── Cost model: buddy allocator guard ──────────────────────── */
/*
 * Paper: "compute the cost of promoting a huge page from real time
 * system data."
 *
 * Returns true if free contiguous blocks of this order are plentiful
 * enough that promotion is unlikely to trigger expensive compaction.
 *
 * Reads live buddy data from system_map (written by the userspace
 * loader from /proc/buddyinfo every 250ms).
 *
 * Three tiers on frag_index (0=clean, 100=fragmented):
 *   <30  → permissive  (compaction cost low)
 *   30-70 → moderate
 *   >70  → conservative (compaction would be expensive, step down)
 */
static __always_inline bool
buddy_guard_ok(__u32 order, struct system_info *sys)
{
    if (order == 0)  return true;
    if (order >= 14) return false;

    __u64 free  = sys->buddy_free[order];
    __u64 total = sys->total_pages;
    if (!total) return true;

    __u32 frag = sys->frag_index;
    __u64 threshold;

    if (order >= ORDER_2MB) {
        threshold = total / (frag < 30 ? BUDDY_FRAC_2MB_LO  :
                             frag < 70 ? BUDDY_FRAC_2MB_MED  :
                                         BUDDY_FRAC_2MB_HI);
    } else if (order >= ORDER_64KB) {
        threshold = total / (frag < 30 ? BUDDY_FRAC_64KB_LO  :
                             frag < 70 ? BUDDY_FRAC_64KB_MED  :
                                         BUDDY_FRAC_64KB_HI);
    } else {
        /* 16KB: sub-PMD, no compaction, always near-free */
        threshold = total / 10000;
    }

    return free >= threshold;
}

/* ── Benefit model: fill count → page order ─────────────────── */
/*
 * This is the entire promotion policy.
 *
 * fill_count = number of distinct 4KB pages faulted in this 2MB region.
 * Since each page can only generate one major fault, fill_count is a
 * direct measure of how much of the 2MB region has been accessed.
 *
 * This replaces classify_vma() + classify_comm() + hot_region_adjust()
 * from v2.0.  Process name and VMA size are not consulted.
 */
static __always_inline __u32
fill_to_order(__u32 fill_count)
{
    if (fill_count >= FILL_VHOT) return ORDER_2MB;    /* >= 64 pages  */
    if (fill_count >= FILL_HOT)  return ORDER_64KB;   /* >= 16 pages  */
    if (fill_count >= FILL_WARM) return ORDER_16KB;   /* >=  2 pages  */
    return ORDER_4KB;                                  /* first fault  */
}

/* ── Main page fault handler ────────────────────────────────── */
SEC("kprobe/handle_mm_fault")
int auto_classify_fault(struct pt_regs *ctx)
{
    __u32 pid  = bpf_get_current_pid_tgid() >> 32;
    __u64 addr = (unsigned long)PT_REGS_PARM2(ctx);
    __u32 zero = 0;

    struct system_info *sys = bpf_map_lookup_elem(&system_map, &zero);
    if (!sys) return 0;

    /* Under direct reclaim: kernel is reclaiming pages to satisfy
     * allocations.  Promoting right now would compete for the same
     * contiguous blocks the reclaim path is trying to free.  Back off. */
    if (sys->direct_reclaim_active)
        return 0;

    /* Region key: same for both density_map and hint_map */
    __u64 rkey = ((__u64)pid << 32) | (addr >> 21);

    /* ── Step 1: VMA type — stack / exec exclusions ────────── */
    bool is_stack = false;
    bool is_exec  = false;
    bool is_file  = false;
    __u64 vma_start = 0, vma_end = 0;

    struct vma_cache_entry *vc = bpf_map_lookup_elem(&vma_cache, &rkey);
    if (vc && addr >= vc->vma_start && addr < vc->vma_end &&
        vc->hit_count < VMA_CACHE_TTL) {
        vc->hit_count++;
        is_stack  = vc->is_stack;
        is_exec   = vc->is_exec;
        is_file   = vc->has_file;
        vma_start = vc->vma_start;
        vma_end   = vc->vma_end;
    } else {
        struct task_struct *task = bpf_get_current_task_btf();
        struct vma_scratch empty = {};
        bpf_map_update_elem(&vma_scratch_map, &pid, &empty, BPF_ANY);
        if (bpf_find_vma(task, addr, vma_fill_callback, NULL, 0) != 0)
            return 0;
        struct vma_scratch *sc = bpf_map_lookup_elem(&vma_scratch_map, &pid);
        if (!sc || !sc->found) return 0;

        is_stack  = (sc->vm_flags & (1ULL << 8)) != 0;
        is_exec   = (sc->vm_flags & (1ULL << 2)) != 0;
        is_file   = sc->has_file;
        vma_start = sc->vm_start;
        vma_end   = sc->vm_end;

        struct vma_cache_entry nvc = {
            .pid = pid, .hit_count = 0,
            .vma_start = vma_start, .vma_end = vma_end,
            .vm_flags  = sc->vm_flags,
            .has_file  = sc->has_file,
            .is_stack  = is_stack ? 1 : 0,
            .is_exec   = is_exec  ? 1 : 0,
        };
        bpf_map_update_elem(&vma_cache, &rkey, &nvc, BPF_ANY);
    }

    /*
     * Stacks grow one page at a time by definition — fill_count will
     * never build up.  Exec pages benefit from iTLB, not dTLB, and
     * the kernel handles them via khugepaged collapse.
     */
    if (is_stack || is_exec)
        return 0;

    /* ── Step 2: Update fill count for this 2MB region ─────── */
    struct region_density *rd = bpf_map_lookup_elem(&density_map, &rkey);
    if (!rd) {
        /*
         * First fault to this region.
         *
         * We have zero evidence of access density.  Insert the record
         * and return without writing a hint.  The kernel takes its own
         * default path (standard mTHP policy) for this one fault.
         * The next fault to this region (fill_count=2) gets 16KB.
         */
        struct region_density nrd = {
            .pid           = pid,
            .fill_count    = 1,
            .region_base   = addr & ~((2ULL * 1024 * 1024) - 1),
            .current_order = ORDER_4KB,
        };
        bpf_map_update_elem(&density_map, &rkey, &nrd, BPF_ANY);
        return 0;
    }

    rd->fill_count++;

    /* ── Step 3: Benefit — fill count → ideal order ─────────── */
    __u32 ideal = fill_to_order(rd->fill_count);

    /*
     * File-backed read-write: cap at 64KB.
     * Large writable file pages hit expensive write-protect faults
     * on copy-on-write if promoted to 2MB.
     * File-backed read-only (mmapped model weights, tensors from disk)
     * are treated the same as anonymous — density drives promotion.
     */
    if (is_file && ideal > ORDER_64KB)
        ideal = ORDER_64KB;

    /* ── Step 4: Cost — buddy guard ─────────────────────────── */
    __u32 order = ideal;

    if (order >= ORDER_2MB  && !buddy_guard_ok(ORDER_2MB,  sys))
        order = ORDER_64KB;
    if (order >= ORDER_64KB && !buddy_guard_ok(ORDER_64KB, sys))
        order = ORDER_16KB;
    if (order >= ORDER_16KB && !buddy_guard_ok(ORDER_16KB, sys))
        order = ORDER_4KB;

    /* Hard cap under extreme fragmentation */
    if (sys->frag_index > 90 && order > ORDER_16KB)
        order = ORDER_16KB;

    rd->current_order = order;

    /* ── Step 5: Write per-region hint ──────────────────────── */
    /*
     * Key = (pid << 32) | (addr >> 21) — same as density_map.
     * The kernel hook reads this same key for the faulting address.
     *
     * Storing per-region (not per-pid) means:
     *   - A hot tensor region at ORDER_2MB stays at ORDER_2MB
     *   - A fresh heap region at fill_count=3 gets ORDER_16KB
     *   - They do not interfere with each other
     */
    if (order > ORDER_4KB)
        bpf_map_update_elem(&hint_map, &rkey, &order, BPF_ANY);

    /* ── Step 6: Emit event for userspace dashboard ──────────── */
    if (order > ORDER_4KB) {
        struct classify_event *ev =
            bpf_ringbuf_reserve(&classify_rb, sizeof(*ev), 0);
        if (ev) {
            ev->pid          = pid;
            ev->address      = addr;
            ev->vma_start    = vma_start;
            ev->vma_end      = vma_end;
            ev->vma_flags    = 0;
            ev->chosen_order = order;
            ev->app_class    = rd->fill_count;  /* repurposed: fill count */
            ev->frag_pct     = sys->frag_index;
            ev->fault_flags  = FAULT_FL_PROMOTED |
                               (order < ideal ? FAULT_FL_BUDDY_GUARD : 0);
            bpf_get_current_comm(ev->comm, sizeof(ev->comm));
            bpf_ringbuf_submit(ev, 0);
        }
    }

    return 0;
}

/* ── Process exit: clean up per-pid hints ───────────────────── */
/*
 * We cannot efficiently iterate hint_map by pid since it is now
 * keyed by (pid, region).  The LRU eviction handles aging.
 * We do clean vma_scratch_map which is still per-pid.
 */
SEC("tracepoint/sched/sched_process_exit")
int handle_exit(void *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    bpf_map_delete_elem(&vma_scratch_map, &pid);
    /* hint_map, density_map, vma_cache: LRU — age out automatically */
    return 0;
}

/* ── Direct reclaim tracking ─────────────────────────────────── */
SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_begin")
int handle_reclaim_begin(void *ctx)
{
    __u32 zero = 0;
    struct system_info *sys = bpf_map_lookup_elem(&system_map, &zero);
    if (sys) sys->direct_reclaim_active = 1;
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_end")
int handle_reclaim_end(void *ctx)
{
    __u32 zero = 0;
    struct system_info *sys = bpf_map_lookup_elem(&system_map, &zero);
    if (sys) sys->direct_reclaim_active = 0;
    return 0;
}

char _license[] SEC("license") = "GPL";
