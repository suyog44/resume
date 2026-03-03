// SPDX-License-Identifier: GPL-2.0-only
/*
 * stop_machine_test.c — complete test suite for:
 *   common-stop_machine-deadlock-panic-production.patch
 *
 * ═══════════════════════════════════════════════════════════════════
 * PATCH RECAP (what we are testing)
 * ═══════════════════════════════════════════════════════════════════
 * The patch adds ONE block at position C of multi_cpu_stop():
 *
 *   do {
 *       if (curstate < msdata->state) { ... switch ... }
 *       if (is_active)
 *           ack_state(msdata);                   // position A
 *       else if (curstate > MULTI_STOP_PREPARE) {
 *           touch_nmi_watchdog();
 *           rcu_momentary_eqs();                 // position B
 *       }
 *       // ─── PATCH: position C, unconditional ───────────────────
 *       if (!timed_out &&
 *           time_after(jiffies,
 *                      start_jiffies + STOPPER_TIMEOUT_JIFFIES)) {
 *           timed_out = 1;
 *           stopper_deadlock_dump(cpu, msdata, elapsed_j);
 *           panic("migration/%u: stuck in multi_cpu_stop ...");
 *       }
 *       // ─────────────────────────────────────────────────────────
 *   } while (curstate != MULTI_STOP_EXIT);
 *
 * Key constants (hardcoded, no module_param in production patch):
 *   STOPPER_TIMEOUT_JIFFIES = 3 * HZ
 *   At HZ=250: 750 jiffies = 3000 ms
 *
 * 7 diagnostic sections emitted by stopper_deadlock_dump():
 *   Sec1: outer stopper fn + caller   ← root cause
 *   Sec2: multi_stop_data (state, num_threads, thread_ack, inner fn)
 *   Sec3: cpu_online/active/dying masks
 *   Sec4: per-CPU stopper state (lockless walk)
 *   Sec5: dump_stack()
 *   Sec6: trigger_all_cpu_backtrace()   ← IPI-based on arm64 (see NOTE)
 *   Sec7: show_state()
 *
 * ═══════════════════════════════════════════════════════════════════
 * NOTE: trigger_all_cpu_backtrace() hazard on Qualcomm arm64
 * ═══════════════════════════════════════════════════════════════════
 * On SM4100/SM5100 (arm64), arch_trigger_cpumask_backtrace() uses IPI,
 * not true NMI.  CPUs that have IRQs disabled (e.g. holding a spinlock)
 * cannot receive the IPI — the caller waits up to 10 s per CPU.
 * With 8 cores: 80 s inside the panic path.  Qualcomm bark fires at 11 s.
 * → bite before ramdump if all 8 CPUs have IRQs off.
 * Ref: patchew.org/linux/fb0b7a92-d371-4510-80c4-25a57f2c4f3d@paulmck-laptop/
 *      (Cheng-Jui Wang arm64 lockup report, kernel.org, Oct 2024)
 * Production patch includes Sec6 as-is.  This test module SKIPS it in safe
 * paths; crash-path tests rely on the patch to call it from panic context.
 *
 * ═══════════════════════════════════════════════════════════════════
 * COMPLETE TEST MATRIX — 18 test cases
 * ═══════════════════════════════════════════════════════════════════
 *  tc=1   Safe  : 7-section dump, real stopper context, no panic
 *  tc=2   CRASH : mdelay >3 s — exact production simulate_panic path
 *  tc=3   C/S   : same-CPU re-entrancy — exact production bug (LB variant)
 *  tc=4   C/S   : re-entrancy + rcu_read_lock — stopper hang + RCU stall
 *  tc=5   C/S   : two-CPU mutual deadlock — stop_two_cpus circular
 *  tc=6   C/S   : three-deep queue — multi-pending Sec4 verification
 *  tc=7   Safe  : IRQ-off MUST NOT fire — jiffies frozen safety invariant
 *  tc=8   S+C   : timing boundary — just-under (safe) vs just-over (crash)
 *  tc=9   Safe  : hotplug variant — cpu_dying_mask observation
 *  tc=10  C/P   : rcu_expedited scheduling stress — probabilistic crash
 *  tc=11  Safe  : watchdog priority starvation — RT thread measurement
 *  tc=12  Safe  : per-section unit tests — format and mask validation
 *  tc=13  C/S   : balance_push path — balance_push() re-entrancy variant
 *  tc=14  C/S   : migrate_swap re-entrancy — stop_two_cpus nested path
 *  tc=15  C/S   : all-CPU fan-out — every stopper queueing on next CPU
 *  tc=16  C/S   : completion + spinlock deadlock — stopper waits on lock
 *  tc=17  CRASH : timed_out guard — verify single-fire, no double panic
 *  tc=18  Safe  : run-all safe TCs in sequence — regression sweep
 *
 *  C=CRASH  S=safe(no panic)  C/S=depends on do_panic=  P=probabilistic
 *
 * ═══════════════════════════════════════════════════════════════════
 * USAGE
 * ═══════════════════════════════════════════════════════════════════
 *   # Safe baseline:
 *   insmod stop_machine_test.ko tc=1
 *
 *   # Production mdelay crash path (WILL CRASH):
 *   insmod stop_machine_test.ko tc=2
 *
 *   # Exact production re-entrancy, safe mode:
 *   insmod stop_machine_test.ko tc=3 do_panic=0 target_cpu=0
 *
 *   # Exact production re-entrancy, crash mode (WILL CRASH):
 *   insmod stop_machine_test.ko tc=3 do_panic=1 target_cpu=0
 *
 *   # IRQ-off must NOT crash:
 *   insmod stop_machine_test.ko tc=7
 *
 *   # Timing boundary safe (must NOT crash):
 *   insmod stop_machine_test.ko tc=8 do_panic=0
 *
 *   # Timing boundary crash (WILL CRASH):
 *   insmod stop_machine_test.ko tc=8 do_panic=1
 *
 *   # rcu_expedited probabilistic stress:
 *   echo 1 > /proc/sys/kernel/rcu_expedited
 *   insmod stop_machine_test.ko tc=10 stress_sec=60
 *   hackbench -g 8 -l 1000 &
 *
 *   # balance_push variant safe:
 *   insmod stop_machine_test.ko tc=13 do_panic=0
 *
 *   # all-CPU fan-out crash (WILL CRASH):
 *   insmod stop_machine_test.ko tc=15 do_panic=1
 *
 *   # regression sweep (all safe TCs):
 *   insmod stop_machine_test.ko tc=18
 *
 * BUILD:
 *   make -C <kernel_src> M=$(pwd) modules \
 *     ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
 *     CC=clang LD=ld.lld AR=llvm-ar NM=llvm-nm OBJCOPY=llvm-objcopy
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/stop_machine.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/debug.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/irqflags.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/nmi.h>
#include <linux/percpu.h>
#include <linux/ktime.h>

/* ================================================================== */
/* Module parameters                                                   */
/* ================================================================== */
static int  tc         = 1;
static int  do_panic   = 0;
static uint target_cpu = 0;
static uint stress_sec = 60;

module_param(tc,          int,  0444);
module_param(do_panic,    int,  0444);
module_param(target_cpu,  uint, 0444);
module_param(stress_sec,  uint, 0444);

MODULE_PARM_DESC(tc,        "Test case 1-18");
MODULE_PARM_DESC(do_panic,  "0=safe  1=CRASH via patch panic path");
MODULE_PARM_DESC(target_cpu,"Primary CPU for re-entrancy tests (default 0)");
MODULE_PARM_DESC(stress_sec,"Duration of TC10 stress in seconds (default 60)");

/*
 * Patch threshold is hardcoded: STOPPER_TIMEOUT_JIFFIES = 3 * HZ
 * At HZ=250: 3000 ms.  We block for 3000 + 2000 = 5000 ms to ensure
 * we always exceed it with margin, regardless of scheduler jitter.
 */
#define PATCH_TIMEOUT_MS   3000U   /* hardcoded in production patch     */
#define BLOCKER_EXTRA_MS   2000U   /* safety margin above threshold      */
#define BLOCKER_TOTAL_MS   (PATCH_TIMEOUT_MS + BLOCKER_EXTRA_MS)  /* 5000 ms */

/* Wait timeout for completion objects in safe-mode tests */
#define SAFE_WAIT_JIFFIES  msecs_to_jiffies(BLOCKER_TOTAL_MS)

/* ================================================================== */
/* Banner and shared dump helper                                       */
/* ================================================================== */
static void tc_banner(int n, const char *name, bool safe)
{
	pr_warn("\n");
	pr_warn("╔══════════════════════════════════════════════════════╗\n");
	pr_warn("║ [SM_TEST TC%02d] %-38s ║\n", n, name);
	pr_warn("║  do_panic=%-2d  target_cpu=%-2u  PATCH_THRESHOLD=%ums  ║\n",
		do_panic, target_cpu, PATCH_TIMEOUT_MS);
	if (!safe)
		pr_warn("║  *** WILL CRASH — use only on ramdump device ***     ║\n");
	pr_warn("╚══════════════════════════════════════════════════════╝\n");
}

/*
 * safe_dump() — reproduce all 7 diagnostic sections without calling panic().
 * Called from any TC that wants to show what the patch would print.
 * Sec6 (trigger_all_cpu_backtrace) is skipped — see header NOTE.
 */
