
Before Patching - The Original Code

Source Code (what the developer wrote):

```c
// kernel_function.c
void example_kernel_function(void)
{
    pr_info("Hello from kernel\n");
    do_some_work();
    return;
}
```

Compiled Assembly (what the compiler generated):

```asm
example_kernel_function:
    // Function prologue
    stp x29, x30, [sp, #-16]!  // Save frame pointer and return address
    PACIASP                     // ARMv8.3 PAC instruction - sign return address  
    mov x29, sp                 // Set up frame pointer
    
    // Function body
    adrp x0, .LC0              // Load string address
    add x0, x0, :lo12:.LC0
    bl pr_info                  // Call printk
    
    bl do_some_work            // Do actual work
    
    // Function epilogue  
    AUTIASP                    // ARMv8.3 PAC instruction - verify return address
    ldp x29, x30, [sp], #16    // Restore frame pointer and return address
    ret                        // Return to caller

.LC0:
    .string "Hello from kernel"
```

DWARF Debug Information (what the compiler also generated):

```c
// In .eh_frame section
FDE for example_kernel_function:
  initial_loc: 0xffffffc010123456  // Address of function
  range: 0x40                      // Function is 64 bytes total
  opcodes:
    DW_CFA_advance_loc1: 4         // Move 4 bytes into function (past stp)
    DW_CFA_negate_ra_state         // ← Marks location after PACIASP
    DW_CFA_advance_loc1: 44        // Move 44 bytes forward  
    DW_CFA_negate_ra_state         // ← Marks location after AUTIASP
    // ... other CFI opcodes ...
```

During Boot - What patch-scs.c Does

Step 1: Memory Preparation

```c
// Kernel temporarily makes code writable
// Normally: _stext to _etext is READ-ONLY/EXECUTE (0x755)
// Now: Changed to READ-WRITE/EXECUTE (0x755 → 0x755 but writable)
```

Step 2: DWARF Parsing

```c
// patch-scs.c processes the FDE frame:
scs_handle_fde_frame(frame, code_alignment_factor=1, use_sdata8=false, dry_run=false)
```

Location tracking:

· Start: loc = 0xffffffc010123456 (function beginning)
· Advance 4 bytes: loc = 0xffffffc01012345A (after PACIASP)
· Find DW_CFA_negate_ra_state → patch at loc - 4 = 0xffffffc010123456

Step 3: Actual Patching

```c
// At address 0xffffffc010123456:
scs_patch_loc(0xffffffc010123456)
```

What happens:

1. Read current instruction: 0xd503233f (PACIASP)
2. Replace with: 0xf800865e (SCS_PUSH)
3. Flush CPU cache so change takes effect

Repeat for AUTIASP:

· Advance 44 bytes: loc = 0xffffffc010123486
· Find DW_CFA_negate_ra_state → patch at 0xffffffc010123482
· Replace 0xd50323bf (AUTIASP) with 0xf85f8e5e (SCS_POP)

After Patching - The Modified Code

Final Assembly in Memory:

```asm
example_kernel_function:
    // Function prologue
    stp x29, x30, [sp, #-16]!  // Save frame pointer and return address
    SCS_PUSH                   // ← WAS: PACIASP, NOW: Shadow Call Stack push
    mov x29, sp                 // Set up frame pointer
    
    // Function body
    adrp x0, .LC0              // Load string address
    add x0, x0, :lo12:.LC0
    bl pr_info                  // Call printk
    
    bl do_some_work            // Do actual work
    
    // Function epilogue  
    SCS_POP                    // ← WAS: AUTIASP, NOW: Shadow Call Stack pop
    ldp x29, x30, [sp], #16    // Restore frame pointer and return address
    ret                        // Return to caller

.LC0:
    .string "Hello from kernel"
```

What SCS Instructions Actually Do

SCS_PUSH Operation:

```asm
// SCS_PUSH = 0xf800865e
// Equivalent to:
str x30, [x18], #8            // Store return address to shadow stack
                              // x18 is the shadow stack pointer
```

SCS_POP Operation:

```asm
// SCS_POP = 0xf85f8e5e  
// Equivalent to:
ldr x30, [x18, #-8]!          // Load return address from shadow stack
```

The Problem on Your ARMv8.2 System

What Should Happen vs What Actually Happens:

Expected DWARF Data:

```
Function: 0xffffffc010123456
Opcodes: advance 4 → negate_ra_state → advance 44 → negate_ra_state
```

What Your System Might Have:

```
Function: 0xffffffc010123456  
Opcodes: advance 0 → negate_ra_state → ...  // WRONG: advance by 0!
```

Result:

```c
case DW_CFA_negate_ra_state:
    // loc = 0xffffffc010123456 (didn't advance)
    scs_patch_loc(loc - 4);  // = 0xffffffc010123452 ← INVALID!
    
// In scs_patch_loc:
u32 insn = le32_to_cpup((void *)0xffffffc010123452);  // CRASH!
```

Or Even Worse:

```
Function: 0xffffffc010123456
Opcodes: advance_loc1: 255 → negate_ra_state  // Advance too far!
```

Result:

```c
loc += 255 * 1;  // loc = 0xffffffc010123555 (way past function end)
scs_patch_loc(0xffffffc010123551);  // Invalid memory → CRASH
```

Another Common Failure Scenario

Bad Initial Location:

```c
// In scs_handle_fde_frame:
u64 loc = (u64)offset_to_ptr(&frame->initial_loc);

// If the FDE has initial_loc = 0 (corrupted DWARF):
loc = 0;  // NULL pointer!

// Then any opcode processing:
case DW_CFA_advance_loc1:
    loc += *opcode++ * 1;  // Still 0!
    
case DW_CFA_negate_ra_state:
    scs_patch_loc(0 - 4);  // = 0xFFFFFFFFFFFFFFFC → CRASH!
```

Why This Only Affects ARMv8.2

On ARMv8.3+ (with PAC):

· Hardware understands PACIASP/AUTIASP instructions
· The instructions work as intended
· No patching needed → enable_scs = false

On ARMv8.2 (no PAC):

· CPU sees PACIASP/AUTIASP as undefined instructions
· Would cause illegal instruction exception if executed
· Need to patch them → enable_scs = true
· But if patching goes wrong → kernel panic

The Security Benefit Explained

Before Patching (PAC):

```c
// Hardware-based protection
PACIASP:  Signs return address with cryptographic key
AUTIASP:  Verifies return address wasn't tampered with
// If hacked: Authentication fails → crash (safe)
```

After Patching (SCS):

```c  
// Software-based protection
SCS_PUSH: Stores return address in protected shadow stack
SCS_POP:  Retrieves return address from shadow stack
// If hacked: Can't easily tamper with shadow stack → protected
```

Summary

patch-scs.c is essentially performing binary surgery:

1. Finds all locations where PAC instructions exist using compiler-generated "maps" (DWARF)
2. Cuts out the hardware-specific PAC instructions
3. Transplants software-based SCS instructions
4. Leaves everything else exactly the same

Your kernel panic occurs because:

· The "map" (DWARF data) is either wrong or being misread
· The surgery is happening at the wrong memory locations
· This corrupts valid kernel code or accesses invalid memory