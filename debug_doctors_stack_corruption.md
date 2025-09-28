Minimal Stack Corruption Check Patch

```diff
From: Debug Helper <debug@kernel.org>
Date: Mon Jan 27 2025
Subject: [PATCH] init: minimal stack corruption detection for do_ctors

Add basic stack canary to detect if stack overflow is causing
the early boot crash in do_ctors.

Signed-off-by: Debug Helper <debug@kernel.org>
---

 init/main.c | 63 ++++++++++++++++++++++++++++++++++++++++++++++++++++
 1 file changed, 63 insertions(+)

diff --git a/init/main.c b/init/main.c
index 123456789..abcdef123 100644
--- a/init/main.c
+++ b/init/main.c
@@ -XXX,XX +XXX,XX @@
 #include <linux/rodata_test.h>
 #include <linux/jump_label.h>
 #include <linux/mem_encrypt.h>
+#include <linux/sched.h>
 
 #include <asm/io.h>
 #include <asm/sections.h>
@@ -XXX,XX +XXX,XX @@
 
 #include "do_mounts.h"
 
+/* Stack canary to detect overflow */
+#define STACK_CANARY 0xDEADBEEFCAFEBABEUL
+
 static void __init do_ctors(void)
 {
-	extern initcall_t __ctors_start[], __ctors_end[];
-	initcall_t *fn;
+	extern const initcall_t __ctors_start[], __ctors_end[];
+	const initcall_t *fn;
+	int i = 0;
+	
+	/* Stack canary - placed on stack to detect overflow */
+	volatile unsigned long stack_canary = STACK_CANARY;
+	
+	/* Stack usage tracking */
+	unsigned long stack_start, stack_end, stack_current;
+	struct thread_info *ti = current_thread_info();
+	
+	stack_start = (unsigned long)ti;
+	stack_end = stack_start + THREAD_SIZE;
+	stack_current = (unsigned long)__builtin_stack_address();
+	
+	pr_emerg("=== DO_CTORS STACK CHECK ===\n");
+	pr_emerg("Stack: %px-%px (%lu bytes), Current: %px\n",
+		(void *)stack_start, (void *)stack_end, THREAD_SIZE,
+		(void *)stack_current);
+	pr_emerg("Stack used: %lu bytes (%.1f%%)\n",
+		stack_end - stack_current,
+		((stack_end - stack_current) * 100.0) / THREAD_SIZE);
+	pr_emerg("Stack canary: 0x%016lx\n", stack_canary);
+	
+	/* Check if stack is already near overflow */
+	if (stack_current < stack_start + 1024) {
+		pr_emerg("*** CRITICAL: Less than 1KB stack remaining! ***\n");
+	}
 
-	for (fn = __ctors_start; fn < __ctors_end; fn++)
+	for (fn = __ctors_start; fn < __ctors_end; fn++, i++) {
+		/* Check stack canary every 100 constructors */
+		if (i % 100 == 0) {
+			if (stack_canary != STACK_CANARY) {
+				pr_emerg("*** STACK CORRUPTION DETECTED at constructor %d ***\n", i);
+				pr_emerg("Canary was: 0x%016lx, expected: 0x%016lx\n",
+					stack_canary, STACK_CANARY);
+				dump_stack();
+				break;
+			}
+			
+			/* Update current stack pointer */
+			stack_current = (unsigned long)__builtin_stack_address();
+			pr_emerg("Constructor %d: Stack used: %lu bytes\n",
+				i, stack_end - stack_current);
+			
+			/* Check if we're running out of stack */
+			if (stack_current < stack_start + 2048) {
+				pr_emerg("*** WARNING: Less than 2KB stack remaining ***\n");
+			}
+		}
+		
+		/* Skip NULL/invalid pointers */
+		if (unlikely(!*fn)) {
+			pr_emerg("NULL constructor at index %d\n", i);
+			continue;
+		}
+		
+		if (unlikely((unsigned long)*fn < (unsigned long)_stext || 
+			     (unsigned long)*fn >= (unsigned long)_end)) {
+			pr_emerg("Invalid constructor %p at index %d\n", *fn, i);
+			continue;
+		}
+		
+		pr_emerg("Executing constructor [%d]: %pS\n", i, *fn);
 		(*fn)();
+	}
+	
+	/* Final stack integrity check */
+	pr_emerg("=== DO_CTORS COMPLETE ===\n");
+	pr_emerg("Total constructors: %d\n", i);
+	pr_emerg("Final stack usage: %lu bytes\n", stack_end - stack_current);
+	
+	if (stack_canary != STACK_CANARY) {
+		pr_emerg("*** FINAL STACK CORRUPTION DETECTED ***\n");
+		pr_emerg("Canary: 0x%016lx (expected: 0x%016lx)\n",
+			stack_canary, STACK_CANARY);
+		dump_stack();
+	} else {
+		pr_emerg("Stack integrity: OK\n");
+	}
 }
 
 static void __init do_basic_setup(void)
```