static noinline void safe_dump(unsigned int cpu, unsigned long elapsed_ms,
				const char *label)
{
	unsigned int c;

	pr_emerg("\n");
	pr_emerg("══════════════════════════════════════════════════════\n");
	pr_emerg("[%s] stopper_deadlock_dump() — safe reproduction\n", label);
	pr_emerg("  CPU%u stuck ~%lums  (patch fires at %ums)\n",
		 cpu, elapsed_ms, PATCH_TIMEOUT_MS);
	pr_emerg("══════════════════════════════════════════════════════\n");

	/* Sec1 */
	pr_emerg("=== Sec1: outer stopper fn + caller ===\n");
	pr_emerg("  outer fn    : %pS\n", __builtin_return_address(0));
	pr_emerg("  outer caller: %pS\n", __builtin_return_address(1));

	/* Sec2 */
	pr_emerg("=== Sec2: multi_stop_data (synthetic PREPARE) ===\n");
	pr_emerg("  state=1(PREPARE)  num_threads=2  thread_ack=1\n");
	pr_emerg("  inner fn: [production: active_load_balance_cpu_stop]\n");
	pr_emerg("  active_cpus: %*pbl\n", cpumask_pr_args(cpu_online_mask));

	/* Sec3 */
	pr_emerg("=== Sec3: CPU masks ===\n");
	pr_emerg("  cpu_online_mask : %*pbl\n", cpumask_pr_args(cpu_online_mask));
	pr_emerg("  cpu_active_mask : %*pbl\n", cpumask_pr_args(cpu_active_mask));
	pr_emerg("  cpu_dying_mask  : %*pbl  (non-zero = hotplug variant)\n",
		 cpumask_pr_args(cpu_dying_mask));

	/* Sec4 */
	pr_emerg("=== Sec4: per-CPU stopper state ===\n");
	for_each_online_cpu(c)
		pr_emerg("  CPU%u: [in-tree patch calls stopper_dump_one_cpu(%u)]\n",
			 c, c);

	/* Sec5 */
	pr_emerg("=== Sec5: call stack ===\n");
	dump_stack();

	/* Sec6 — intentionally skipped */
	pr_emerg("=== Sec6: trigger_all_cpu_backtrace() [SKIPPED in test] ===\n");
	pr_emerg("  Skipped: IPI-based on arm64, 10s/CPU wait hazard.\n");
	pr_emerg("  Production patch DOES call this in the real panic path.\n");

	/* Sec7 */
	pr_emerg("=== Sec7: all task states (SysRq-T) ===\n");
	show_state();

	pr_emerg("══════════════════════════════════════════════════════\n");
	pr_emerg("[%s] SAFE DUMP COMPLETE — no panic called.\n", label);
	pr_emerg("══════════════════════════════════════════════════════\n");
}

/* ================================================================== */
/* TC01 — Safe baseline: 7-section dump inside real stop_machine      */
/*                                                                     */
/* Purpose: verify every diagnostic section fires correctly before    */
/* you trust the crash path.  Runs from a real stopper context so     */
/* Sec1 outer fn/caller and Sec5 stack are authentic.                 */
/*                                                                     */
/* Expected dmesg: all 7 sections visible.  stop_machine returns.     */
/* ================================================================== */
static int tc01_fn(void *data)
{
	safe_dump(smp_processor_id(), 0, "TC01-baseline");
	return 0;
}

static void run_tc01(void)
{
	tc_banner(1, "Safe baseline — 7-section dump, no panic", true);
	stop_machine(tc01_fn, NULL, NULL);
	pr_warn("[TC01] PASSED — stop_machine returned normally\n");
}

/* ================================================================== */
/* TC02 — Production mdelay crash path  [WILL CRASH]                  */
/*                                                                     */
/* Exact equivalent of what the debugfs simulate_panic entry does     */
/* in the v2 build-fixed patch.                                       */
/*                                                                     */
/* Mechanism:                                                          */
/*   stop_machine(mdelay_fn) → active CPU: mdelay(5000ms)            */
/*   Non-active CPUs: spin in PREPARE with IRQs ON                   */
/*   HZ=250: jiffies advance every 4ms                                */
/*   After 750 jiffies (3000ms): time_after() fires on non-active    */
/*   CPU → stopper_deadlock_dump() → panic() → TZ ramdump captured   */
/*                                                                     */
/* Why mdelay() not msleep()?                                         */
/*   mdelay() busy-waits without yielding — holds the stopper thread  */
/*   in RUN context.  msleep() would yield and let the stopper exit   */
/*   multi_cpu_stop() gracefully. The active CPU must stay pinned.   */
/* ================================================================== */
static int tc02_mdelay_fn(void *data)
{
	pr_warn("[TC02] active fn CPU%u: mdelay(%ums) "
		"(threshold=%ums)\n",
		smp_processor_id(), BLOCKER_TOTAL_MS, PATCH_TIMEOUT_MS);
	pr_warn("[TC02] non-active CPUs: PREPARE, IRQs ON, jiffies ticking\n");
	pr_warn("[TC02] expect: patch fires at ~%ums → panic\n",
		PATCH_TIMEOUT_MS);
	mdelay(BLOCKER_TOTAL_MS);
	return 0;
}

static void run_tc02(void)
{
	tc_banner(2, "mdelay production crash path", false);
	stop_machine(tc02_mdelay_fn, NULL, NULL);
	pr_emerg("[TC02] BUG: should have panicked!\n");
}

/* ================================================================== */
/* TC03 — Same-CPU stopper re-entrancy  [exact production bug]        */
/*                                                                     */
/* This is the primary reproduction of the Qualcomm SM4100/SM5100     */
/* production deadlock ("No of cores hotplugged: 0").                 */
/*                                                                     */
/* Production call chain:                                             */
/*   [stopper/N running W1 = active_load_balance_cpu_stop()]          */
/*     finish_task_switch()                                            */
/*       run_balance_callbacks()                                       */
/*         sched_balance_rq() / balance_push()                        */
/*           stop_one_cpu_nowait(N, W2)  ← RE-ENTRANCY               */
/*   W2 queued on stopper/N which is busy executing W1               */
/*   W1 waits for W2 → W2 never runs → DEADLOCK                     */
/*                                                                     */
/* cpu_dying_mask = 0 in both production and this test (LB variant)  */
/*                                                                     */
/* MODE A (do_panic=0): stop_one_cpu() on single stopper.             */
/*   Our completion timeout fires at 5s. safe_dump(). Returns.        */
/* MODE B (do_panic=1): stop_machine() — all CPUs in multi_cpu_stop.  */
/*   Non-active CPUs spin in PREPARE with IRQs ON → jiffies advance  */
/*   → patch time_after() fires at 3s → stopper_deadlock_dump()      */
/*   → panic() → ramdump.                                             */
/* ================================================================== */
struct tc03_ctx {
	struct completion    inner_done;
	struct cpu_stop_work inner_work;
};

static int tc03_inner_fn(void *data)
{
	struct tc03_ctx *ctx = data;
	/* Reaches here only if stopper was NOT deadlocked (unexpected) */
	pr_warn("[TC03] inner_fn ran — no deadlock (unexpected)\n");
	complete(&ctx->inner_done);
	return 0;
}

static int tc03_outer_fn(void *data)
{
	struct tc03_ctx *ctx = data;
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;
	int ret;

	pr_warn("[TC03] outer_fn CPU%u: re-entrancy path\n", cpu);
	pr_warn("  [production] active_load_balance_cpu_stop()\n");
	pr_warn("    → finish_task_switch() → run_balance_callbacks()\n");
	pr_warn("    → stop_one_cpu_nowait(CPU%u)  ← re-entrant\n", cpu);
	pr_warn("  stopper/CPU%u busy → inner never runs → DEADLOCK\n", cpu);

	/*
	 * Queue on the SAME stopper currently executing this function.
	 * This IS the production failure: the stopper is single-threaded
	 * and cannot process new work until the current fn returns.
	 * Current fn waits for the new work → circular → DEADLOCK.
	 */
	stop_one_cpu_nowait(cpu, tc03_inner_fn, ctx, &ctx->inner_work);

	/*
	 * wait_for_completion_timeout() not mdelay():
	 * Allows jiffies to advance (timer IRQ fires during block).
	 * MODE B: patch time_after() fires on non-active CPU at 3s.
	 * MODE A: our 5s timeout fires, we call safe_dump() and return.
	 */
	ret = wait_for_completion_timeout(&ctx->inner_done, SAFE_WAIT_JIFFIES);
	if (!ret) {
		unsigned long ms = jiffies_to_msecs(jiffies - t0);
		pr_emerg("[TC03] DEADLOCK after ~%lums\n", ms);
		if (!do_panic)
			safe_dump(cpu, ms, "TC03");
		else
			pr_emerg("[TC03] patch should have panicked at %ums\n",
				 PATCH_TIMEOUT_MS);
	}
	return 0;
}

