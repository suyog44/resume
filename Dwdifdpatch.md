```diff
From 0000000000000000000000000000000000000002 Mon Sep 17 00:00:00 2001
From: Shree <shree@qcom-dev.local>
Date: Wed, 18 Mar 2026 13:45:00 +0530
Subject: [PATCH 2/2] usb: dwc3: msm: kretprobe: fix tracepoint event names
 and missing trace header for Qualcomm vendor tree

Build errors (10 errors, dwc3-msm-ops.c):

  error: call to undeclared function 'register_trace_dwc3_log_request'
    did you mean 'register_trace_dwc3_alloc_request'?
    trace.h:150: DEFINE_EVENT(dwc3_log_request, dwc3_alloc_request, ...

  error: call to undeclared function 'register_trace_dwc3_log_gadget_ep_cmd'
    did you mean 'register_trace_dwc3_gadget_ep_cmd'?
    trace.h:228: DEFINE_EVENT(dwc3_log_gadget_ep_cmd, dwc3_gadget_ep_cmd, ...

  error: call to undeclared function 'register_trace_dwc3_log_trb'
  error: call to undeclared function 'register_trace_dwc3_log_event'
  error: call to undeclared function 'register_trace_dwc3_log_ep'
  (+ 5 matching unregister_trace_* errors)

Root cause: register_trace_*() / unregister_trace_*() macros are
generated from the DEFINE_EVENT *instance* name, not the class name.
The Qualcomm vendor trace.h uses instance names that differ from
upstream dwc3 trace.h.

Confirmed renames from compiler "did you mean" hints:
  dwc3_log_request       -> dwc3_alloc_request  (trace.h:150)
  dwc3_log_gadget_ep_cmd -> dwc3_gadget_ep_cmd  (trace.h:228)

Remaining 3 names (dwc3_log_trb, dwc3_log_event, dwc3_log_ep) have
no "did you mean" in build log. Marked TODO — run:
  grep "DEFINE_EVENT" drivers/usb/dwc3/trace.h
to confirm their correct instance names.

Also adds #include <trace/events/dwc3.h> which is required for all
register_trace_* / unregister_trace_* macro expansions.
CREATE_TRACE_POINTS is NOT added here — it must exist in exactly one
.c file in the dwc3 build (core.c or trace.c already owns it).

Also removes module_mutex / find_module() which are not exported
in GKI 6.12 kernels. Notifier-only approach is the correct solution.

Signed-off-by: Shree <shree@qcom-dev.local>
---
 drivers/usb/dwc3/dwc3_msm_kretprobe.c | 49 +++++++++++++++-----------
 1 file changed, 29 insertions(+), 20 deletions(-)

diff --git a/drivers/usb/dwc3/dwc3_msm_kretprobe.c b/drivers/usb/dwc3/dwc3_msm_kretprobe.c
--- a/drivers/usb/dwc3/dwc3_msm_kretprobe.c
+++ b/drivers/usb/dwc3/dwc3_msm_kretprobe.c
@@ -13,6 +13,13 @@
 #include <linux/workqueue.h>
 #include <linux/rcupdate.h>
+/*
+ * Do NOT add CREATE_TRACE_POINTS here. It must be defined in exactly
+ * one .c file in the dwc3 build (core.c or trace.c already owns it).
+ * This include pulls in only the register_trace_* / unregister_trace_*
+ * macro declarations generated from DEFINE_EVENT instance names.
+ */
+#include <trace/events/dwc3.h>
 #include <linux/usb/android_configfs_uevent.h>
 #include <linux/usb/dwc3-msm.h>
 #include <linux/usb/composite.h>
@@ -375,12 +382,14 @@ static int dwc3_tp_register_all(void)
 #define REG_TP(name, cb) \
 	ret = register_trace_##name(cb, NULL); \
 	if (ret) { \
 		pr_err("[dwc3_kretprobe] register_trace_" #name \
 		       " failed: %d\n", ret); \
 		return ret; \
 	}
-	REG_TP(dwc3_log_request,       dwc3_tp_log_request_cb)
-	REG_TP(dwc3_log_gadget_ep_cmd, dwc3_tp_log_gadget_ep_cmd_cb)
-	REG_TP(dwc3_log_trb,           dwc3_tp_log_trb_cb)
-	REG_TP(dwc3_log_event,         dwc3_tp_log_event_cb)
-	REG_TP(dwc3_log_ep,            dwc3_tp_log_ep_cb)
+	/* Confirmed: trace.h:150 DEFINE_EVENT(dwc3_log_request, dwc3_alloc_request */
+	REG_TP(dwc3_alloc_request,     dwc3_tp_log_request_cb)
+	/* Confirmed: trace.h:228 DEFINE_EVENT(dwc3_log_gadget_ep_cmd, dwc3_gadget_ep_cmd */
+	REG_TP(dwc3_gadget_ep_cmd,     dwc3_tp_log_gadget_ep_cmd_cb)
+	/* TODO: verify — grep "DEFINE_EVENT.*dwc3_log_trb"   drivers/usb/dwc3/trace.h */
+	REG_TP(dwc3_log_trb,           dwc3_tp_log_trb_cb)
+	/* TODO: verify — grep "DEFINE_EVENT.*dwc3_log_event" drivers/usb/dwc3/trace.h */
+	REG_TP(dwc3_log_event,         dwc3_tp_log_event_cb)
+	/* TODO: verify — grep "DEFINE_EVENT.*dwc3_log_ep"    drivers/usb/dwc3/trace.h */
+	REG_TP(dwc3_log_ep,            dwc3_tp_log_ep_cb)
 #undef REG_TP
 	return 0;
 }
@@ -388,11 +397,16 @@ static int dwc3_tp_register_all(void)
 static void dwc3_tp_unregister_all(void)
 {
-	unregister_trace_dwc3_log_request(dwc3_tp_log_request_cb, NULL);
-	unregister_trace_dwc3_log_gadget_ep_cmd(
-			dwc3_tp_log_gadget_ep_cmd_cb, NULL);
-	unregister_trace_dwc3_log_trb(dwc3_tp_log_trb_cb, NULL);
-	unregister_trace_dwc3_log_event(dwc3_tp_log_event_cb, NULL);
-	unregister_trace_dwc3_log_ep(dwc3_tp_log_ep_cb, NULL);
+	/* Must mirror dwc3_tp_register_all() names exactly */
+	unregister_trace_dwc3_alloc_request(
+			dwc3_tp_log_request_cb, NULL);
+	unregister_trace_dwc3_gadget_ep_cmd(
+			dwc3_tp_log_gadget_ep_cmd_cb, NULL);
+	/* TODO: update if DEFINE_EVENT instance name differs */
+	unregister_trace_dwc3_log_trb(
+			dwc3_tp_log_trb_cb, NULL);
+	unregister_trace_dwc3_log_event(
+			dwc3_tp_log_event_cb, NULL);
+	unregister_trace_dwc3_log_ep(
+			dwc3_tp_log_ep_cb, NULL);
 	tracepoint_synchronize_unregister();
 }
 
@@ -454,7 +468,7 @@ static int __init dwc3_msm_kretprobe_module_init(void)
 {
-	struct module *dwc3_mod;
 	int ret;
 
 	ret = register_module_notifier(&dwc3_msm_mod_nb);
 	if (ret) {
 		pr_err("[dwc3_kretprobe] register_module_notifier "
 		       "failed: %d\n", ret);
 		return ret;
 	}
 
-	/*
-	 * Handle the case where dwc3_msm was loaded before this module
-	 * (built-in, or explicit modprobe ordering). The notifier will
-	 * not fire retroactively, so check state explicitly.
-	 */
-	mutex_lock(&module_mutex);
-	dwc3_mod = find_module("dwc3_msm");
-	if (dwc3_mod && dwc3_mod->state == MODULE_STATE_LIVE) {
-		mutex_unlock(&module_mutex);
-		pr_info("[dwc3_kretprobe] dwc3_msm already live, "
-			"arming probes now\n");
-		ret = dwc3_msm_kretprobe_init();
-		if (ret) {
-			unregister_module_notifier(&dwc3_msm_mod_nb);
-			return ret;
-		}
-	} else {
-		mutex_unlock(&module_mutex);
-		pr_info("[dwc3_kretprobe] waiting for dwc3_msm "
-			"MODULE_STATE_LIVE\n");
-	}
+	/*
+	 * module_mutex / find_module() not exported in GKI 6.12.
+	 * Attempt init directly; non-fatal if dwc3_msm not yet live.
+	 * MODULE_STATE_LIVE notifier will arm probes when ready.
+	 */
+	ret = dwc3_msm_kretprobe_init();
+	if (ret)
+		pr_info("[dwc3_kretprobe] dwc3_msm not live yet, "
+			"will arm on MODULE_STATE_LIVE\n");
 
 	return 0;
 }
-- 
2.34.1
```

---

Once the remaining 3 names need verifying, run this on device or in the source tree and reply with the output:

```bash
grep "DEFINE_EVENT" drivers/usb/dwc3/trace.h | \
    grep -E "log_trb|log_event|log_ep"
```

The second argument on each line is the correct name — I'll give you an immediate follow-up diff with those replaced.
