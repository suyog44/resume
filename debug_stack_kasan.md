
Why This Should NEVER Happen

do_ctors() is called exactly once during kernel boot from:

```c
kernel_init() → kernel_init_freeable() → do_basic_setup() → do_ctors()
```

This is a linear initialization path with no loops or recursion.

Possible Causes for Multiple Calls

1. Stack Corruption Leading to Re-entry (Most Likely)

```c
// One of the constructors corrupts the return address on stack
void corrupting_constructor(void) {
    char buffer[8];
    // Buffer overflow overwrites return address
    strcpy(buffer, very_long_string); // Corrupts stack frame
}
```

Effect: When do_ctors() returns, it jumps back to do_ctors() instead of do_basic_setup().

2. Function Pointer Table Corruption

```c
// The function pointer for do_ctors gets duplicated/corrupted
static initcall_t *initcall_table[] = {
    do_ctors,    // Original
    // ... later corrupted to point to do_ctors again
    do_ctors,    // Duplicate entry
};
```

3. Compiler/Linker Bug

Rare but possible:

· Linker script issue creating duplicate sections
· Compiler optimization creating multiple call sites
· Function inlining gone wrong

4. Memory Mapping Issue

```c
// MMU/page table corruption causing code replication
// Original do_ctors at 0xFFFFFFC008000000
// Corrupted mapping creates copy at 0xFFFFFFC008100000
// Both get called due to corrupted function pointers
```

5. Interrupt Handler Corruption

```c
// Interrupt occurs during do_ctors execution
void interrupt_handler() {
    // Corrupts kernel data structures
    // Modifies the initcall execution table
}
```

Debugging the Multiple Calls

Add this diagnostic patch to trace the callers:

```c
static void __init do_ctors(void)
{
    static int call_count = 0;
    unsigned long return_address;
    unsigned long frame_pointer;
    
    call_count++;
    
    /* Get call stack information */
    return_address = (unsigned long)__builtin_return_address(0);
    frame_pointer = (unsigned long)__builtin_frame_address(0);
    
    pr_emerg("=== DO_CTORS CALL #%d ===\n", call_count);
    pr_emerg("Return address: %pS\n", (void *)return_address);
    pr_emerg("Frame pointer: %px\n", (void *)frame_pointer);
    pr_emerg("Stack dump:\n");
    dump_stack();
    
    if (call_count > 1) {
        pr_emerg("*** CRITICAL: do_ctors called %d times! ***\n", call_count);
        pr_emerg("*** This should NEVER happen - serious kernel bug ***\n");
        
        /* Try to identify the corruption pattern */
        analyze_corruption_pattern();
        return; // Abort after first successful execution
    }
    
    // ... rest of normal do_ctors code ...
}
```

Enhanced Analysis Function

```c
static void __init analyze_corruption_pattern(void)
{
    pr_emerg("=== CORRUPTION ANALYSIS ===\n");
    
    /* Check if return address points back to do_basic_setup */
    unsigned long ret_addr = (unsigned long)__builtin_return_address(0);
    extern char __do_basic_setup_start[], __do_basic_setup_end[];
    
    if (ret_addr >= (unsigned long)__do_basic_setup_start && 
        ret_addr <= (unsigned long)__do_basic_setup_end) {
        pr_emerg("Return address within do_basic_setup - normal case\n");
    } else {
        pr_emerg("*** ABNORMAL: Return address: %pS ***\n", (void *)ret_addr);
    }
    
    /* Check stack frame integrity */
    analyze_stack_frame();
    
    /* Check if we're in an interrupt context */
    if (in_interrupt()) {
        pr_emerg("*** IN INTERRUPT CONTEXT - should not happen! ***\n");
    }
}
```

Most Likely Scenario Based on Your Logs

Looking at your logs again:

```
First execution: Total: 19968, Executed: 2496, Skipped: 0
Second execution: Total: -272658896480, Executed: 66, Skipped: -64  
```

The negative numbers suggest integer overflow from memory corruption. This points to:

Stack Buffer Overflow in a Constructor

One of the 2496 constructors is corrupting the stack, specifically:

1. Constructor 2495 (the last successful one) has a buffer overflow
2. It overwrites the return address on the stack
3. The corrupted return address points to do_ctors() entry point
4. When do_ctors() returns, it jumps back to do_ctors() instead of returning to do_basic_setup()

Immediate Investigation Steps

1. Identify the Culprit Constructor

```c
// Add this before executing each constructor
if (i == 2494) { // Before the suspected constructor
    pr_emerg("=== PRE-CONSTRUCTOR 2495 STACK ANALYSIS ===\n");
    dump_stack();
}
```

2. Check Stack Pointer Integrity

```c
// At start and end of do_ctors
pr_emerg("Initial SP: %px\n", __builtin_frame_address(0));
// ... after constructors ...
pr_emerg("Final SP: %px\n", __builtin_frame_address(0));
// They should be identical!
```

3. Validate Return Address

```c
unsigned long expected_return = (unsigned long)&&expected_return_label;
// ... do work ...
expected_return_label:
// Check if we returned to the expected location
```

The Shocking Reality

This is indeed a serious kernel bug. The fact that do_ctors() is called multiple times means:

· Memory safety is completely broken
· Stack integrity is compromised
· Control flow is hijacked
· The kernel cannot be trusted until this is fixed

You should:

1. Immediately add the call guard to prevent multiple executions
2. Identify the corrupting constructor (likely #2495)
3. Check that constructor for buffer overflows
4. Consider disabling that constructor if it's non-essential

This is not a false KASAN positive - it's a genuine memory corruption that's hijacking kernel control flow!