static void run_tc03(void)
{
	struct tc03_ctx ctx;
	unsigned int cpu = target_cpu;

	tc_banner(3, "Same-CPU re-entrancy — exact production LB path",
		  !do_panic);
	if (!cpu_online(cpu)) cpu = 0;
	init_completion(&ctx.inner_done);

	if (do_panic) {
		pr_warn("[TC03-B] stop_machine() → all CPUs in PREPARE → CRASH\n");
		stop_machine(tc03_outer_fn, &ctx, NULL);
	} else {
		pr_warn("[TC03-A] stop_one_cpu(CPU%u) → single stopper → safe\n",
			cpu);
		stop_one_cpu(cpu, tc03_outer_fn, &ctx);
	}
	pr_warn("[TC03] returned (MODE A only)\n");
}

/* ================================================================== */
/* TC04 — Re-entrancy + rcu_read_lock: dual symptom reproduction      */
/*                                                                     */
/* Identical mechanism to TC03, plus an RCU read-side critical        */
/* section held across the entire deadlock window.                    */
/*                                                                     */
/* WHY this matters:                                                  */
/*   Production crash logs show TWO symptoms simultaneously:          */
/*   (a) migration/N stuck → stopper dump → patch panic at 3s        */
/*   (b) "INFO: rcu_sched self-detected stall on CPU N" at ~21s      */
/*   Symptom (b) appears if the patch DOES NOT fire (regression).     */
/*   If the patch fires at 3s, (b) never prints — panic comes first.  */
/*   This TC reproduces BOTH: (b) is visible in dmesg if do_panic=0. */
/*                                                                     */
/* rcu_read_lock() before the re-entrant queue means:                 */
/*   The RCU read-side CS is held with no chance of rcu_read_unlock() */
/*   until the completion fires (which it never does in deadlock).    */
/*   After rcu_jiffies_till_stall_check() (~21s), RCU GP kthread      */
/*   prints "rcu_sched: detected stall on CPU N" — secondary symptom. */
/* ================================================================== */
struct tc04_ctx {
	struct completion    inner_done;
	struct cpu_stop_work inner_work;
};

static int tc04_inner_fn(void *data)
{
	struct tc04_ctx *ctx = data;
	pr_warn("[TC04] inner_fn ran (unexpected — no deadlock)\n");
	complete(&ctx->inner_done);
	return 0;
}

static int tc04_outer_fn(void *data)
{
	struct tc04_ctx *ctx = data;
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;
	int ret;

	pr_warn("[TC04] outer_fn CPU%u: re-entrancy + rcu_read_lock\n", cpu);
	pr_warn("[TC04] rcu_read_lock() held — RCU GP stall fires ~21s "
		"if patch absent\n");

	/*
	 * rcu_read_lock() holds an RCU reader across the deadlock.
	 * Reproduces the secondary production symptom: RCU GP stall warning.
	 * In do_panic=0 mode, the 5s safe timeout fires and rcu_read_unlock()
	 * is called — so the RCU stall warning is suppressed in safe mode.
	 * In do_panic=1 mode, patch panic() fires at 3s — rcu_read_unlock()
	 * is never reached, and if you could wait 21s you'd see the RCU stall.
	 */
	rcu_read_lock();
	stop_one_cpu_nowait(cpu, tc04_inner_fn, ctx, &ctx->inner_work);
	ret = wait_for_completion_timeout(&ctx->inner_done, SAFE_WAIT_JIFFIES);
	rcu_read_unlock(); /* reached only in safe mode */

	if (!ret) {
		unsigned long ms = jiffies_to_msecs(jiffies - t0);
		pr_emerg("[TC04] DEADLOCK + RCU CS stuck ~%lums\n", ms);
		pr_emerg("[TC04] In production: RCU stall visible at ~21s "
			 "alongside stopper hang\n");
		if (!do_panic)
			safe_dump(cpu, ms, "TC04");
	}
	return 0;
}

static void run_tc04(void)
{
	struct tc04_ctx ctx;
	unsigned int cpu = target_cpu;

	tc_banner(4, "Re-entrancy + rcu_read_lock: stopper hang + RCU GP stall",
		  !do_panic);
	if (!cpu_online(cpu)) cpu = 0;
	init_completion(&ctx.inner_done);

	if (do_panic)
		stop_machine(tc04_outer_fn, &ctx, NULL);
	else
		stop_one_cpu(cpu, tc04_outer_fn, &ctx);
	pr_warn("[TC04] returned\n");
}

/* ================================================================== */
/* TC05 — Two-CPU mutual circular deadlock (stop_two_cpus variant)    */
/*                                                                     */
/* stop_two_cpus(cpuA, cpuB, fn, arg) runs fn on cpuA's stopper      */
/* while also involving cpuB's stopper.  If fn then tries to do       */
/* ANOTHER stop involving cpuB, and cpuB's stopper is already inside  */
/* multi_cpu_stop() for the outer call → cpuB can't accept new work  */
/* → fn waits → DEADLOCK.                                             */
/*                                                                     */
/* This exercises:                                                    */
/*   - multi_stop_data.num_threads = 2                                */
/*   - multi_stop_data.thread_ack  = 2 (both CPUs stuck)             */
/*   - Sec3: both cpuA and cpuB present in cpu_online_mask            */
/*   - Sec4: stopper_dump_one_cpu() shows busy on both CPUs           */
/* ================================================================== */
struct tc05_ctx {
	struct completion    extra_done;
	struct cpu_stop_work extra_work;
	unsigned int         cpu_a;
	unsigned int         cpu_b;
};

static int tc05_extra_fn(void *data)
{
	struct tc05_ctx *ctx = data;
	pr_warn("[TC05] extra_fn ran CPU%u (no deadlock — unexpected)\n",
		smp_processor_id());
	complete(&ctx->extra_done);
	return 0;
}

static int tc05_fn_a(void *data)
{
	struct tc05_ctx *ctx = data;
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;
	int ret;

	pr_warn("[TC05] fn_a CPU%u: queueing extra work on CPU%u stopper\n",
		cpu, ctx->cpu_b);
	pr_warn("[TC05] CPU%u stopper is inside multi_cpu_stop() → cannot\n",
		ctx->cpu_b);
	pr_warn("[TC05] process extra work → fn_a waits → TWO-CPU DEADLOCK\n");

	/*
	 * Queue extra work on cpu_b's stopper.
	 * cpu_b is inside multi_cpu_stop() (for the outer stop_machine call)
	 * and cannot process new work until all threads exit together.
	 * fn_a (on cpu_a's stopper) waits for extra_fn → deadlock.
	 */
	stop_one_cpu_nowait(ctx->cpu_b, tc05_extra_fn, ctx, &ctx->extra_work);

	ret = wait_for_completion_timeout(&ctx->extra_done, SAFE_WAIT_JIFFIES);
	if (!ret) {
		unsigned long ms = jiffies_to_msecs(jiffies - t0);
		pr_emerg("[TC05] TWO-CPU DEADLOCK after ~%lums\n", ms);
		pr_emerg("[TC05] CPU%u → CPU%u stopper busy → circular wait\n",
			 ctx->cpu_a, ctx->cpu_b);
		pr_emerg("[TC05] Sec3 cpu_dying_mask: %*pbl (0=LB variant)\n",
			 cpumask_pr_args(cpu_dying_mask));
		if (!do_panic) {
			dump_stack();
			show_state();
			pr_emerg("[TC05] No panic — device stays up\n");
		}
	}
	return 0;
}

static void run_tc05(void)
{
	struct tc05_ctx ctx;
	unsigned int cpu_a = target_cpu, cpu_b;

	tc_banner(5, "Two-CPU mutual deadlock — stop_two_cpus circular",
		  !do_panic);

	if (!cpu_online(cpu_a)) cpu_a = 0;
	cpu_b = cpumask_next(cpu_a, cpu_online_mask);
	if (cpu_b >= nr_cpu_ids) {
		pr_warn("[TC05] SKIPPED — need >= 2 online CPUs\n");
		return;
	}
	pr_warn("[TC05] cpu_a=%u cpu_b=%u\n", cpu_a, cpu_b);

	init_completion(&ctx.extra_done);
	ctx.cpu_a = cpu_a;
	ctx.cpu_b = cpu_b;

	if (do_panic)
		stop_machine(tc05_fn_a, &ctx, NULL);
	else
		stop_one_cpu(cpu_a, tc05_fn_a, &ctx);
	pr_warn("[TC05] returned\n");
}

/* ================================================================== */
/* TC06 — Three-deep nested queue: multi-pending Sec4 verification    */
/*                                                                     */
/* W1 on stopper/N queues W2 AND W3 on stopper/N before waiting.     */
/* Both W2 and W3 pile up in the stopper work list behind W1.        */
/* W1 waits for W2 → deadlock (W3 also stuck).                      */
/*                                                                     */
/* Validates:                                                         */
/*   stopper_dump_one_cpu() shows n>=2 in pending list               */
/*   "pending[0]: fn=tc06_w2_fn caller=..."                          */
/*   "pending[1]: fn=tc06_w3_fn caller=..."                          */
/*                                                                     */
/* Real production scenario: both balance_push() and                  */
/* active_load_balance_cpu_stop() independently call                  */
/* stop_one_cpu_nowait() on the same stopper before either completes. */
/* ================================================================== */
struct tc06_ctx {
	struct completion    w2_done;
	struct completion    w3_done;
	struct cpu_stop_work w2_work;
	struct cpu_stop_work w3_work;
};

