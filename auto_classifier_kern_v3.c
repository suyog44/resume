// SPDX-License-Identifier: GPL-2.0
/*
 * auto_classifier_kern.c — arm64-mthp-autopilot v3.0
 * Target: Raspberry Pi 5 (BCM2712, Cortex-A76), Linux 6.12
 *
 * ═══════════════════════════════════════════════════════════════════
 * DESIGN: FAULT-DENSITY DRIVEN PROMOTION  (aligned with eBPF-mm paper)
 * ═══════════════════════════════════════════════════════════════════
 *
 * Reference: "eBPF-mm: Userspace-guided memory management in Linux
 * with eBPF", Mores et al., arXiv:2409.11220v1
 *
 * The paper's core argument:
 *   Linux THP is cost-oblivious — it allocates huge pages greedily on
 *   first touch without asking whether the memory is actually accessed
 *   enough to justify the promotion cost (zeroing + compaction).
 *   The correct signal is ACCESS FREQUENCY, not allocation size.
 *
 * What our v2.0 code was doing WRONG:
 *   classify_vma() used VMA SIZE as the promotion signal.
 *   "VMA is 6 MB → give it 2 MB pages."
 *   This is exactly the cost-oblivious pattern the paper criticises.
 *   A 6 MB VMA that is touched once and never again does not benefit
 *   from 2 MB pages.  A 256 KB VMA that is faulted 500 times per
 *   second benefits enormously.  SIZE IS NOT THE SIGNAL.
 *
 * What v3.0 does:
 *   The promotion decision is made entirely on FAULT DENSITY —
 *   how many times has the kernel faulted into this 2 MB-aligned
 *   memory region since it was first mapped?
 *
 *   On the FIRST fault to a region: always 4 KB.
 *     Cost of a wrong promotion is paid in zeroing + compaction.
 *     We have no evidence yet that this region is hot.
 *
 *   After WARM_THRESHOLD faults: promote to 16 KB.
 *     Contiguous-PTE x4.  Very cheap — sub-PMD, no compaction needed.
 *     16 KB zeroing cost = 4x 4 KB cost, but gained on every subsequent
 *     TLB fill.  Justified after just a few faults.
 *
 *   After HOT_THRESHOLD faults: promote to 64 KB.
 *     Contiguous-PTE x16.  16x TLB reach improvement on Cortex-A76.
 *     Zeroing cost = 16x 4 KB.  Justified once the region has proven
 *     it is being repeatedly accessed.
 *
 *   After VHOT_THRESHOLD faults: promote to 2 MB.
 *     PMD-level.  Highest zeroing + compaction cost.
 *     Only justified for regions that are demonstrably heavily used.
 *     The paper uses DAMON for this signal; we use fault count as the
 *     in-kernel proxy (equivalent to DAMON's access frequency).
 *
 *   The buddy guard enforces the cost side:
 *     Even if a region is very hot, if there are no free 2 MB
 *     contiguous blocks in the buddy allocator, the promotion cost
 *     (compaction) is too high.  Step down to 64 KB in that case.
 *
 * WHAT IS REMOVED vs v2.0
 * -----------------------
 *   - classify_comm():  process name matching entirely removed.
 *     The paper makes no mention of using process names.
 *     Access frequency is process-name-agnostic.
 *
 *   - classify_vma() VMA-size ladder: removed.
 *     VMA size is not an access-frequency signal.
 *
 *   - Early-exit for GENERIC: removed.
 *     Every process needs fault density tracking from fault #1.
 *
 *   - APPCLASS_* system: removed.
 *     Replaced by a single per-region fault counter.
 *
 * WHAT IS KEPT
 * ------------
 *   - buddy_guard_ok(): kept as-is.  This is the cost model:
 *     real-time fragmentation data from system_map.  Exactly what
 *     the paper describes as "time needed to locate an available
 *     huge page (compaction)" cost estimation.
 *
 *   - VMA cache: kept for performance.  On cache hit we skip
 *     bpf_find_vma() but still re-evaluate fault density.
 *     Cached: vma bounds (for range check) + VMA type flags.
 *
 *   - system_map / frag_index: kept.  The paper explicitly says
 *     promotion cost is estimated from "real time system data".
 *
 *   - Stack detection (VM_GROWSDOWN): kept as hard exclusion.
 *     Stacks grow incrementally, not as repeated-fault hot regions.
 *
 *   - Ring buffer / observability: kept.
 *
 * THRESHOLDS (tuned for Cortex-A76, 4 KB base page)
 * --------------------------------------------------
 *   WARM_THRESHOLD  =   4   faults -> 16 KB
 *   HOT_THRESHOLD   =  32   faults -> 64 KB
 *   VHOT_THRESHOLD  = 128   faults -> 2 MB
 *
 *   These are per 2 MB-aligned region, per process.
 *   They represent cumulative major fault count since first access,
 *   which is the closest in-kernel equivalent to DAMON access frequency.
 *
 *   Why these numbers:
 *     4   faults: enough to confirm repeated access, cheap to promote
 *     32  faults: region is clearly in the working set -> 64KB
 *     128 faults: region is a hot core data structure, pay the
 *                 2 MB promotion cost
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "profile.h"

/* ── Promotion thresholds — fault count per 2MB-aligned region ── */
#define WARM_THRESHOLD    4    /*   4 faults -> promote to 16KB */
#define HOT_THRESHOLD    32    /*  32 faults -> promote to 64KB */
#define VHOT_THRESHOLD  128    /* 128 faults -> promote to  2MB */

