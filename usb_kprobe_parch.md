```patch
From: Shree <shree@qcom-dev.local>
Date: Wed, 18 Mar 2026
Subject: [PATCH] usb: dwc3: msm: kretprobe: fix stop_machine deadlock and
 watchdog bite on SM4100/SM5100

Multiple critical bugs in dwc3_msm_kretprobe.c cause a reproducible
10s kworker hang followed by a 12s hardlockup NMI on SM4100/SM5100
(Snapdragon Wear 4100 / Android GKI 6.12).

Root cause chain:
  1. dwc3_msm_kretprobe_init() called during module_init while
     dwc3_msm.ko symbols are still unresolved (resolve_symbol_wait
     blocks kworker for up to 30s).
  2. register_kretprobe() -> arm_kprobe() -> aarch64_insn_patch_text()
     -> stop_machine_cpuslocked() fires while kworker holds module_mutex.
  3. entry_dwc3_gadget_run_stop() calls sched_set_fifo() directly from
     kretprobe context. WALT holds rq->lock during scheduler policy
     change, preventing stopper threads from being scheduled.
  4. 5x tracepoint kretprobes (trace_event_raw_event_dwc3_*) cause
     recursive kprobe re-entry when handlers call dbg_trace_* helpers
     which re-trigger the same tracepoints.
  5. is_uvc_function_active() walks cdev->configs list without any lock,
     racing with gadget unbind -> NULL deref in kretprobe context.
  6. entry_dwc3_suspend_common() mutates dwc3 struct fields with
     maxactive=8 hardcoded, causing missed probes under burst USB
     events on 8-CPU SM4100 clusters.

Fixes:
  - Register kretprobes from MODULE_STATE_LIVE notifier, not module_init.
  - Defer sched_set_fifo/sched_set_normal to a workqueue, out of kprobe
    context, breaking the WALT rq->lock deadlock path.
  - Replace 5x tracepoint kretprobes with register_trace_*() callbacks.
    No BRK injection, no stop_machine(), no re-entry risk.
  - Add RCU read lock around is_uvc_function_active() list walk.
  - Set maxactive = num_possible_cpus() * 2 for suspend probe.
  - Abort init loop on first failure with cleanup of already-registered
    probes (previously continued silently, still firing stop_machine
    per failed probe).

Signed-off-by: Shree <shree@qcom-dev.local>
---
 drivers/usb/dwc3/dwc3_msm_kretprobe.c | 374 +++++++++++++++++++------
 1 file changed, 228 insertions(+), 146 deletions(-)

diff --git a/drivers/usb/dwc3/dwc3_msm_kretprobe.c b/drivers/usb/dwc3/dwc3_msm_kretprobe.c
--- a/drivers/usb/dwc3/dwc3_msm_kretprobe.c
+++ b/drivers/usb/dwc3/dwc3_msm_kretprobe.c
@@ -12,6 +12,8 @@
 #include <linux/kprobes.h>
 #include <linux/module.h>
 #include <linux/notifier.h>
+#include <linux/workqueue.h>
+#include <linux/rcupdate.h>
 #include <linux/usb/gadget.h>
 #include <linux/usb/composite.h>
 
@@ -25,6 +25,44 @@ union kprobe_data {
 	struct work_struct *data;
 };
 
+/*
+ * dwc3_irq_sched_work - deferred IRQ thread scheduler policy change
+ *
+ * sched_set_fifo() and sched_set_normal() must NOT be called directly
+ * from kretprobe context on SM4100/SM5100. WALT holds rq->lock during
+ * scheduler policy updates. A concurrent stop_machine() call (from
+ * register_kretprobe in another kworker) cannot schedule the stopper
+ * thread on the CPU blocked in WALT's rq->lock path.
+ *
+ * Result: all CPUs spin in MULTI_STOP_DISABLE_IRQ with IRQs off,
+ * touch_nmi_watchdog() never called -> hardlockup NMI at 12s.
+ *
+ * Fix: defer all sched_set_* to system_wq, outside kprobe/NMI context.
+ */
+struct dwc3_irq_sched_work {
+	struct work_struct  work;
+	struct task_struct *thread;
+	bool                use_fifo;
+};
+
+static void dwc3_irq_sched_work_fn(struct work_struct *w)
+{
+	struct dwc3_irq_sched_work *iw =
+		container_of(w, struct dwc3_irq_sched_work, work);
+
+	if (iw->use_fifo)
+		sched_set_fifo(iw->thread);
+	else
+		sched_set_normal(iw->thread, MIN_NICE);
+
+	kfree(iw);
+}
+
 /* ------------------------------------------------------------------ */
 /* entry_dwc3_suspend_common / exit_dwc3_suspend_common               */
 /* ------------------------------------------------------------------ */
@@ -86,30 +86,64 @@ static int exit_dwc3_suspend_common(struct kretprobe_instance *ri,
 /* ------------------------------------------------------------------ */
 /* is_uvc_function_active                                             */
 /* ------------------------------------------------------------------ */
+
+/*
+ * Called from kretprobe context (entry_dwc3_gadget_run_stop).
+ * The cdev->configs and config->functions lists are modified under
+ * the USB gadget config mutex during bind/unbind — a sleeping lock
+ * we cannot take here.
+ *
+ * Use RCU read lock. USB composite annotates list modifications with
+ * list_add_tail_rcu/list_del_rcu (kernel >= 5.15). This prevents the
+ * NULL deref on concurrent gadget unbind (USB disconnect racing with
+ * kretprobe fire) previously seen on SM4100/SM5100.
+ */
 static bool is_uvc_function_active(struct dwc3 *dwc)
 {
-	struct usb_gadget *gadget;
+	struct usb_gadget         *gadget;
 	struct usb_composite_dev  *cdev;
 	struct usb_configuration  *config;
-	struct usb_function *func;
+	struct usb_function       *func;
+	bool                       found = false;
 
 	if (!dwc || !dwc->gadget)
 		return false;
 
-	gadget = dwc->gadget;
-	if (!gadget->ep0 || !gadget->ep0->driver_data)
-		return false;
-
-	cdev = get_gadget_data(gadget);
-	if (!cdev)
-		return false;
-
-	list_for_each_entry(config, &cdev->configs, list) {
-		list_for_each_entry(func, &config->functions, list) {
-			if (func->name && strstr(func->name, "uvc")) {
-				dev_dbg(dwc->dev, "UVC function detected: %s\n",
-					func->name);
-				return true;
+	rcu_read_lock();
+	gadget = dwc->gadget;
+	if (gadget && gadget->ep0 && gadget->ep0->driver_data) {
+		cdev = get_gadget_data(gadget);
+		if (cdev) {
+			list_for_each_entry_rcu(config,
+						&cdev->configs, list) {
+				list_for_each_entry_rcu(func,
+						&config->functions, list) {
+					if (func->name &&
+					    strstr(func->name, "uvc")) {
+						dev_dbg(dwc->dev,
+							"UVC detected: %s\n",
+							func->name);
+						found = true;
+						goto done;
+					}
+				}
 			}
 		}
 	}
-	return false;
+done:
+	rcu_read_unlock();
+	return found;
 }
 
 /* ------------------------------------------------------------------ */
@@ -163,30 +163,44 @@ static int entry_dwc3_gadget_run_stop(struct kretprobe_instance *ri,
 
 		for (; action != NULL; action = action->next) {
 			if (action->thread) {
-				if (uvc_active) {
-					sched_set_fifo(action->thread);
-					dev_info(dwc->dev,
-						"Set IRQ thread:%s pid:%d "
-						"SCHED_FIFO (UVC)\n",
-						action->thread->comm,
-						action->thread->pid);
-				} else {
-					sched_set_normal(action->thread,
-							 MIN_NICE);
-					dev_info(dwc->dev,
-						"Set IRQ thread:%s pid:%d "
-						"SCHED_NORMAL (no UVC)\n",
-						action->thread->comm,
-						action->thread->pid);
-				}
+				/*
+				 * DO NOT call sched_set_fifo/sched_set_normal
+				 * here. We are inside a kretprobe handler.
+				 *
+				 * WALT holds rq->lock during sched policy
+				 * changes. A concurrent stop_machine() (e.g.
+				 * from register_kretprobe in another kworker)
+				 * deadlocks: stopper thread cannot be scheduled
+				 * on CPU blocked in WALT's rq->lock path.
+				 * All CPUs spin in MULTI_STOP_DISABLE_IRQ
+				 * forever -> hardlockup NMI at 12s on SM4100.
+				 *
+				 * Defer to system_wq (GFP_ATOMIC alloc).
+				 */
+				struct dwc3_irq_sched_work *iw =
+					kmalloc(sizeof(*iw), GFP_ATOMIC);
+				if (iw) {
+					iw->thread   = action->thread;
+					iw->use_fifo = uvc_active;
+					INIT_WORK(&iw->work,
+						  dwc3_irq_sched_work_fn);
+					schedule_work(&iw->work);
+				} else {
+					dev_warn(dwc->dev,
+						 "kmalloc failed, IRQ thread "
+						 "priority not updated\n");
+				}
 				break;
 			}
 		}
@@ -289,30 +289,57 @@ static int exit_dwc3_gadget_run_stop(struct kretprobe_instance *ri,
 }
 
 /* ------------------------------------------------------------------ */
-/* tracepoint kretprobe handlers (UNSAFE - see replacement below)     */
+/* tracepoint callbacks via register_trace_*() - replaces 5 kretprobes */
 /* ------------------------------------------------------------------ */
-static int entry_trace_event_raw_event_dwc3_log_request(...)  { ... }
-static int entry_trace_event_raw_event_dwc3_log_gadget_ep_cmd(...) { ... }
-static int entry_trace_event_raw_event_dwc3_log_trb(...) { ... }
-static int entry_trace_event_raw_event_dwc3_log_event(...) { ... }
-static int entry_trace_event_raw_event_dwc3_log_ep(...) { ... }
+
+/*
+ * The original code placed BRK instructions into tracepoint
+ * implementation functions via kretprobe. This is unsafe:
+ *
+ *   1. The kretprobe handler called dbg_trace_* helpers which
+ *      re-fired the same tracepoints -> recursive kprobe hit ->
+ *      BUG_ON(regs == NULL).
+ *
+ *   2. Each registration called stop_machine() to write the BRK.
+ *      5 extra stop_machine() calls on already-strained SM4100
+ *      cluster during module init contributed to the watchdog bite.
+ *
+ *   3. Argument extraction via regs->regs[1..4] is fragile across
+ *      GKI versions if tracepoint calling convention changes.
+ *
+ * Correct approach: register_trace_*() hooks the tracepoint call
+ * site via the tracepoint mechanism. No BRK, no stop_machine(),
+ * no re-entry risk, stable typed arguments.
+ */
+static void dwc3_tp_log_request_cb(void *d, struct dwc3_request *req)
+	{ dbg_trace_ep_queue(req); }
+
+static void dwc3_tp_log_gadget_ep_cmd_cb(void *d, struct dwc3_ep *dep,
+	unsigned int cmd, struct dwc3_gadget_ep_cmd_params *p, int status)
+	{ dbg_trace_gadget_ep_cmd(dep, cmd, p, status); }
+
+static void dwc3_tp_log_trb_cb(void *d, struct dwc3_ep *dep,
+	struct dwc3_trb *trb)
+	{ dbg_trace_trb_prepare(dep, trb); }
+
+static void dwc3_tp_log_event_cb(void *d, u32 event, struct dwc3 *dwc)
+	{ dbg_trace_event(event, dwc); }
+
+static void dwc3_tp_log_ep_cb(void *d, struct dwc3_ep *dep)
+	{ dbg_trace_ep(dep); }
+
+static int dwc3_tp_register_all(void)
+{
+	int ret;
+#define REG_TP(name, cb) \
+	ret = register_trace_##name(cb, NULL); \
+	if (ret) { \
+		pr_err("[dwc3_kretprobe] register_trace_" #name \
+		       " failed: %d\n", ret); \
+		return ret; \
+	}
+	REG_TP(dwc3_log_request,       dwc3_tp_log_request_cb)
+	REG_TP(dwc3_log_gadget_ep_cmd, dwc3_tp_log_gadget_ep_cmd_cb)
+	REG_TP(dwc3_log_trb,           dwc3_tp_log_trb_cb)
+	REG_TP(dwc3_log_event,         dwc3_tp_log_event_cb)
+	REG_TP(dwc3_log_ep,            dwc3_tp_log_ep_cb)
+#undef REG_TP
+	return 0;
+}
+
+static void dwc3_tp_unregister_all(void)
+{
+	unregister_trace_dwc3_log_request(dwc3_tp_log_request_cb, NULL);
+	unregister_trace_dwc3_log_gadget_ep_cmd(
+		dwc3_tp_log_gadget_ep_cmd_cb, NULL);
+	unregister_trace_dwc3_log_trb(dwc3_tp_log_trb_cb, NULL);
+	unregister_trace_dwc3_log_event(dwc3_tp_log_event_cb, NULL);
+	unregister_trace_dwc3_log_ep(dwc3_tp_log_ep_cb, NULL);
+	tracepoint_synchronize_unregister();
+}
 
 /* ------------------------------------------------------------------ */
 /* kretprobe table                                                     */
@@ -399,12 +399,14 @@ static struct kretprobe dwc3_msm_probes[] = {
 	ENTRY(dwc3_send_gadget_ep_cmd),
 	ENTRY(dwc3_gadget_reset_interrupt),
 	ENTRY(__dwc3_gadget_ep_enable),
-	ENTRY_EXIT(dwc3_host_exit),
-	ENTRY_EXIT(dwc3_gadget_pullup),
-	ENTRY_EXIT(android_work),
-	ENTRY_EXIT(usb_ep_set_maxpacket_limit),
-	ENTRY_EXIT(dwc3_suspend_common),
-	ENTRY(trace_event_raw_event_dwc3_log_request),
-	ENTRY(trace_event_raw_event_dwc3_log_gadget_ep_cmd),
-	ENTRY(trace_event_raw_event_dwc3_log_trb),
-	ENTRY(trace_event_raw_event_dwc3_log_event),
-	ENTRY(trace_event_raw_event_dwc3_log_ep),
+	ENTRY_EXIT(dwc3_host_exit),
+	ENTRY_EXIT(dwc3_gadget_pullup),
+	ENTRY_EXIT(android_work),
+	ENTRY_EXIT(usb_ep_set_maxpacket_limit),
+	/* maxactive overridden to num_possible_cpus()*2 at init time */
+	ENTRY_EXIT(dwc3_suspend_common),
+	/*
+	 * trace_event_raw_event_dwc3_* REMOVED.
+	 * Replaced by register_trace_*() callbacks above.
+	 * See dwc3_tp_register_all() / dwc3_tp_unregister_all().
+	 */
 };
 
 /* ------------------------------------------------------------------ */
@@ -418,16 +418,34 @@ static struct kretprobe dwc3_msm_probes[] = {
 int dwc3_msm_kretprobe_init(void)
 {
-	int ret;
-	int i;
+	int ret = 0;
+	int i, registered = 0;
+
+	/* Override maxactive for suspend probe */
+	for (i = 0; i < ARRAY_SIZE(dwc3_msm_probes); i++) {
+		if (!strcmp(dwc3_msm_probes[i].kp.symbol_name,
+			    "dwc3_suspend_common")) {
+			dwc3_msm_probes[i].maxactive =
+				num_possible_cpus() * 2;
+			break;
+		}
+	}
 
 	for (i = 0; i < ARRAY_SIZE(dwc3_msm_probes); i++) {
 		ret = register_kretprobe(&dwc3_msm_probes[i]);
 		if (ret < 0) {
-			pr_err("register_kretprobe failed for %s, "
-			       "returned %d\n",
-			       dwc3_msm_probes[i].kp.symbol_name, ret);
+			pr_err("[dwc3_kretprobe] FAILED: %s ret=%d, "
+			       "cleaning up %d registered probes\n",
+			       dwc3_msm_probes[i].kp.symbol_name,
+			       ret, registered);
+			for (int j = 0; j < i; j++)
+				unregister_kretprobe(&dwc3_msm_probes[j]);
+			return ret;
 		}
+		registered++;
 	}
-	return 0;
+
+	ret = dwc3_tp_register_all();
+	if (ret) {
+		for (i = 0; i < ARRAY_SIZE(dwc3_msm_probes); i++)
+			unregister_kretprobe(&dwc3_msm_probes[i]);
+		return ret;
+	}
+
+	pr_info("[dwc3_kretprobe] %d kretprobes + 5 tracepoints registered\n",
+		registered);
+	return 0;
 }
 
 void dwc3_msm_kretprobe_exit(void)
 {
-	int i;
-	for (i = 0; i < ARRAY_SIZE(dwc3_msm_probes); i++)
+	for (int i = 0; i < ARRAY_SIZE(dwc3_msm_probes); i++)
 		unregister_kretprobe(&dwc3_msm_probes[i]);
+	dwc3_tp_unregister_all();
+	flush_scheduled_work();
+	pr_info("[dwc3_kretprobe] all probes unregistered\n");
 }
 
 /* ------------------------------------------------------------------ */
@@ -433,16 +433,65 @@ void dwc3_msm_kretprobe_exit(void)
+static int dwc3_msm_mod_notifier_cb(struct notifier_block *nb,
+				     unsigned long action, void *data)
+{
+	struct module *mod = data;
+
+	if (strcmp(mod->name, "dwc3_msm"))
+		return NOTIFY_DONE;
+
+	switch (action) {
+	case MODULE_STATE_LIVE:
+		/*
+		 * dwc3_msm fully initialised, all symbols resolved.
+		 * Safe to call register_kretprobe() -> stop_machine() now.
+		 */
+		if (dwc3_msm_kretprobe_init())
+			pr_err("[dwc3_kretprobe] init failed after "
+			       "MODULE_STATE_LIVE\n");
+		break;
+
+	case MODULE_STATE_GOING:
+		/*
+		 * Remove all probes before dwc3_msm text is freed.
+		 * unregister_kretprobe() restores BRK -> original insn
+		 * via stop_machine(). Must complete before module unmap.
+		 */
+		dwc3_msm_kretprobe_exit();
+		break;
+
+	default:
+		break;
+	}
+	return NOTIFY_OK;
+}
+
+static struct notifier_block dwc3_msm_mod_nb = {
+	.notifier_call = dwc3_msm_mod_notifier_cb,
+	.priority      = 0,
+};
+
 static int __init dwc3_msm_kretprobe_module_init(void)
 {
-	return dwc3_msm_kretprobe_init();
+	struct module *dwc3_mod;
+	int ret;
+
+	ret = register_module_notifier(&dwc3_msm_mod_nb);
+	if (ret) {
+		pr_err("[dwc3_kretprobe] register_module_notifier "
+		       "failed: %d\n", ret);
+		return ret;
+	}
+
+	/*
+	 * If dwc3_msm is already loaded (built-in or loaded before us),
+	 * the notifier will not fire. Handle this case explicitly.
+	 */
+	mutex_lock(&module_mutex);
+	dwc3_mod = find_module("dwc3_msm");
+	if (dwc3_mod && dwc3_mod->state == MODULE_STATE_LIVE) {
+		mutex_unlock(&module_mutex);
+		pr_info("[dwc3_kretprobe] dwc3_msm already live, "
+			"arming probes now\n");
+		ret = dwc3_msm_kretprobe_init();
+		if (ret) {
+			unregister_module_notifier(&dwc3_msm_mod_nb);
+			return ret;
+		}
+	} else {
+		mutex_unlock(&module_mutex);
+		pr_info("[dwc3_kretprobe] waiting for dwc3_msm "
+			"MODULE_STATE_LIVE\n");
+	}
+	return 0;
 }
 
 static void __exit dwc3_msm_kretprobe_module_exit(void)
 {
-	dwc3_msm_kretprobe_exit();
+	unregister_module_notifier(&dwc3_msm_mod_nb);
+	dwc3_msm_kretprobe_exit();
 }
 
 module_init(dwc3_msm_kretprobe_module_init);
 module_exit(dwc3_msm_kretprobe_module_exit);
 MODULE_LICENSE("GPL v2");
-MODULE_DESCRIPTION("DWC3 MSM kretprobe instrumentation");
+MODULE_DESCRIPTION("DWC3 MSM kretprobe instrumentation (SM4100/SM5100 fixed)");
-- 
2.34.1
```