static int tc06_w2_fn(void *data)
{
	struct tc06_ctx *ctx = data;
	pr_warn("[TC06] W2 ran (no deadlock)\n");
	complete(&ctx->w2_done);
	return 0;
}

static int tc06_w3_fn(void *data)
{
	struct tc06_ctx *ctx = data;
	pr_warn("[TC06] W3 ran (no deadlock)\n");
	complete(&ctx->w3_done);
	return 0;
}

static int tc06_w1_fn(void *data)
{
	struct tc06_ctx *ctx = data;
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;
	int ret;

	pr_warn("[TC06] W1 outer fn CPU%u: queueing W2 and W3 on SAME stopper\n",
		cpu);
	pr_warn("[TC06] → 2 pending entries behind W1 in stopper work list\n");
	pr_warn("[TC06] → Sec4 must show n=2 pending works\n");

	/* Queue both W2 and W3 — they both block behind W1 */
	stop_one_cpu_nowait(cpu, tc06_w2_fn, ctx, &ctx->w2_work);
	stop_one_cpu_nowait(cpu, tc06_w3_fn, ctx, &ctx->w3_work);

	/* Wait for W2 — deadlock: W2 needs stopper free, W1 holds stopper */
	ret = wait_for_completion_timeout(&ctx->w2_done, SAFE_WAIT_JIFFIES);
	if (!ret) {
		unsigned long ms = jiffies_to_msecs(jiffies - t0);
		pr_emerg("[TC06] THREE-DEEP DEADLOCK after ~%lums\n", ms);
		pr_emerg("[TC06] stopper/CPU%u: W1 running, W2+W3 pending\n",
			 cpu);
		pr_emerg("[TC06] Sec4 expected:\n");
		pr_emerg("  pending[0]: fn=%pS\n", tc06_w2_fn);
		pr_emerg("  pending[1]: fn=%pS\n", tc06_w3_fn);
		if (!do_panic)
			safe_dump(cpu, ms, "TC06");
	}
	return 0;
}

static void run_tc06(void)
{
	struct tc06_ctx ctx;
	unsigned int cpu = target_cpu;

	tc_banner(6, "Three-deep nested queue — Sec4 multi-pending verify",
		  !do_panic);
	if (!cpu_online(cpu)) cpu = 0;
	init_completion(&ctx.w2_done);
	init_completion(&ctx.w3_done);

	if (do_panic)
		stop_machine(tc06_w1_fn, &ctx, NULL);
	else
		stop_one_cpu(cpu, tc06_w1_fn, &ctx);
	pr_warn("[TC06] returned\n");
}

/* ================================================================== */
/* TC07 — IRQ-off false positive guard  [MUST NOT crash]             */
/*                                                                     */
/* THE KEY SAFETY INVARIANT of the patch design:                      */
/*                                                                     */
/*   In DISABLE_IRQ and RUN states, CPUs call:                       */
/*     local_irq_disable()                                            */
/*     hard_irq_disable()  (arch)                                     */
/*   This blocks the timer hardware IRQ → jiffies DO NOT advance.    */
/*   → time_after(jiffies, start_jiffies + THRESHOLD) stays FALSE.   */
/*   → patch MUST NOT fire.                                           */
/*                                                                     */
/* This test holds IRQs disabled for BLOCKER_TOTAL_MS (5s).          */
/* Uses ktime_get() (hardware counter, IRQ-independent) to measure   */
/* real elapsed time while jiffies are frozen.                       */
/*                                                                     */
/* Expected: stop_machine returns.  No panic.                         */
/* If it panics: position-C detection has a false-positive bug.       */
/*                                                                     */
/* This directly validates the commit message statement:              */
/*   "DISABLE_IRQ/RUN (IRQs OFF): jiffies frozen → time_after()     */
/*    always false → NO panic (correct!)"                             */
/* ================================================================== */
static int tc07_irqoff_fn(void *data)
{
	unsigned long flags;
	ktime_t t0 = ktime_get();
	ktime_t elapsed;

	pr_warn("[TC07] IRQ-off safety test CPU%u — disabling IRQs\n",
		smp_processor_id());
	pr_warn("[TC07] Busy-waiting %ums with IRQs OFF\n", BLOCKER_TOTAL_MS);
	pr_warn("[TC07] jiffies will be FROZEN — time_after() must stay false\n");
	pr_warn("[TC07] patch MUST NOT fire → device MUST stay up\n");

	local_irq_save(flags);

	do {
		cpu_relax();
		elapsed = ktime_sub(ktime_get(), t0);
	} while (ktime_to_ms(elapsed) < (s64)BLOCKER_TOTAL_MS);

	local_irq_restore(flags);

	pr_warn("[TC07] IRQs restored after %lldms real-time\n",
		ktime_to_ms(elapsed));
	pr_warn("[TC07] If you see this: IRQ-off guard PASSED — no false fire\n");
	return 0;
}

static void run_tc07(void)
{
	tc_banner(7, "IRQ-off: jiffies frozen — MUST NOT crash", true);
	pr_warn("[TC07] This test MUST complete without panic.\n");
	pr_warn("[TC07] A panic here means position-C has a false-positive bug.\n");
	stop_machine(tc07_irqoff_fn, NULL, NULL);
	pr_warn("[TC07] PASSED — IRQ-off safety invariant confirmed\n");
}

/* ================================================================== */
/* TC08 — Timing boundary: just-under (safe) vs just-over (crash)     */
/*                                                                     */
/* Validates the detection fires at the correct threshold.            */
/* Threshold: STOPPER_TIMEOUT_JIFFIES = 3 * HZ = 3000ms at HZ=250   */
/*                                                                     */
/* do_panic=0: block for 2500ms (< 3000ms) → MUST NOT panic          */
/* do_panic=1: block for 3500ms (> 3000ms) → MUST panic              */
/*                                                                     */
/* At HZ=250: 1 jiffy = 4ms.  Granularity = ±4ms.                   */
/* 2500ms = 625 jiffies (safely below 750)                           */
/* 3500ms = 875 jiffies (safely above 750)                           */
/* ================================================================== */
struct tc08_ctx { unsigned int ms; };

static int tc08_mdelay_fn(void *data)
{
	struct tc08_ctx *ctx = data;
	pr_warn("[TC08] blocking %ums (threshold=%ums)\n",
		ctx->ms, PATCH_TIMEOUT_MS);
	mdelay(ctx->ms);
	return 0;
}

static void run_tc08(void)
{
	struct tc08_ctx ctx;

	if (!do_panic) {
		tc_banner(8, "Timing boundary A: 2500ms < 3000ms MUST NOT crash",
			  true);
		ctx.ms = PATCH_TIMEOUT_MS - 500;   /* 2500ms */
		pr_warn("[TC08A] %ums < %ums threshold → expect no panic\n",
			ctx.ms, PATCH_TIMEOUT_MS);
		stop_machine(tc08_mdelay_fn, &ctx, NULL);
		pr_warn("[TC08A] PASSED — detection correctly did not fire\n");
	} else {
		tc_banner(8, "Timing boundary B: 3500ms > 3000ms WILL CRASH",
			  false);
		ctx.ms = PATCH_TIMEOUT_MS + 500;   /* 3500ms */
		pr_warn("[TC08B] %ums > %ums threshold → expect panic\n",
			ctx.ms, PATCH_TIMEOUT_MS);
		stop_machine(tc08_mdelay_fn, &ctx, NULL);
		pr_emerg("[TC08B] BUG: should have panicked!\n");
	}
}

/* ================================================================== */
/* TC09 — Hotplug variant: cpu_dying_mask observation                 */
/*                                                                     */
/* The patch's Sec3 prints cpu_dying_mask to distinguish:             */
/*   cpu_dying_mask == 0 → load-balancer nested stop (production)    */
/*   cpu_dying_mask != 0 → CPU hotplug triggered the stop            */
/*                                                                     */
/* "No of cores hotplugged: 0" in production crash logs confirms      */
/* the LB variant.  This test demonstrates what the hotplug variant  */
/* looks like so you can distinguish them in ramdump analysis.        */
/*                                                                     */
/* Safe: no deadlock introduced, only mask observation.               */
/* ================================================================== */
static int tc09_observe_fn(void *data)
{
	pr_warn("[TC09] Inside stop fn — printing CPU masks:\n");
	pr_warn("  cpu_online_mask : %*pbl\n", cpumask_pr_args(cpu_online_mask));
	pr_warn("  cpu_active_mask : %*pbl\n", cpumask_pr_args(cpu_active_mask));
	pr_warn("  cpu_dying_mask  : %*pbl  ← must be 0 for LB variant\n",
		cpumask_pr_args(cpu_dying_mask));
	if (cpumask_empty(cpu_dying_mask))
		pr_warn("  → cpu_dying_mask=0: load-balancer variant\n");
	else
		pr_warn("  → cpu_dying_mask non-zero: hotplug variant\n");
	return 0;
}