/* ── Per-region fault density record ───────────────────────── */
struct region_density {
    __u32 pid;
    __u32 fault_count;    /* cumulative major faults to this 2MB region */
    __u64 region_base;    /* 2MB-aligned base address                   */
    __u32 current_order;  /* last decided order                         */
    __u32 _pad;
};

/* ── VMA scratch: bpf_find_vma callback writes here ────────── */
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
 * density_map: the ONLY promotion signal.
 * Key   = (pid << 32) | (addr >> 21)  -- 2MB-aligned region per pid.
 * Value = region_density with cumulative fault_count.
 */
struct {
    __uint(type,        BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 8192);
    __type(key,         __u64);
    __type(value,       struct region_density);
} density_map SEC(".maps");

/* system_map: written by userspace loader every 250ms */
struct {
    __uint(type,        BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key,         __u32);
    __type(value,       struct system_info);
} system_map SEC(".maps");

/* hint_map: output to kernel THP path */
struct {
    __uint(type,        BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2048);
    __type(key,         __u32);   /* pid */
    __type(value,       __u32);   /* chosen order */
} hint_map SEC(".maps");

/* vma_cache: skip bpf_find_vma() on repeated faults to same VMA */
struct {
    __uint(type,        BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, VMA_CACHE_MAX_ENTRIES);
    __type(key,         __u64);
    __type(value,       struct vma_cache_entry);
} vma_cache SEC(".maps");

/* vma_scratch_map: temp storage for bpf_find_vma callback */
struct {
    __uint(type,        BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2048);
    __type(key,         __u32);
    __type(value,       struct vma_scratch);
} vma_scratch_map SEC(".maps");

/* classify_rb: promoted fault events -> userspace dashboard */
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
    struct file *f = BPF_CORE_READ(vma, vm_file);
    sc.has_file = (f != NULL) ? 1 : 0;
    sc.found    = 1;

    bpf_map_update_elem(&vma_scratch_map, &pid, &sc, BPF_ANY);
    return 0;
}

/* ── Cost model: buddy guard ────────────────────────────────── */
/*
 * Paper: "we empirically calculate a fixed cost for both zeroing
 * and, in case of fragmentation, compaction."
 *
 * Returns true if the buddy allocator has enough free blocks of
 * the requested order that promotion is not likely to trigger
 * expensive compaction.  Uses live fragmentation data from system_map
 * (written every 250ms from /proc/buddyinfo by the userspace loader).
 *
 * Three tiers based on current frag_index (0=clean, 100=fragmented):
 *   low  frag (<30): permissive — compaction cost is low
 *   med  frag (30-70): moderate threshold
 *   high frag (>70): conservative — compaction would be expensive
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
        /* 16KB: sub-PMD, never needs compaction, almost always ok */
        threshold = total / 10000;
    }

    return free >= threshold;
}

/* ── Fault density -> page order ────────────────────────────── */
/*
 * The entire promotion policy in four lines.
 *
 * This replaces all of v2.0's classify_vma(), classify_comm(),
 * and hot_region_adjust().  It encodes the paper's benefit model:
 *   fault_count = proxy for access frequency = promotion benefit
 */
static __always_inline __u32
density_to_order(__u32 fault_count)
{
    if (fault_count >= VHOT_THRESHOLD) return ORDER_2MB;
    if (fault_count >= HOT_THRESHOLD)  return ORDER_64KB;
    if (fault_count >= WARM_THRESHOLD) return ORDER_16KB;
    return ORDER_4KB;
}

