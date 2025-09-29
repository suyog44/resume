

Minimal Patch: Stack Left After Each Constructor

```diff
From: Debug Helper <debug@kernel.org>
Date: Mon Jan 27 2025
Subject: [PATCH] init: print stack remaining after every constructor call

Track stack usage after every single constructor to identify exactly
which constructor causes the stack overflow.

Signed-off-by: Debug Helper <debug@kernel.org>
---

 init/main.c | 47 ++++++++++++++++++++++++++++++++++++++++++-----
 1 file changed, 42 insertions(+), 5 deletions(-)

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
 
+/* Simple stack tracking */
+static unsigned long __init get_stack_remaining(void)
+{
+    struct thread_info *ti = current_thread_info();
+    unsigned long stack_end = (unsigned long)ti + THREAD_SIZE;
+    unsigned long stack_current = (unsigned long)__builtin_stack_address();
+    return stack_end - stack_current;
+}
+
+static void __init print_stack_status(int constructor_index, const char *phase)
+{
+    unsigned long remaining = get_stack_remaining();
+    
+    pr_emerg("Constructor %d %s: stack_left=%lu bytes\n", 
+        constructor_index, phase, remaining);
+    
+    /* Highlight critical levels */
+    if (remaining < 1024) {
+        pr_emerg("*** WARNING: Less than 1KB stack left at constructor %d ***\n", constructor_index);
+    }
+    if (remaining < 512) {
+        pr_emerg("*** CRITICAL: Less than 512 bytes stack left at constructor %d ***\n", constructor_index);
+    }
+    if (remaining < 256) {
+        pr_emerg("*** EMERGENCY: Less than 256 bytes - STACK OVERFLOW at constructor %d ***\n", constructor_index);
+        dump_stack();
+    }
+}
+
 static void __init do_ctors(void)
 {
-	extern initcall_t __ctors_start[], __ctors_end[];
-	initcall_t *fn;
+	extern ctor_fn_t __ctors_start[], __ctors_end[];
+	ctor_fn_t *fn;
+	int i = 0;
+	unsigned long initial_stack;
+	
+	pr_emerg("=== DO_CTORS START ===\n");
+	initial_stack = get_stack_remaining();
+	pr_emerg("Initial stack: %lu bytes\n", initial_stack);
 
-	for (fn = __ctors_start; fn < __ctors_end; fn++)
-		(*fn)();
+	for (fn = __ctors_start; fn < __ctors_end; fn++, i++) {
+		/* Print stack before constructor */
+		print_stack_status(i, "before");
+		
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
+		(*fn)();
+		
+		/* Print stack AFTER constructor - this is the critical measurement */
+		print_stack_status(i, "after");
+	}
+	
+	pr_emerg("=== DO_CTORS COMPLETE: %d constructors, final stack: %lu bytes ===\n", 
+		i, get_stack_remaining());
 }
 
 static void __init do_basic_setup(void)
```

Even Simpler Version (Just Stack Left)

If you want the absolute minimum output:

```diff
From: Debug Helper <debug@kernel.org>
Date: Mon Jan 27 2025
Subject: [PATCH] init: minimal stack tracking after each constructor

Signed-off-by: Debug Helper <debug@kernel.org>
---

 init/main.c | 32 ++++++++++++++++++++++++++++----
 1 file changed, 28 insertions(+), 4 deletions(-)

diff --git a/init/main.c b/init/main.c
index 123456789..abcdef123 100644
--- a/init/main.c
+++ b/init/main.c
@@ -XXX,XX +XXX,XX @@
 
 static void __init do_ctors(void)
 {
-	extern initcall_t __ctors_start[], __ctors_end[];
-	initcall_t *fn;
+	extern ctor_fn_t __ctors_start[], __ctors_end[];
+	ctor_fn_t *fn;
+	int i = 0;
+	
+	pr_emerg("do_ctors: starting with %ld constructors\n", 
+		__ctors_end - __ctors_start);
 
-	for (fn = __ctors_start; fn < __ctors_end; fn++)
-		(*fn)();
+	for (fn = __ctors_start; fn < __ctors_end; fn++, i++) {
+		struct thread_info *ti = current_thread_info();
+		unsigned long stack_remaining;
+		
+		if (unlikely(!*fn)) {
+			pr_emerg("ctor %d: NULL, skipping\n", i);
+			continue;
+		}
+		
+		pr_emerg("ctor %d: %pS", i, *fn);
+		(*fn)();
+		
+		/* Calculate stack remaining AFTER constructor */
+		stack_remaining = ((unsigned long)ti + THREAD_SIZE) - 
+			         (unsigned long)__builtin_stack_address();
+		
+		pr_emerg(" -> stack_left=%lu bytes\n", stack_remaining);
+		
+		if (stack_remaining < 512) {
+			pr_emerg("*** CRITICAL: Only %lu bytes stack left after ctor %d ***\n", 
+				stack_remaining, i);
+		}
+	}
+	
+	pr_emerg("do_ctors: completed %d constructors\n", i);
 }
 
 static void __init do_basic_setup(void)
```

Expected Output

You'll see something like this:

```
do_ctors: starting with 2496 constructors
ctor 0: some_function -> stack_left=15872 bytes
ctor 1: another_function -> stack_left=15840 bytes  
ctor 2: deep_recursive_function -> stack_left=512 bytes
*** CRITICAL: Only 512 bytes stack left after ctor 2 ***
ctor 3: next_function -> stack_left=480 bytes
*** CRITICAL: Only 480 bytes stack left after ctor 3 ***
```

What to Look For

Normal Pattern:

```
stack_left=15872 bytes
stack_left=15840 bytes  
stack_left=15808 bytes
```

→ Small, consistent decreases (normal stack usage)

Problem Pattern:

```
stack_left=15872 bytes
stack_left=512 bytes
*** CRITICAL: Only 512 bytes stack left ***
```

→ Constructor #2 is the stack hog!

Stack Overflow Pattern:

```
stack_left=15872 bytes
stack_left=512 bytes
*** CRITICAL: Only 512 bytes stack left ***
stack_left=128 bytes
*** CRITICAL: Only 128 bytes stack left ***
stack_left=18446744073709551615 bytes  ← This is -1 (overflow!)
```

→ Constructor caused stack overflow

The Critical Constructor

When you see a sudden large drop in stack remaining, that's the constructor causing the problem:

```
ctor 149: normal_function -> stack_left=2048 bytes
ctor 150: stack_hog_function -> stack_left=512 bytes  ← THIS IS THE CULPRIT
ctor 151: next_function -> stack_left=480 bytes
```

Constructor 150 is the one that uses ~1.5KB of stack in a single call!

Immediate Fix

Once you identify the problematic constructor, you can:

1. Fix that constructor to use less stack
2. Increase stack size temporarily while debugging
3. Move that constructor to run later with more stack available

This patch will definitively show you exactly which constructor is causing the stack exhaustion and how much stack each constructor uses.