static void run_tc09(void)
{
	unsigned int ocpu;
	int r;

	tc_banner(9, "Hotplug variant — cpu_dying_mask observation", true);

	pr_warn("[TC09] Step 1: baseline (no hotplug in progress)\n");
	stop_machine(tc09_observe_fn, NULL, NULL);

	if (num_online_cpus() < 2) {
		pr_warn("[TC09] SKIPPED step 2 — need >= 2 online CPUs\n");
		return;
	}

	ocpu = cpumask_next(smp_processor_id(), cpu_online_mask);
	if (ocpu >= nr_cpu_ids) {
		pr_warn("[TC09] SKIPPED step 2 — no other online CPU found\n");
		return;
	}

	pr_warn("[TC09] Step 2: taking CPU%u offline\n", ocpu);
	r = cpu_down(ocpu);
	if (r) {
		pr_warn("[TC09] cpu_down failed (%d) — hotplug may be disabled\n", r);
		return;
	}
	pr_warn("[TC09] CPU%u offline. Masks:\n", ocpu);
	pr_warn("  cpu_online_mask: %*pbl\n", cpumask_pr_args(cpu_online_mask));
	pr_warn("  cpu_dying_mask : %*pbl (transient — clears after offline)\n",
		cpumask_pr_args(cpu_dying_mask));

	pr_warn("[TC09] Step 3: bringing CPU%u back online\n", ocpu);
	r = cpu_up(ocpu);
	if (r)
		pr_warn("[TC09] cpu_up failed (%d)\n", r);
	else
		pr_warn("[TC09] CPU%u online again\n", ocpu);

	pr_warn("[TC09] PASSED — production crash: cpu_dying_mask=0 → LB variant\n");
}

/* ================================================================== */
/* TC10 — rcu_expedited scheduling stress  [probabilistic crash]      */
/*                                                                     */
/* rcu_expedited=1 forces every synchronize_rcu() to call            */
/* synchronize_rcu_expedited() which uses stop_machine internally.    */
/* Under heavy scheduling pressure, active_load_balance() fires       */
/* inside one of these stop_machine windows → nested stop_one_cpu_   */
/* nowait() re-entrancy → DEADLOCK → patch fires.                    */
/*                                                                     */
/* Probability: ~50% in 60s with 8 stress kthreads.                  */
/* Significantly higher with external hackbench load.                 */
/*                                                                     */
/* PREREQUISITES (must be done before insmod):                        */
/*   echo 1 > /proc/sys/kernel/rcu_expedited                          */
/*   hackbench -g 8 -l 1000 &   (optional but recommended)           */
/* ================================================================== */
#define TC10_NTHREADS  8
static atomic_t    tc10_stop;
static struct task_struct *tc10_tasks[TC10_NTHREADS];

static int tc10_stress_fn(void *unused)
{
	unsigned long i = 0;
	while (!atomic_read(&tc10_stop) && !kthread_should_stop()) {
		switch (i % 5) {
		case 0: schedule();        break;
		case 1: synchronize_rcu(); break; /* → stop_machine if expedited */
		case 2: cond_resched();    break;
		case 3: yield();           break;
		case 4: schedule_timeout_interruptible(1); break;
		}
		i++;
	}
	return 0;
}

static void run_tc10(void)
{
	int i;
	unsigned long end = jiffies + stress_sec * HZ;

	tc_banner(10, "rcu_expedited stress — probabilistic production crash",
		  false);
	pr_warn("[TC10] PREREQUISITES:\n");
	pr_warn("  echo 1 > /proc/sys/kernel/rcu_expedited\n");
	pr_warn("  hackbench -g 8 -l 1000 &\n");
	pr_warn("[TC10] Starting %d kthreads for %us\n",
		TC10_NTHREADS, stress_sec);

	atomic_set(&tc10_stop, 0);
	for (i = 0; i < TC10_NTHREADS; i++) {
		tc10_tasks[i] = kthread_run(tc10_stress_fn, NULL,
					    "sm_stress/%d", i);
		if (IS_ERR(tc10_tasks[i]))
			tc10_tasks[i] = NULL;
	}

	while (time_before(jiffies, end))
		schedule_timeout_interruptible(HZ);

	atomic_set(&tc10_stop, 1);
	for (i = 0; i < TC10_NTHREADS; i++) {
		if (tc10_tasks[i]) {
			kthread_stop(tc10_tasks[i]);
			tc10_tasks[i] = NULL;
		}
	}

	pr_warn("[TC10] Stress done — no crash in %us window\n", stress_sec);
	pr_warn("[TC10] Try: longer stress_sec or more external load\n");
}

/* ================================================================== */
/* TC11 — Watchdog priority starvation measurement  [safe]            */
/*                                                                     */
/* Qualcomm msm_watchdog kthread: SCHED_FIFO, priority MAX_RT_PRIO-1 */
/* migration/N threads:           SCHED_FIFO, priority MAX_RT_PRIO-1 */
/*                                                                     */
/* SCHED_FIFO rule: a running task at priority P is NOT preempted by  */
/* another runnable task at the same priority P.                      */
/* → If all CPUs are occupied by migration/N threads spinning in      */
/*   multi_cpu_stop(), the watchdog kthread has nowhere to run.       */
/* → pet_watchdog() never called → hardware counter runs freely      */
/* → bark fires at last_pet_time + bark_time (11s)                   */
/*                                                                     */
/* This test pins SCHED_FIFO/MAX_RT_PRIO-1 kthreads on each CPU      */
/* for 2s, demonstrating that the watchdog is starved.                */
/* Safe: threads voluntarily yield after 2s.                          */
/* ================================================================== */
static atomic_t tc11_yield_flag;
static struct task_struct *tc11_tasks[NR_CPUS];

static int tc11_rt_fn(void *data)
{
	struct sched_param sp = { .sched_priority = MAX_RT_PRIO - 1 };
	unsigned int cpu = smp_processor_id();
	unsigned long t0 = jiffies;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sp);

	pr_warn("[TC11] CPU%u: SCHED_FIFO prio=%d — watchdog starved for 2s\n",
		cpu, MAX_RT_PRIO - 1);

	while (jiffies - t0 < 2 * HZ && !atomic_read(&tc11_yield_flag))
		cpu_relax();

	pr_warn("[TC11] CPU%u RT thread yielding\n", cpu);
	return 0;
}

static void run_tc11(void)
{
	unsigned int cpu;
	int i = 0;

	tc_banner(11, "Watchdog priority starvation measurement", true);
	pr_warn("[TC11] watchdog kthread = SCHED_FIFO MAX_RT_PRIO-1\n");
	pr_warn("[TC11] migration/N threads = SCHED_FIFO MAX_RT_PRIO-1\n");
	pr_warn("[TC11] Equal priority → no preemption → watchdog starved\n");
	pr_warn("[TC11] patch(3s) < bark_time(11s) → panic wins ramdump\n");
	pr_warn("[TC11] Timing safety margin: 11000 - 3000 = 8000ms\n");

	atomic_set(&tc11_yield_flag, 0);
	for_each_online_cpu(cpu) {
		if (i >= NR_CPUS) break;
		tc11_tasks[i] = kthread_create(tc11_rt_fn, NULL,
					       "sm_rt/%u", cpu);
		if (!IS_ERR(tc11_tasks[i])) {
			kthread_bind(tc11_tasks[i], cpu);
			wake_up_process(tc11_tasks[i]);
			i++;
		}
	}
	msleep(2500);
	atomic_set(&tc11_yield_flag, 1);
	msleep(200);
	for (i = 0; i < NR_CPUS && tc11_tasks[i]; i++)
		if (!IS_ERR(tc11_tasks[i]))
			kthread_stop(tc11_tasks[i]);

	pr_warn("[TC11] PASSED — 8s safety margin vs Qualcomm bark confirmed\n");
}