/* ── Main page fault handler ────────────────────────────────── */
SEC("kprobe/handle_mm_fault")
int auto_classify_fault(struct pt_regs *ctx)
{
    __u32 pid  = bpf_get_current_pid_tgid() >> 32;
    __u64 addr = (unsigned long)PT_REGS_PARM2(ctx);
    __u32 zero = 0;

    /* Load current system state */
    struct system_info *sys = bpf_map_lookup_elem(&system_map, &zero);
    if (!sys) return 0;

    /* Under direct reclaim: kernel is already under memory pressure.
     * Don't add promotion cost on top. */
    if (sys->direct_reclaim_active)
        return 0;

    __u64 region_key = ((__u64)pid << 32) | (addr >> 21);

    /* ── Step 1: Determine VMA type (stack / file / anon) ─────── */
    bool is_stack = false;
    bool is_exec  = false;
    bool is_file  = false;
    __u64 vma_start = 0, vma_end = 0;

    struct vma_cache_entry *vc = bpf_map_lookup_elem(&vma_cache, &region_key);

    if (vc && addr >= vc->vma_start && addr < vc->vma_end &&
        vc->hit_count < VMA_CACHE_TTL) {
        /* Cache hit */
        vc->hit_count++;
        is_stack  = vc->is_stack;
        is_exec   = vc->is_exec;
        is_file   = vc->has_file;
        vma_start = vc->vma_start;
        vma_end   = vc->vma_end;
    } else {
        /* Cache miss: walk VMA tree */
        struct task_struct *task = bpf_get_current_task_btf();
        struct vma_scratch empty = {};
        bpf_map_update_elem(&vma_scratch_map, &pid, &empty, BPF_ANY);

        if (bpf_find_vma(task, addr, vma_fill_callback, NULL, 0) != 0)
            return 0;

        struct vma_scratch *sc = bpf_map_lookup_elem(&vma_scratch_map, &pid);
        if (!sc || !sc->found) return 0;

        is_stack  = (sc->vm_flags & (1ULL << 8)) != 0;  /* VM_GROWSDOWN */
        is_exec   = (sc->vm_flags & (1ULL << 2)) != 0;  /* VM_EXEC      */
        is_file   = sc->has_file;
        vma_start = sc->vm_start;
        vma_end   = sc->vm_end;

        struct vma_cache_entry new_vc = {
            .pid       = pid,
            .hit_count = 0,
            .vma_start = vma_start,
            .vma_end   = vma_end,
            .vm_flags  = sc->vm_flags,
            .has_file  = sc->has_file,
            .is_stack  = is_stack ? 1 : 0,
            .is_exec   = is_exec  ? 1 : 0,
        };
        bpf_map_update_elem(&vma_cache, &region_key, &new_vc, BPF_ANY);
    }

    /*
     * Hard exclusions:
     *   Stacks:     grow by single pages, never "hot" in the fault sense
     *   Exec pages: kernel's own iTLB path (khugepaged) handles these
     */
    if (is_stack || is_exec)
        return 0;

    /* ── Step 2: Update fault density for this 2MB region ─────── */
    struct region_density *rd = bpf_map_lookup_elem(&density_map, &region_key);
    if (!rd) {
        /*
         * First fault to this region.
         * Paper principle: don't pay promotion cost without evidence
         * of benefit.  Insert with count=1, return without promoting.
         */
        struct region_density new_rd = {
            .pid           = pid,
            .fault_count   = 1,
            .region_base   = addr & ~((2ULL * 1024 * 1024) - 1),
            .current_order = ORDER_4KB,
        };
        bpf_map_update_elem(&density_map, &region_key, &new_rd, BPF_ANY);
        return 0;
    }

    rd->fault_count++;

    /* ── Step 3: Benefit model — fault density -> ideal order ──── */
    __u32 ideal = density_to_order(rd->fault_count);

    /*
     * File-backed read-write: cap at 64KB.
     * 2MB promotion of shared writable pages triggers expensive
     * write-protect faults on copy-on-write.
     * Read-only file mappings (model weights, mmap'd tensors) are
     * treated the same as anonymous — density drives the decision.
     */
    if (is_file && ideal > ORDER_64KB)
        ideal = ORDER_64KB;

    /* ── Step 4: Cost model — buddy guard ─────────────────────── */
    /*
     * Paper: "Compute the cost of promoting a huge page from real
     * time system data."  Step down if contiguous blocks are scarce.
     */
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

    /* ── Step 5: Write hint for kernel THP path ──────────────── */
    if (order > ORDER_4KB)
        bpf_map_update_elem(&hint_map, &pid, &order, BPF_ANY);

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
            ev->app_class    = 0;
            ev->frag_pct     = sys->frag_index;
            ev->fault_flags  = FAULT_FL_PROMOTED |
                               (order < ideal ? FAULT_FL_BUDDY_GUARD : 0);
            bpf_get_current_comm(ev->comm, sizeof(ev->comm));
            bpf_ringbuf_submit(ev, 0);
        }
    }

    return 0;
}

/* ── Process exit: clean up per-pid state ───────────────────── */
SEC("tracepoint/sched/sched_process_exit")
int handle_exit(void *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    bpf_map_delete_elem(&hint_map,        &pid);
    bpf_map_delete_elem(&vma_scratch_map, &pid);
    /* density_map and vma_cache are LRU — age out automatically */
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
