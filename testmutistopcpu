Testing Guide: stop_machine Deadlock Detection Patch
Platform: Qualcomm Android16/GKI, Linux 6.12.58, HZ=250, NO_HZ_IDLE
Patch Application Order
# 1. Apply production patch first
git apply 0001-ANDROID-stop_machine-migration-deadlock-panic-v3.patch

# 2. Apply test hooks patch on top (DO NOT MERGE to production)
git apply 0002-TEST-stop_machine-debugfs-test-hooks.patch

# 3. Build kernel
make -j$(nproc) ARCH=arm64 Image.gz

# 4. Verify both patches applied cleanly
git log --oneline -2
Test 1: Safe Dump Format Verification (no panic)
Purpose
Verify all 7 diagnostic sections print correctly without crashing the device.
Check symbol resolution, cpumask format, IPI delivery.
Steps
# Boot device with test kernel

# Trigger the dump (safe — no panic)
echo 1 > /sys/kernel/debug/stop_machine/trigger_dump

# Check dmesg immediately
dmesg | grep -A 200 "STOPPER DEADLOCK"
Expected dmesg output
[  x.xxx] =====================================================
[  x.xxx] [TEST TRIGGER] Synthetic stopper deadlock dump
[  x.xxx]   Simulating: migration/N stuck for ~3005 ms
[  x.xxx]   NOTE: This is a TEST — state/masks are real, msdata is fake
[  x.xxx] =====================================================
[  x.xxx] === offending CPU4 stopper (real current stopper) ===
[  x.xxx]   outer fn    : (null)
[  x.xxx]   outer caller: (null)
[  x.xxx] === multi_stop_data (SYNTHETIC) ===
[  x.xxx]   state       : 1 (PREPARE)
[  x.xxx]   num_threads : 2
[  x.xxx]   thread_ack  : 1  <- should be 1 (one CPU not acked)
[  x.xxx]   inner fn    : (null)
[  x.xxx]   active_cpus : 0-7
[  x.xxx] === CPU masks (REAL) ===
[  x.xxx]   cpu_online_mask : 0-7
[  x.xxx]   cpu_active_mask : 0-7
[  x.xxx]   cpu_dying_mask  : 0  (0 = load-balancer variant)
[  x.xxx] === per-CPU stopper state (REAL) ===
[  x.xxx]   [CPU0 stopper] task=migration/0    enabled=1
[  x.xxx]                  fn=(null)  caller=(null)
[  x.xxx]     pending works: (none)
         ... (one entry per online CPU)
[  x.xxx] === call stack of trigger thread (REAL) ===
[  x.xxx] Call trace:
[  x.xxx]  stopper_test_dump_write+0x...
[  x.xxx]  vfs_write+0x...
[  x.xxx]  ...
[  x.xxx] === IPI backtrace for ALL CPUs (REAL) ===
[  x.xxx] CPU0: ...
[  x.xxx] CPU1: ...
[  x.xxx] === all task states (REAL SysRq-T) ===
[  x.xxx] task:migration/0    state:S ...
         ...
[  x.xxx] =====================================================
[  x.xxx] [TEST TRIGGER COMPLETE] No panic was called.
[  x.xxx] If all sections printed correctly, the dump code works.
[  x.xxx] =====================================================
Verification checklist
[ ] All 7 sections appear in dmesg
[ ] cpu_online_mask shows correct bitmask for your platform (e.g. 0-7)
[ ] cpu_dying_mask shows 0 (no hotplug in progress)
[ ] Per-CPU stopper entries appear for each online CPU
[ ] Call trace: section shows stopper_test_dump_write at top
[ ] IPI backtrace section shows a stack from each CPU
[ ] No kernel panic occurred — device stayed up
Test 2: Real Timeout Detection (safe — reduces timeout first)
Purpose
Trigger the REAL jiffies-based timeout detection path with a 1-second timeout
instead of 3 seconds, so you don't have to wait long. This calls the real
stopper_deadlock_dump() (not the test wrapper) but still without panic.
Note: This test only uses the module_param override. The dump path
is the real production code.
Steps
# Step 1: Reduce timeout to 1 second (default is 3s at HZ=250=250 jiffies)
echo 1 > /sys/module/stop_machine/parameters/stopper_timeout_sec

# Step 2: Verify it took effect
cat /sys/module/stop_machine/parameters/stopper_timeout_sec
# Expected: 1

# Step 3: Run scheduler stress to trigger active load balancing
#         This exercises the real multi_cpu_stop path frequently
taskset -c 0-3 stress-ng --sched 4 --sched-ops 500000 &
STRESS_PID=$!

# Optional: also stress CPU affinity changes
taskset -c 0-3 stress-ng --affinity 4 --affinity-ops 50000 &

# Step 4: Watch dmesg for the timeout
dmesg -w | grep -E "STOPPER|migration.*stuck|Nested stop_machine"