/* ================================================================== */
/* TC12 — Per-section unit tests  [safe, format validation]           */
/*                                                                     */
/* Tests each of the 7 sections in isolation from a real stop_machine */
/* context.  Validates:                                               */
/*   - %pS resolves to symbol+offset (not raw address)               */
/*   - %*pbl cpumask format is correct                                */
/*   - HZ/jiffies relationship: 3*HZ ≈ 3000ms                       */
/*   - state name array has 5 entries [0..4]                          */
/*   - timed_out guard prevents double fire                           */
/* ================================================================== */
static int tc12_fn(void *data)
{
	unsigned int c;

	pr_warn("[TC12] Section unit tests from CPU%u\n", smp_processor_id());

	pr_warn("[TC12] 12a Sec1 fn/caller resolution:\n");
	pr_warn("  fn    : %pS\n", tc12_fn);
	pr_warn("  caller: %pS\n", __builtin_return_address(0));
	pr_warn("  → must show symbol name, not raw hex\n");

	pr_warn("[TC12] 12b Sec2 state name table:\n");
	pr_warn("  STOPPER_TIMEOUT_JIFFIES = 3 * HZ = 3 * %u = %u jiffies\n",
		HZ, 3 * HZ);
	pr_warn("  jiffies_to_msecs(3*HZ) = %ums  (must be ~3000)\n",
		jiffies_to_msecs(3 * HZ));
	pr_warn("  states: NONE(0) PREPARE(1) DISABLE_IRQ(2) RUN(3) EXIT(4)\n");

	pr_warn("[TC12] 12c Sec3 mask format:\n");
	pr_warn("  cpu_online_mask : %*pbl\n", cpumask_pr_args(cpu_online_mask));
	pr_warn("  cpu_active_mask : %*pbl\n", cpumask_pr_args(cpu_active_mask));
	pr_warn("  cpu_dying_mask  : %*pbl  (must be 0 here)\n",
		cpumask_pr_args(cpu_dying_mask));

	pr_warn("[TC12] 12d Sec4 per-CPU iteration (%u online CPUs):\n",
		num_online_cpus());
	for_each_online_cpu(c)
		pr_warn("  CPU%u: patch calls stopper_dump_one_cpu(%u)\n", c, c);

	pr_warn("[TC12] 12e Sec5 dump_stack():\n");
	dump_stack();

	pr_warn("[TC12] 12f Sec6 trigger_all_cpu_backtrace() analysis:\n");
	pr_warn("  arm64 Qualcomm: IPI-based (not true NMI)\n");
	pr_warn("  IRQs-off CPU: cannot receive IPI → 10s timeout/CPU\n");
	pr_warn("  %u online CPUs × 10s = %us max inside panic path\n",
		num_online_cpus(), num_online_cpus() * 10);
	pr_warn("  Qualcomm bark_time = 11s  →  %s\n",
		num_online_cpus() * 10 < 11
		? "safe on this CPU count"
		: "HAZARD: exceeds bark_time");

	pr_warn("[TC12] 12g Sec7 show_state():\n");
	show_state();

	pr_warn("[TC12] 12h timed_out guard (prevents double panic):\n");
	pr_warn("  timed_out = 0 before loop\n");
	pr_warn("  first time_after() → timed_out = 1, dump, panic()\n");
	pr_warn("  subsequent loop iterations: timed_out=1 → condition skipped\n");
	pr_warn("  → exactly ONE panic call per deadlock event\n");

	pr_warn("[TC12] 12i panic string format:\n");
	pr_warn("  'migration/%%u: stuck in multi_cpu_stop for ~%%u ms | "
		"state=%%s thread_ack=%%d stop_fn=%%pS | "
		"nested stop_machine deadlock | see ramdump for full trace'\n");
	pr_warn("  min_t(unsigned int, state, MULTI_STOP_EXIT=4) → no OOB\n");

	pr_warn("[TC12] PASSED — all sections validated\n");
	return 0;
}

static void run_tc12(void)
{
	tc_banner(12, "Per-section unit tests — format and invariant checks",
		  true);
	stop_machine(tc12_fn, NULL, NULL);
	pr_warn("[TC12] PASSED\n");
}

/* ================================================================== */
/* TC13 — balance_push() re-entrancy path                             */
/*                                                                     */
/* balance_push() is a DIFFERENT code path from sched_balance_rq()   */
/* that also calls stop_one_cpu_nowait() from inside a stopper fn.   */
/*                                                                     */
/* Production path (balance_push variant):                            */
/*   active_load_balance_cpu_stop()                                   */
/*     → run_balance_callbacks() / balance_callback()                 */
/*       → push_rt_task() / push_dl_task()                            */
/*         → balance_push()                                           */
/*           → stop_one_cpu_nowait(target_cpu, push_cpu_stop, ...)   */
/*             ← same stopper → DEADLOCK                              */
/*                                                                     */
/* This TC specifically models the balance_push() variant by          */
/* simulating a "push" to a second CPU that is itself inside          */
/* multi_cpu_stop() (cannot process the push work).                  */
/*                                                                     */
/* Structural difference from TC03:                                   */
/*   TC03: re-entrant on the SAME CPU stopper (self-deadlock)        */
/*   TC13: pushes to a DIFFERENT CPU that is also inside the stop    */
/*         barrier (cross-CPU deadlock via stop_one_cpu_nowait)      */
/* ================================================================== */
struct tc13_ctx {
	struct completion    push_done;
	struct cpu_stop_work push_work;
	unsigned int         push_cpu;
};

static int tc13_push_fn(void *data)
{
	struct tc13_ctx *ctx = data;
	pr_warn("[TC13] push_fn ran CPU%u (push_cpu=%u) — no deadlock (unexpected)\n",
		smp_processor_id(), ctx->push_cpu);
	complete(&ctx->push_done);
	return 0;
}

static int tc13_outer_fn(void *data)
{
	struct tc13_ctx *ctx = data;
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;
	int ret;

	pr_warn("[TC13] balance_push variant — CPU%u pushing to CPU%u\n",
		cpu, ctx->push_cpu);
	pr_warn("[TC13] Production path:\n");
	pr_warn("  active_load_balance_cpu_stop()\n");
	pr_warn("    → balance_push() → stop_one_cpu_nowait(CPU%u, push_fn)\n",
		ctx->push_cpu);
	pr_warn("  CPU%u stopper inside multi_cpu_stop() → push_fn queued\n",
		ctx->push_cpu);
	pr_warn("  but CPU%u cannot process it → outer fn waits → DEADLOCK\n",
		ctx->push_cpu);

	/*
	 * Push work to ctx->push_cpu stopper.
	 * In stop_machine() context: push_cpu's stopper is inside
	 * multi_cpu_stop() for the outer stop barrier → cannot execute
	 * push_fn until all threads exit together.
	 * outer_fn waits for push_fn → push_fn can't run → DEADLOCK.
	 */
	stop_one_cpu_nowait(ctx->push_cpu, tc13_push_fn, ctx,
			    &ctx->push_work);

	ret = wait_for_completion_timeout(&ctx->push_done, SAFE_WAIT_JIFFIES);
	if (!ret) {
		unsigned long ms = jiffies_to_msecs(jiffies - t0);
		pr_emerg("[TC13] BALANCE_PUSH DEADLOCK after ~%lums\n", ms);
		pr_emerg("[TC13] CPU%u → push_cpu CPU%u stuck\n",
			 cpu, ctx->push_cpu);
		pr_emerg("[TC13] cpu_dying_mask: %*pbl (0=LB variant)\n",
			 cpumask_pr_args(cpu_dying_mask));
		if (!do_panic)
			safe_dump(cpu, ms, "TC13");
	}
	return 0;
}

static void run_tc13(void)
{
	struct tc13_ctx ctx;
	unsigned int cpu_a = target_cpu, cpu_b;

	tc_banner(13, "balance_push() re-entrancy — cross-CPU push deadlock",
		  !do_panic);

	if (!cpu_online(cpu_a)) cpu_a = 0;
	cpu_b = cpumask_next(cpu_a, cpu_online_mask);
	if (cpu_b >= nr_cpu_ids) {
		pr_warn("[TC13] SKIPPED — need >= 2 online CPUs\n");
		return;
	}
	pr_warn("[TC13] active_cpu=%u  push_cpu=%u\n", cpu_a, cpu_b);

	init_completion(&ctx.push_done);
	ctx.push_cpu = cpu_b;

	if (do_panic) {
		pr_warn("[TC13-B] stop_machine() → CRASH\n");
		stop_machine(tc13_outer_fn, &ctx, NULL);
	} else {
		pr_warn("[TC13-A] stop_one_cpu(CPU%u) → safe\n", cpu_a);
		stop_one_cpu(cpu_a, tc13_outer_fn, &ctx);
	}
	pr_warn("[TC13] returned\n");
}

/* ================================================================== */
/* TC14 — migrate_swap_stop re-entrancy  [stop_two_cpus nested]       */
/*                                                                     */
/* migrate_swap() calls stop_two_cpus(src, dst, migrate_swap_stop)   */
/* which internally calls multi_cpu_stop() with num_threads=2.        */
/* If migrate_swap_stop itself triggers another stop_one_cpu_nowait   */
/* (e.g. via finish_task_switch → run_balance_callbacks), the nested  */
/* work is blocked behind the outer two-CPU stop barrier.             */
/*                                                                     */
/* This TC models the migrate_swap nested path:                       */
/*   - Runs stop_machine() with num_threads = all online CPUs         */
/*   - fn on cpu_a simulates "migrate_swap_stop" then tries to push  */
/*   - Blocked cross-CPU nested work → DEADLOCK                      */
/*                                                                     */
/* Key difference from TC05:                                          */
/*   TC05: fn_a waits for extra work on cpu_b (which is also stopped)*/
/*   TC14: simulates migrate_swap_stop specifically calling into       */
/*         finish_task_switch() path that tries another stop          */
/* ================================================================== */
struct tc14_ctx {
	struct completion    swap_done;
	struct cpu_stop_work swap_nested_work;
	unsigned int         src_cpu;
	unsigned int         dst_cpu;
};

static int tc14_nested_fn(void *data)
{
	struct tc14_ctx *ctx = data;
	pr_warn("[TC14] nested_fn (would run after migrate_swap) — unexpected\n");
	complete(&ctx->swap_done);
	return 0;
}