Even Simpler Version (Just Stack Canary)

If you want the absolute minimum:

```diff
From: Debug Helper <debug@kernel.org>
Date: Mon Jan 27 2025
Subject: [PATCH] init: basic stack canary for do_ctors

Signed-off-by: Debug Helper <debug@kernel.org>
---

 init/main.c | 34 ++++++++++++++++++++++++++++++++--
 1 file changed, 32 insertions(+), 2 deletions(-)

diff --git a/init/main.c b/init/main.c
index 123456789..abcdef123 100644
--- a/init/main.c
+++ b/init/main.c
@@ -XXX,XX +XXX,XX @@
 
 static void __init do_ctors(void)
 {
-	extern initcall_t __ctors_start[], __ctors_end[];
-	initcall_t *fn;
+	extern const initcall_t __ctors_start[], __ctors_end[];
+	const initcall_t *fn;
+	volatile unsigned long canary = 0xDEADBEEFCAFEBABE;
+	int i = 0;
+	
+	pr_emerg("do_ctors: stack canary at %px: 0x%016lx\n", 
+		&canary, canary);
 
-	for (fn = __ctors_start; fn < __ctors_end; fn++)
+	for (fn = __ctors_start; fn < __ctors_end; fn++, i++) {
+		/* Check canary every 50 constructors */
+		if (i % 50 == 0 && canary != 0xDEADBEEFCAFEBABE) {
+			pr_emerg("*** STACK CORRUPTION at constructor %d ***\n", i);
+			dump_stack();
+			break;
+		}
+		
+		if (unlikely(!*fn)) {
+			pr_emerg("NULL constructor at %d\n", i);
+			continue;
+		}
+		
 		(*fn)();
+	}
+	
+	pr_emerg("do_ctors: completed %d constructors\n", i);
+	
+	if (canary != 0xDEADBEEFCAFEBABE) {
+		pr_emerg("*** STACK CORRUPTION DETECTED ***\n");
+		dump_stack();
+	}
 }
```

What This Will Tell You

If Stack Canary Gets Corrupted:

```
*** STACK CORRUPTION DETECTED at constructor 150 ***
Canary was: 0x0000000000000000, expected: 0xDEADBEEFCAFEBABE
```

→ Stack overflow is happening

If Stack Canary Stays Intact:

```
Stack integrity: OK
```

→ Stack is NOT the problem - issue is elsewhere (memory corruption, etc.)

Stack Usage Percentage:

```
Stack used: 12000 bytes (73.2%)
```

→ Shows how much stack is actually being used

Expected Output Examples

Case 1: Stack Overflow

```
=== DO_CTORS STACK CHECK ===
Stack: FFFFFFC084FB8000-FFFFFC084FC0000 (16384 bytes), Current: FFFFFFC084FB7E40
Stack used: 1984 bytes (12.1%)
Constructor 100: Stack used: 1500 bytes
*** STACK CORRUPTION DETECTED at constructor 150 ***
Canary was: 0x0000000000000000, expected: 0xDEADBEEFCAFEBABE
```

Case 2: No Stack Issue

```
=== DO_CTORS STACK CHECK ===  
Stack: FFFFFFC084FB8000-FFFFFC084FC0000 (16384 bytes), Current: FFFFFFC084FB7E40
Stack used: 1984 bytes (12.1%)
Constructor 100: Stack used: 2000 bytes
Constructor 200: Stack used: 2100 bytes
=== DO_CTORS COMPLETE ===
Total constructors: 2496
Final stack usage: 2200 bytes
Stack integrity: OK
```