# Step 5: After test, restore default timeout
wait $STRESS_PID
echo 3 > /sys/module/stop_machine/parameters/stopper_timeout_sec
Alternative: hackbench (triggers more scheduler activity)
echo 1 > /sys/module/stop_machine/parameters/stopper_timeout_sec
hackbench -g 8 -l 50000 -s 512
dmesg | tail -50
echo 3 > /sys/module/stop_machine/parameters/stopper_timeout_sec
Test 3: Full Panic Simulation (WILL CRASH — use on ramdump-enabled device)
Purpose
Verify the COMPLETE end-to-end flow: timeout fires -> stopper_deadlock_dump()
-> panic() -> ramdump captured -> post-mortem analysis possible.
Prerequisites
Device connected to serial console
Ramdump host configured (QCOM CrashScope / DRAM dump mode)
stopper_timeout_sec parameter accessible
Steps
# Step 1: Configure ramdump mode before test
# (Platform specific - ensure crash.img capture is ready)

# Step 2: Reduce timeout for faster test
echo 1 > /sys/module/stop_machine/parameters/stopper_timeout_sec

# Step 3: Trigger the crash (DEVICE WILL REBOOT)
echo 1 > /sys/kernel/debug/stop_machine/simulate_panic

# Step 4: After reboot, extract ramdump
# Check panic string in ramdump/serial log:
Expected serial/dmesg before reset
[  x.xxx] stop_machine: [TEST simulate_panic] DEVICE WILL CRASH IN 3s
[  x.xxx] stop_machine: [TEST] timeout threshold = 1s
[  x.xxx] stop_machine: [TEST] calling stop_machine() with blocking fn
[  x.xxx] stop_machine: [TEST simulate_panic] sleeping 3s in stopper

... (after 1s * HZ = 250 jiffies) ...

[  x.xxx] =====================================================
[  x.xxx] STOPPER DEADLOCK: migration/0 stuck in
[  x.xxx] multi_cpu_stop for ~1004 ms
[  x.xxx] Suspected cause: nested cpu_stop_queue_work() re-entrancy
           ...
[  x.xxx] =====================================================
[  x.xxx] Calling panic() to trigger ramdump/minidump.
[  x.xxx] bark_time (~9-11s) > 3s threshold => ramdump wins.
[  x.xxx] =====================================================
[  x.xxx] Kernel panic - not syncing: migration/0: stuck in multi_cpu_stop
          for ~1004 ms | state=RUN thread_ack=7 stop_fn=stopper_test_panic_fn
          | nested stop_machine deadlock | see ramdump for full trace
Post-mortem verification in Trace32
# After loading ramdump
CORE.ATTACH
# Check panic string
Data.String v.value("&panic_buf")
# Check which CPU detected it
Var.view multi_stop_data
# Verify call stack
CORE.select 0
TASK.Stack.View migration/0
Test 4: Verify NO_HZ_IDLE Does Not Cause False Positives
Purpose
Confirm that the jiffies-based detection does NOT fire during normal
(non-deadlocked) stop_machine() calls.
Steps
# Set aggressive timeout
echo 1 > /sys/module/stop_machine/parameters/stopper_timeout_sec

# Run CPU hotplug repeatedly (exercises stop_machine heavily)
for i in $(seq 1 50); do
  echo 0 > /sys/devices/system/cpu/cpu7/online
  echo 1 > /sys/devices/system/cpu/cpu7/online
done

# Run module load/unload (uses stop_machine internally)
for i in $(seq 1 20); do
  modprobe dummy_hcd 2>/dev/null
  modprobe -r dummy_hcd 2>/dev/null
done

# Check: NO "STOPPER DEADLOCK" in dmesg
dmesg | grep "STOPPER DEADLOCK"
# Expected: (empty — no false positives)

# Restore
echo 3 > /sys/module/stop_machine/parameters/stopper_timeout_sec
Test 5: Verify IRQ-off States Do Not False-Positive
Purpose
The DISABLE_IRQ and RUN states of stop_machine intentionally disable IRQs.
jiffies should freeze during these states so the timeout never fires.
Steps
# Set very aggressive 1s timeout
echo 1 > /sys/module/stop_machine/parameters/stopper_timeout_sec

# Use stop_machine with a callback that takes ~500ms (shorter than 1s timeout)
# If the IRQ-off detection is correct, this should NOT panic

# stress-ng --cpu runs a stop_machine briefly during CPU freq changes
stress-ng --cpu 0 --cpu-ops 1000 --timeout 30s

# Check no spurious panics
dmesg | grep -E "STOPPER|panic"

echo 3 > /sys/module/stop_machine/parameters/stopper_timeout_sec
Decision Matrix: Which Test to Run First
Goal                                    Test
─────────────────────────────────────────────────────────────
Verify dump format is correct           Test 1 (trigger_dump)
Verify jiffies detection fires          Test 2 (stress-ng + timeout=1)
Verify full ramdump capture             Test 3 (simulate_panic) ← CRASHES
Verify no false positives (hotplug)     Test 4 (CPU hotplug loop)
Verify no false positives (IRQ-off)     Test 5 (cpu-stress)
Recommended order for first validation:
Test 1 (safe, verify output format)
Test 4 (safe, verify no false positives)
Test 5 (safe, verify IRQ-off safety)
Test 2 (safe, verify detection fires)
Test 3 (CRASH — do last on dedicated ramdump device)
Cleanup: Remove Test Hooks Before Production Build
# Revert the test hooks patch only
git revert HEAD  # reverts 0002-TEST patch

# Verify production patch still in place
git log --oneline -1
# Should show: ANDROID: stop_machine: panic with full CPU dump...

# Build production image
make -j$(nproc) ARCH=arm64 Image.gz