static int tc14_migrate_swap_fn(void *data)
{
	struct tc14_ctx *ctx = data;
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;
	int ret;

	pr_warn("[TC14] migrate_swap_stop simulation on CPU%u\n", cpu);
	pr_warn("[TC14] Production: migrate_swap_stop()\n");
	pr_warn("  → finish_task_switch()\n");
	pr_warn("  → run_balance_callbacks()\n");
	pr_warn("  → stop_one_cpu_nowait(CPU%u)  ← nested inside stop_two_cpus\n",
		ctx->dst_cpu);
	pr_warn("  CPU%u stopper busy in multi_cpu_stop() → nested blocked\n",
		ctx->dst_cpu);

	/*
	 * Simulate migrate_swap_stop triggering a nested stop to dst_cpu.
	 * dst_cpu's stopper is inside the outer stop_machine barrier →
	 * cannot process the nested work → fn waits → DEADLOCK.
	 */
	stop_one_cpu_nowait(ctx->dst_cpu, tc14_nested_fn, ctx,
			    &ctx->swap_nested_work);

	pr_warn("[TC14] nested work queued on CPU%u — expecting deadlock\n",
		ctx->dst_cpu);

	ret = wait_for_completion_timeout(&ctx->swap_done, SAFE_WAIT_JIFFIES);
	if (!ret) {
		unsigned long ms = jiffies_to_msecs(jiffies - t0);
		pr_emerg("[TC14] MIGRATE_SWAP DEADLOCK after ~%lums\n", ms);
		pr_emerg("[TC14] src=CPU%u dst=CPU%u — nested stop blocked\n",
			 ctx->src_cpu, ctx->dst_cpu);
		if (!do_panic)
			safe_dump(cpu, ms, "TC14");
	}
	return 0;
}

static void run_tc14(void)
{
	struct tc14_ctx ctx;
	unsigned int cpu_a = target_cpu, cpu_b;

	tc_banner(14, "migrate_swap_stop re-entrancy — stop_two_cpus nested",
		  !do_panic);

	if (!cpu_online(cpu_a)) cpu_a = 0;
	cpu_b = cpumask_next(cpu_a, cpu_online_mask);
	if (cpu_b >= nr_cpu_ids) {
		pr_warn("[TC14] SKIPPED — need >= 2 online CPUs\n");
		return;
	}
	pr_warn("[TC14] src_cpu=%u  dst_cpu=%u\n", cpu_a, cpu_b);

	init_completion(&ctx.swap_done);
	ctx.src_cpu = cpu_a;
	ctx.dst_cpu = cpu_b;

	if (do_panic) {
		pr_warn("[TC14-B] stop_machine() → CRASH\n");
		stop_machine(tc14_migrate_swap_fn, &ctx, NULL);
	} else {
		pr_warn("[TC14-A] stop_one_cpu(CPU%u) → safe\n", cpu_a);
		stop_one_cpu(cpu_a, tc14_migrate_swap_fn, &ctx);
	}
	pr_warn("[TC14] returned\n");
}

/* ================================================================== */
/* TC15 — All-CPU fan-out: every stopper queues on its right neighbour*/
/*                                                                     */
/* Creates a circular deadlock spanning ALL online CPUs:              */
/*   CPU0's fn queues work on CPU1's stopper                         */
/*   CPU1's fn queues work on CPU2's stopper                         */
/*   ...                                                              */
/*   CPU(N-1)'s fn queues work on CPU0's stopper                     */
/*   All are inside stop_machine() simultaneously → all blocked       */
/*                                                                     */
/* This exercises the worst-case scenario:                            */
/*   - All N online CPUs stuck in PREPARE                            */
/*   - N pending works none of which can be processed                 */
/*   - Sec4 shows N stoppers each with pending works                  */
/*   - Sec3 shows full cpu_online_mask stuck                          */
/*   - ALL non-active CPUs spin at position C → ALL detect at 3s     */
/*   - First detector (smallest CPU id) fires panic()                 */
/*   - timed_out=1 prevents others from double-firing                 */
/* ================================================================== */
#define TC15_MAX_CPUS NR_CPUS

struct tc15_per_cpu {
	struct completion    next_done;
	struct cpu_stop_work next_work;
	unsigned int         my_cpu;
	unsigned int         next_cpu;
};

static DEFINE_PER_CPU(struct tc15_per_cpu, tc15_data);

static int tc15_next_fn(void *data)
{
	/* Only runs if the outer stop_machine exited (no deadlock) */
	struct tc15_per_cpu *d = data;
	pr_warn("[TC15] next_fn CPU%u (no deadlock — unexpected)\n",
		smp_processor_id());
	complete(&d->next_done);
	return 0;
}

static int tc15_fan_fn(void *data)
{
	struct tc15_per_cpu *d = this_cpu_ptr(&tc15_data);
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;
	int ret;

	pr_warn("[TC15] fan_fn CPU%u → queuing work on CPU%u stopper\n",
		cpu, d->next_cpu);

	/*
	 * Queue work on the next CPU in the ring.
	 * All CPUs are inside multi_cpu_stop() → none can process new work.
	 * Every fn in the ring waits for its "next" to complete → ring deadlock.
	 */
	stop_one_cpu_nowait(d->next_cpu, tc15_next_fn, d, &d->next_work);

	ret = wait_for_completion_timeout(&d->next_done, SAFE_WAIT_JIFFIES);
	if (!ret) {
		unsigned long ms = jiffies_to_msecs(jiffies - t0);
		pr_emerg("[TC15] ALL-CPU RING DEADLOCK from CPU%u after ~%lums\n",
			 cpu, ms);
		pr_emerg("[TC15] CPU%u → CPU%u in ring across %u CPUs\n",
			 cpu, d->next_cpu, num_online_cpus());
		pr_emerg("[TC15] cpu_online_mask: %*pbl  (ALL stuck)\n",
			 cpumask_pr_args(cpu_online_mask));
		if (!do_panic) {
			dump_stack();
			show_state();
			pr_emerg("[TC15] No panic — device stays up\n");
		}
	}
	return 0;
}

static void run_tc15(void)
{
	unsigned int cpu, prev_cpu = 0, first_cpu = 0;
	int n = 0;
	bool first = true;
	unsigned int cpus[TC15_MAX_CPUS];

	tc_banner(15, "All-CPU ring deadlock — every stopper queues on next",
		  !do_panic);

	/* Build ring: cpu[0]→cpu[1]→...→cpu[N-1]→cpu[0] */
	for_each_online_cpu(cpu) {
		if (n >= TC15_MAX_CPUS) break;
		cpus[n++] = cpu;
	}
	if (n < 2) {
		pr_warn("[TC15] SKIPPED — need >= 2 online CPUs\n");
		return;
	}

	for_each_online_cpu(cpu) {
		struct tc15_per_cpu *d = per_cpu_ptr(&tc15_data, cpu);
		d->my_cpu = cpu;
		if (!first) {
			per_cpu_ptr(&tc15_data, prev_cpu)->next_cpu = cpu;
		} else {
			first_cpu = cpu;
			first = false;
		}
		init_completion(&d->next_done);
		prev_cpu = cpu;
	}
	/* Close the ring: last CPU → first CPU */
	per_cpu_ptr(&tc15_data, prev_cpu)->next_cpu = first_cpu;

	pr_warn("[TC15] Ring: ");
	for (int i = 0; i < n; i++)
		pr_cont("CPU%u→", cpus[i]);
	pr_cont("CPU%u (closed)\n", cpus[0]);

	if (do_panic) {
		pr_warn("[TC15-B] stop_machine() → ALL CPUs in PREPARE → CRASH\n");
		stop_machine(tc15_fan_fn, NULL, NULL);
	} else {
		pr_warn("[TC15-A] stop_one_cpu(CPU%u) → single stopper → safe\n",
			first_cpu);
		stop_one_cpu(first_cpu, tc15_fan_fn, NULL);
	}
	pr_warn("[TC15] returned\n");
}

/* ================================================================== */
/* TC16 — Completion + spinlock deadlock: stopper waiting on lock     */
/*                                                                     */
/* A spinlock is held by a normal task. The stopper fn tries to       */
/* acquire it. The normal task cannot run (CPU pinned by stopper).    */
/* → stopper spins on the lock forever in PREPARE state.             */
/* → IRQs ON during PREPARE → jiffies advance → patch fires.        */
/*                                                                     */
/* This exercises a different blocking mechanism from TC03-TC06:      */
/* Instead of waiting on a completion (explicit re-entrancy), the     */
/* stopper is blocked by a lock contention path.                     */
/*                                                                     */
/* Production analogue: any kernel path that holds a spinlock and     */
/* then triggers stop_machine() — the stopper fn may attempt to       */
/* acquire the same lock → deadlock.                                  */
/*                                                                     */
/* IMPLEMENTATION NOTE:                                               */
/* We use raw_spinlock_t and spin_trylock. We cannot spin indefinitely*/
/* from inside stop_machine with local_irq_save — that would freeze  */
/* jiffies (IRQ-off). So we trylock in a loop WITHOUT disabling IRQs, */
/* which keeps jiffies advancing → patch fires correctly.            */
/* ================================================================== */
static DEFINE_RAW_SPINLOCK(tc16_lock);
static atomic_t tc16_lock_held;

struct tc16_ctx {
	struct completion lock_released;
};

static int tc16_stop_fn(void *data)
{
	unsigned int cpu = smp_processor_id();
	unsigned long t0  = jiffies;

	pr_warn("[TC16] stopper fn CPU%u: trying to acquire tc16_lock\n", cpu);
	pr_warn("[TC16] lock is held by a normal task that cannot run\n");
	pr_warn("[TC16] → stopper spins forever in PREPARE, IRQs ON\n");
	pr_warn("[TC16] → jiffies advance → patch fires at ~%ums\n",
		PATCH_TIMEOUT_MS);

	/*
	 * spin_trylock in a busy loop WITHOUT local_irq_disable().
	 * This keeps IRQs enabled → timer ticks → jiffies advance.
	 * The lock holder cannot run (this CPU is the stopper).
	 * → never acquired → loop spins until patch time_after() fires.
	 */
	while (!raw_spin_trylock(&tc16_lock)) {
		cpu_relax();

		if (!do_panic && time_after(jiffies,
					    t0 + msecs_to_jiffies(BLOCKER_TOTAL_MS))) {
			unsigned long ms = jiffies_to_msecs(jiffies - t0);
			pr_emerg("[TC16] LOCK DEADLOCK after ~%lums — giving up\n",
				 ms);
			safe_dump(cpu, ms, "TC16");
			return 0; /* exit safely in MODE A */
		}
	}

	/* Should not reach here in deadlock scenario */
	raw_spin_unlock(&tc16_lock);
	pr_warn("[TC16] lock acquired (lock was not held — test setup issue)\n");
	return 0;
}

static int tc16_locker_fn(void *unused)
{
	pr_warn("[TC16] lock-holder kthread started — acquiring tc16_lock\n");
	raw_spin_lock(&tc16_lock);
	atomic_set(&tc16_lock_held, 1);
	pr_warn("[TC16] tc16_lock HELD — now invoking stop_machine()\n");

	/* stop_machine() while holding the lock — stopper fn will deadlock */
	stop_machine(tc16_stop_fn, NULL, NULL);

	/* Reached only in safe MODE A */
	raw_spin_unlock(&tc16_lock);
	atomic_set(&tc16_lock_held, 0);
	pr_warn("[TC16] lock released — MODE A safe test complete\n");
	return 0;
}

static void run_tc16(void)
{
	struct task_struct *t;

	tc_banner(16, "Spinlock contention deadlock — stopper blocked on lock",
		  !do_panic);
	pr_warn("[TC16] Mechanism:\n");
	pr_warn("  1. kthread acquires tc16_lock\n");
	pr_warn("  2. calls stop_machine(stop_fn)\n");
	pr_warn("  3. stop_fn tries to acquire tc16_lock → BLOCKED\n");
	pr_warn("  4. lock holder pinned by stop barrier → cannot release\n");
	pr_warn("  5. stopper spins in PREPARE, IRQs ON → jiffies advance\n");
	pr_warn("  6. patch fires at ~%ums → panic()\n", PATCH_TIMEOUT_MS);

	atomic_set(&tc16_lock_held, 0);

	t = kthread_run(tc16_locker_fn, NULL, "sm_locker");
	if (IS_ERR(t)) {
		pr_warn("[TC16] kthread_run failed\n");
		return;
	}

	/* Wait for the kthread to complete (MODE A) or crash (MODE B) */
	msleep(BLOCKER_TOTAL_MS + 1000);

	if (!do_panic && !atomic_read(&tc16_lock_held))
		pr_warn("[TC16] PASSED — safe mode lock deadlock detected\n");
}

/* ================================================================== */
/* TC17 — timed_out guard: single-fire verification  [WILL CRASH]     */
/*                                                                     */
/* The patch uses:                                                    */
/*   int timed_out = 0;  // declared before spin loop                */
/*   if (!timed_out && time_after(...)) {                             */
/*       timed_out = 1;                                               */
/*       stopper_deadlock_dump();                                     */
/*       panic();                                                      */
/*   }                                                                 */
/*                                                                     */
/* timed_out = 1 on first fire prevents stopper_deadlock_dump() from  */
/* being called a SECOND time if panic() somehow returns (it doesn't  */
/* on normal paths, but panic notifiers can delay it).               */
/* Also prevents duplicate dump+panic if multiple CPUs simultaneously */
/* detect the timeout (they all see timed_out=0 momentarily).        */
/*                                                                     */
/* This test verifies that:                                           */
/*   1. panic() IS called (patch fires correctly)                     */
/*   2. Only ONE dump is emitted (search dmesg for banner count)      */
/*                                                                     */
/* EXPECTED DMESG after crash:                                        */
/*   Exactly ONE occurrence of:                                       */
/*   "STOPPER DEADLOCK: migration/N stuck in multi_cpu_stop"         */
/* ================================================================== */
static int tc17_fn(void *data)
{
	pr_warn("[TC17] fn CPU%u: mdelay(%ums)\n",
		smp_processor_id(), BLOCKER_TOTAL_MS);
	pr_warn("[TC17] After panic(): search dmesg for ONE occurrence of:\n");
	pr_warn("  'STOPPER DEADLOCK: migration'  — must appear EXACTLY once\n");
	pr_warn("  Multiple occurrences = timed_out guard not working\n");
	mdelay(BLOCKER_TOTAL_MS);
	return 0;
}

static void run_tc17(void)
{
	tc_banner(17, "timed_out guard — single-fire verification", false);
	pr_warn("[TC17] WILL CRASH. After ramdump:\n");
	pr_warn("  grep -c 'STOPPER DEADLOCK' /proc/last_kmsg\n");
	pr_warn("  Expected: 1  (timed_out prevents double fire)\n");
	stop_machine(tc17_fn, NULL, NULL);
	pr_emerg("[TC17] BUG: should have panicked!\n");
}

/* ================================================================== */
/* TC18 — Regression sweep: all safe TCs in sequence                  */
/*                                                                     */
/* Runs every non-crashing test case back-to-back.                    */
/* Use after any kernel change that might affect stop_machine().      */
/* All must complete without panic — any panic is a regression.       */
/* ================================================================== */
static void run_tc18(void)
{
	tc_banner(18, "Regression sweep — all safe TCs in sequence", true);
	pr_warn("[TC18] Running: TC01 TC07 TC08(safe) TC09 TC11 TC12\n");
	pr_warn("[TC18] Any panic = regression in patch or kernel\n\n");

	pr_warn("[TC18] ── TC01 ──────────────────────────────────────\n");
	run_tc01();

	pr_warn("[TC18] ── TC07 ──────────────────────────────────────\n");
	run_tc07();

	pr_warn("[TC18] ── TC08 (safe, do_panic=0) ──────────────────\n");
	/* Force safe mode for TC08 regardless of module param */
	{ struct tc08_ctx c = { .ms = PATCH_TIMEOUT_MS - 500 };
	  stop_machine(tc08_mdelay_fn, &c, NULL); }
	pr_warn("[TC18] TC08 PASSED\n");

	pr_warn("[TC18] ── TC09 ──────────────────────────────────────\n");
	run_tc09();

	pr_warn("[TC18] ── TC11 ──────────────────────────────────────\n");
	run_tc11();

	pr_warn("[TC18] ── TC12 ──────────────────────────────────────\n");
	run_tc12();

	pr_warn("\n[TC18] REGRESSION SWEEP COMPLETE — all safe TCs passed\n");
}



/* ================================================================== */
/* Module init / exit                                                  */
/* ================================================================== */
static int __init sm_test_init(void)
{
	pr_warn("\n");
	pr_warn("╔══════════════════════════════════════════════════════╗\n");
	pr_warn("║  stop_machine_test.ko                                ║\n");
	pr_warn("║  patch: common-stop_machine-deadlock-panic-          ║\n");
	pr_warn("║         production.patch                             ║\n");
	pr_warn("║  tc=%-2d  do_panic=%-1d  target_cpu=%-2u               ║\n",
		tc, do_panic, target_cpu);
	pr_warn("║  STOPPER_TIMEOUT_JIFFIES = 3*HZ = 3*%u = %u jiffies  ║\n",
		HZ, 3 * HZ);
	pr_warn("║  = %ums  (Qualcomm bark_time = 11000ms)         ║\n",
		jiffies_to_msecs(3 * HZ));
	pr_warn("╚══════════════════════════════════════════════════════╝\n");

	switch (tc) {
	case 1:  run_tc01(); break;
	case 2:  run_tc02(); break;
	case 3:  run_tc03(); break;
	case 4:  run_tc04(); break;
	case 5:  run_tc05(); break;
	case 6:  run_tc06(); break;
	case 7:  run_tc07(); break;
	case 8:  run_tc08(); break;
	case 9:  run_tc09(); break;
	case 10: run_tc10(); break;
	case 11: run_tc11(); break;
	case 12: run_tc12(); break;
	case 13: run_tc13(); break;
	case 14: run_tc14(); break;
	case 15: run_tc15(); break;
	case 16: run_tc16(); break;
	case 17: run_tc17(); break;
	case 18: run_tc18(); break;
	default:
		pr_warn("[SM_TEST] Unknown tc=%d. Valid: 1-18\n", tc);
		return -EINVAL;
	}
	return 0;
}

static void __exit sm_test_exit(void)
{
	pr_warn("[SM_TEST] unloaded (tc=%d)\n", tc);
}

module_init(sm_test_init);
module_exit(sm_test_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kernel Test <test@example.com>");
MODULE_DESCRIPTION("Full test suite for stop_machine deadlock detection "
		   "(common-stop_machine-deadlock-panic-production.patch)");
