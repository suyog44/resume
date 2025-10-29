DWARF Debugging Tutorial for Linux Kernel Developers

What is DWARF in Simple Terms?

Think of DWARF as a "GPS navigation system" for your compiled code. When your program is running, DWARF tells debuggers:

· "You are HERE in the code"
· "To go back to the caller, take THIS path"
· "Your variables are stored in THESE memory locations"

DWARF Components in Your Code

1. .eh_frame Section - The "Call Frame GPS"

This is what your code is parsing. It contains:

· CIE (Common Information Entry) = "Default navigation rules"
· FDE (Frame Description Entry) = "Specific directions for one function"

2. CFI Opcodes - The "Turn-by-Turn Directions"

These are the instructions your code processes:

```
"Drive 100 meters"          = DW_CFA_advance_loc
"Turn left at next signal"  = DW_CFA_negate_ra_state
"Parking rules change here" = DW_CFA_def_cfa
```

How Your SCS Patching Code Uses DWARF

The "Treasure Hunt" Metaphor

Imagine DWARF is giving you clues to find PAC instructions:

1. Start at function beginning (initial_loc)
2. Follow the steps (CFI opcodes)
3. Look for the special marker (DW_CFA_negate_ra_state)
4. The treasure is 4 steps back (loc - 4)

Code Breakdown in Simple Terms

```c
// CIE = "Here are the general rules for this area"
if (frame->cie_id_or_pointer == 0) {
    // Check if the "map legend" makes sense
    if (strcmp(frame->augmentation_string, "zR"))
        return EDYNSCS_INVALID_CIE_HEADER;
}

// FDE = "Here's a specific function's directions"  
else {
    // Walk through the function step by step
    while (size-- > 0) {
        switch (*opcode++) {
            case DW_CFA_advance_loc1:
                // "Move forward X instructions"
                loc += *opcode++ * code_alignment_factor;
                break;
                
            case DW_CFA_negate_ra_state:
                // "FOUND IT! The PAC instruction is right behind us!"
                scs_patch_loc(loc - 4);
                break;
        }
    }
}
```

Common DWARF Problems in Kernel Context

1. Bad Map Data

· Compiler generates wrong DWARF information
· Optimizations corrupt the "directions"
· The GPS is giving bad coordinates

2. Wrong Interpretation

· Your code misunderstands the "directions"
· Math errors in location calculation
· Missing edge cases in opcode parsing

3. Timing Issues

· DWARF data not fully initialized yet
· Memory layout changes after boot
· The "map" doesn't match the actual "terrain"

Practical Debugging Steps

Step 1: See the Raw DWARF Data

```bash
# View the actual DWARF information
objdump -w --eh-frame-frame vmlinux

# Check specific sections  
readelf -S vmlinux | grep eh_frame
```

Step 2: Add Strategic Debug Prints

In scs_patch():

```c
pr_info("SCS: Starting patch, eh_frame=%p, size=%d\n", eh_frame, size);
```

In CIE processing:

```c
pr_info("SCS: CIE - aug_string='%s', code_align=%d, RA_reg=%d\n",
        frame->augmentation_string, 
        frame->code_alignment_factor,
        frame->return_address_register);
```

In FDE processing:

```c
pr_info("SCS: FDE - initial_loc=0x%llx, range=0x%x\n", 
        (u64)loc, frame->range);
```

Before patching:

```c
pr_info("SCS: Patching at 0x%llx, current instruction: 0x%x\n", 
        loc - 4, *(u32 *)(loc - 4));
```

Step 3: Trace the "Walk"

In the opcode loop:

```c
pr_info("SCS: [loc=0x%llx] Opcode: 0x%02x, size_left=%d\n", 
        loc, opcode[-1], size);
```

Step 4: Validate Results

After patching:

```c
pr_info("SCS: Patched 0x%llx: 0x%x -> 0x%x\n", 
        loc - 4, old_instruction, new_instruction);
```

Understanding the Specific Problem

Based on your ARMv8.2 context:

The Theory: DWARF data might be pointing to wrong locations because:

1. Compiler assumptions don't match your CPU
2. Location calculations have off-by-one errors
3. Some functions have unusual CFI patterns

The Evidence:

· Your code assumes DW_CFA_negate_ra_state always follows PAC instructions
· But ARMv8.2 doesn't have PAC, so this assumption might break
· The math loc - 4 might not always be correct

Quick Debugging Template

Add this to the top of your file:

```c
#define pr_fmt(fmt) "SCS_DEBUG: " fmt
#include <linux/printk.h>

// Then add these debug points:

// In scs_patch(), after variable declarations:
pr_info("=== SCS PATCH START ===\n");
pr_info("eh_frame: %p, size: %d\n", eh_frame, size);

// In scs_handle_fde_frame(), early:
pr_info("FDE - loc: 0x%llx, size: %d, dry_run: %d\n", 
        loc, size, dry_run);

// Right before the switch(opcode) loop:
pr_info("Starting opcode walk at %p\n", opcode);

// In DW_CFA_negate_ra_state case:
pr_info(">>> FOUND negate_ra_state at loc=0x%llx\n", loc);
pr_info(">>> Would patch at loc-4=0x%llx\n", loc - 4);
u32 current_insn = le32_to_cpup((void *)(loc - 4));
pr_info(">>> Current instruction at patch location: 0x%x\n", current_insn);
```

This will give you a clear picture of:

· What DWARF data your code is seeing
· Where it thinks PAC instructions should be
· What instructions are actually at those locations

The key insight: DWARF is just a "map" - sometimes the map is wrong, or your interpretation of it is wrong. Your debug prints will show you which is happening.


Based on the patch-scs code you shared, let me explain the specific DWARF symbols and how they're used:

DWARF Symbols in Your patch-scs Code

1. Frame Structure Symbols

```c
struct eh_frame {
    u32 size;                    // Size of this frame
    u32 cie_id_or_pointer;       // 0 for CIE, offset for FDE
    union {
        struct { // CIE (Common Information Entry)
            u8  version;
            u8  augmentation_string[3];  // ← Critical: "zR" 
            u8  code_alignment_factor;   // ← Usually 1 or 4
            u8  data_alignment_factor;
            u8  return_address_register; // ← Must be 30 (x30/LR)
            u8  augmentation_data_size;  // ← Must be 1
            u8  fde_pointer_format;      // ← DW_EH_PE values
        };
        // ... FDE structures
    };
};
```

2. CFI Opcode Symbols (The "Actions")

Location Advancement:

```c
DW_CFA_advance_loc1    // Move forward (1 byte offset)
DW_CFA_advance_loc2    // Move forward (2 byte offset)  
DW_CFA_advance_loc4    // Move forward (4 byte offset)
// Also: 0x40-0x7f range = advance_loc with embedded offset
```

Stack Frame Operations:

```c
DW_CFA_def_cfa              // Define Canonical Frame Address
DW_CFA_def_cfa_register     // Change CFA register
DW_CFA_def_cfa_offset       // Change CFA offset
```

Register Operations:

```c
DW_CFA_offset_extended      // Register saved at offset from CFA
DW_CFA_restore_extended    // Restore saved register
DW_CFA_undefined           // Register is undefined
DW_CFA_same_value          // Register has same value as previous
```

State Management:

```c
DW_CFA_remember_state      // Save current frame state
DW_CFA_restore_state      // Restore saved frame state
```

3. The Critical Symbol for SCS Patching

```c
DW_CFA_negate_ra_state     // ← THIS IS THE KEY ONE!
```

· Purpose: Toggles the "return address signing state"
· Location: Always appears immediately after PAC instructions
· Usage in your code: When found, patch the instruction 4 bytes back

4. Encoding Format Symbols

```c
DW_EH_PE_pcrel             // PC-relative encoding
DW_EH_PE_sdata4            // 4-byte signed data  
DW_EH_PE_sdata8            // 8-byte signed data
```

How These Symbols Work Together

CIE Processing Flow:

```c
// Check augmentation string
if (strcmp(frame->augmentation_string, "zR"))
    return EDYNSCS_INVALID_CIE_HEADER;

// Validate critical fields  
if (frame->return_address_register != 30)  // Must be LR register
    return EDYNSCS_INVALID_CIE_HEADER;

// Determine FDE format
switch (frame->fde_pointer_format) {
case DW_EH_PE_pcrel | DW_EH_PE_sdata4:  // 4-byte PC-relative
case DW_EH_PE_pcrel | DW_EH_PE_sdata8:  // 8-byte PC-relative
    // Valid formats
    break;
default:
    return EDYNSCS_INVALID_CIE_SDATA_SIZE;
}
```

FDE Processing Flow:

```c
// Parse opcodes to "walk" through the function
while (size-- > 0) {
    switch (*opcode++) {
    case DW_CFA_advance_loc1:
        loc += *opcode++ * code_alignment_factor;
        break;
        
    case DW_CFA_advance_loc2:  
        loc += *opcode++ * code_alignment_factor;
        loc += (*opcode++ << 8) * code_alignment_factor;
        break;
        
    case DW_CFA_negate_ra_state:  // ← FOUND PAC LOCATION!
        scs_patch_loc(loc - 4);   // Patch the PAC instruction
        break;
        
    // ... handle other opcodes
    }
}
```

Real-World Example

Imagine this assembly code:

```asm
my_function:
    stp x29, x30, [sp, #-16]!  // Prologue
    paciasp                    // ← This gets PACIASP instruction  
    ... function body ...
    autiasp                    // ← This gets AUTIASP instruction
    ldp x29, x30, [sp], #16    // Epilogue
    ret
```

The DWARF CFI would contain:

```
CIE: [general rules...]
FDE for my_function:
  initial_loc: my_function
  range: size_of_function
  opcodes:
    DW_CFA_advance_loc1 (to paciasp+4)
    DW_CFA_negate_ra_state     ← Marks PACIASP location
    ... more advances ...
    DW_CFA_advance_loc1 (to autiasp+4)  
    DW_CFA_negate_ra_state     ← Marks AUTIASP location
```

Debugging These Symbols

Add these debug prints to understand the flow:

```c
// In CIE processing
pr_info("CIE: aug_string='%.3s', code_align=%u, RA_reg=%u, fde_fmt=0x%x\n",
        frame->augmentation_string, frame->code_alignment_factor,
        frame->return_address_register, frame->fde_pointer_format);

// In FDE processing  
pr_info("FDE: initial_loc=0x%llx, range=0x%x\n", loc, frame->range);

// In opcode loop
pr_info("CFI: loc=0x%llx, opcode=0x%02x\n", loc, opcode[-1]);

// When finding the critical symbol
case DW_CFA_negate_ra_state:
    pr_info(">>> FOUND negate_ra_state! Patching at 0x%llx\n", loc - 4);
    u32 current_insn = le32_to_cpup((void *)(loc - 4));
    pr_info(">>> Instruction at patch location: 0x%x\n", current_insn);
    if (!dry_run)
        scs_patch_loc(loc - 4);
    break;
```

Key Insights for Your Issue

1. augmentation_string must be "zR" - If not, parsing fails
2. return_address_register must be 30 - ARM64 link register
3. DW_CFA_negate_ra_state marks PAC locations - But only if DWARF data is correct
4. The loc - 4 calculation - Assumes PAC instructions are exactly 4 bytes before

Your kernel panic might occur if:

· DWARF data has wrong augmentation_string
· Location calculation (loc - 4) points to wrong memory
· DW_CFA_negate_ra_state appears in unexpected contexts
· The actual instructions at patch locations aren't PAC instructions

The debug prints will show you exactly which of these is happening.

PatchSCS Driver Working with DWARF Symbols

Complete Workflow Overview

Let me trace through exactly how the PatchSCS driver works with DWARF symbols:

1. Initialization Phase

Driver Entry Point (not shown in your code, but implied):

```c
// Called during kernel boot
int __init scs_init(void)
{
    // 1. Locate the .eh_frame section in kernel memory
    // 2. Call scs_patch() with the eh_frame data
    return scs_patch(eh_frame_start, eh_frame_size);
}
```

2. Main Patching Loop - scs_patch()

```c
int scs_patch(const u8 eh_frame[], int size)
{
    const u8 *p = eh_frame;
    
    // Walk through ALL frames in .eh_frame section
    while (size > 4) {
        const struct eh_frame *frame = (const void *)p;
        
        // Check frame header validity
        if (frame->size == 0 || frame->size == U32_MAX || frame->size > size)
            break;
        
        // **CRITICAL BRANCH**: CIE vs FDE
        if (frame->cie_id_or_pointer == 0) {
            // Process CIE frame (setup rules)
            if (strcmp(frame->augmentation_string, "zR"))
                return EDYNSCS_INVALID_CIE_HEADER;
                
            // Extract global parameters
            code_alignment_factor = frame->code_alignment_factor;
            
            // Determine FDE format
            switch (frame->fde_pointer_format) {
            case DW_EH_PE_pcrel | DW_EH_PE_sdata4:
                fde_use_sdata8 = false;
                break;
            case DW_EH_PE_pcrel | DW_EH_PE_sdata8:
                fde_use_sdata8 = true;
                break;
            default:
                return EDYNSCS_INVALID_CIE_SDATA_SIZE;
            }
        } else {
            // Process FDE frame (actual function)
            // TWO PASSES: dry-run then real patching
            ret = scs_handle_fde_frame(frame, code_alignment_factor,
                                       fde_use_sdata8, true); // Dry run
            if (ret) return ret;
            
            scs_handle_fde_frame(frame, code_alignment_factor,
                               fde_use_sdata8, false); // Real patch
        }
        
        // Move to next frame
        p += sizeof(frame->size) + frame->size;
        size -= sizeof(frame->size) + frame->size;
    }
    return 0;
}
```

3. FDE Processing - scs_handle_fde_frame()

```c
static int scs_handle_fde_frame(const struct eh_frame *frame,
                int code_alignment_factor,
                bool use_sdata8,
                bool dry_run)
{
    // Calculate function start address
    u64 loc = (u64)offset_to_ptr(&frame->initial_loc);
    const u8 *opcode = frame->opcodes;
    
    if (use_sdata8) {
        // 64-bit address format
        loc = (u64)&frame->initial_loc64 + frame->initial_loc64;
        opcode = frame->opcodes64;
    }
    
    // Skip augmentation data (single byte uleb128)
    int l = *opcode++;
    opcode += l;
    
    // **MAIN OPCODE PROCESSING LOOP**
    while (size-- > 0) {
        u8 current_opcode = *opcode++;
        
        switch (current_opcode) {
            // --- LOCATION ADVANCEMENT ---
            case DW_CFA_advance_loc1:
                loc += *opcode++ * code_alignment_factor;
                size--;
                break;
                
            case DW_CFA_advance_loc2:
                loc += *opcode++ * code_alignment_factor;
                loc += (*opcode++ << 8) * code_alignment_factor;
                size -= 2;
                break;
                
            case 0x40 ... 0x7f: // Embedded advance_loc
                loc += (current_opcode & 0x3f) * code_alignment_factor;
                break;
            
            // --- THE CRITICAL PATCH POINT ---
            case DW_CFA_negate_ra_state:
                if (!dry_run)
                    scs_patch_loc(loc - 4); // PATCH HERE!
                break;
            
            // --- SKIPPED OPERATIONS ---  
            case DW_CFA_def_cfa:
            case DW_CFA_offset_extended:
                size = skip_xleb128(&opcode, size);
                // fallthrough to skip another operand
                
            case DW_CFA_def_cfa_offset:
            case DW_CFA_def_cfa_register:
                size = skip_xleb128(&opcode, size);
                break;
                
            // --- IGNORED OPERATIONS ---
            case DW_CFA_nop:
            case DW_CFA_remember_state:
            case DW_CFA_restore_state:
                break;
                
            default:
                return EDYNSCS_INVALID_CFA_OPCODE;
        }
    }
    return 0;
}
```

4. Actual Patching - scs_patch_loc()

```c
static void __always_inline scs_patch_loc(u64 loc)
{
    // Read current instruction at location
    u32 insn = le32_to_cpup((void *)loc);
    
    // Replace PAC instructions with SCS instructions
    switch (insn) {
    case PACIASP:   // 0xd503233f
        *(u32 *)loc = cpu_to_le32(SCS_PUSH);  // 0xf800865e
        break;
    case AUTIASP:   // 0xd50323bf  
        *(u32 *)loc = cpu_to_le32(SCS_POP);   // 0xf85f8e5e
        break;
    default:
        // Not a PAC instruction - skip patching
        return;
    }
    
    // Clean cache for patched instruction
    if (IS_ENABLED(CONFIG_ARM64_WORKAROUND_CLEAN_CACHE))
        asm("dc civac, %0" :: "r"(loc));
    else
        asm(ALTERNATIVE("dc cvau, %0", "nop", ARM64_HAS_CACHE_IDC)
            :: "r"(loc));
}
```

5. Complete Data Flow Example

Let's trace through a real example:

DWARF Data in .eh_frame:

```
CIE Frame:
  size: 20
  cie_id: 0
  version: 1
  augmentation_string: "zR"
  code_alignment_factor: 1
  data_alignment_factor: -8  
  return_address_register: 30
  augmentation_data_size: 1
  fde_pointer_format: 0x1b (DW_EH_PE_pcrel|DW_EH_PE_sdata4)

FDE Frame: 
  size: 32
  cie_pointer: -20 (points to CIE above)
  initial_loc: 0xffffffc010123456 (function start)
  range: 0x100 (function size)
  opcodes:
    [0] DW_CFA_advance_loc1: 16  // Move 16 bytes into function
    [1] DW_CFA_negate_ra_state   // Mark PAC location
    [2] DW_CFA_advance_loc1: 240 // Move to end
    [3] DW_CFA_negate_ra_state   // Mark another PAC location
```

Execution Flow:

1. CIE Processing: Sets code_alignment_factor=1, fde_use_sdata8=false
2. FDE Processing:
   · Start location = 0xffffffc010123456
   · Advance 16 bytes → location = 0xffffffc010123466
   · Find DW_CFA_negate_ra_state → patch at 0xffffffc010123462 (loc - 4)
   · Advance 240 bytes → location = 0xffffffc010123556
   · Find DW_CFA_negate_ra_state → patch at 0xffffffc010123552

Memory Before/After Patching:

```
Before: 0xffffffc010123462: d503233f (PACIASP)
After:  0xffffffc010123462: f800865e (SCS_PUSH)

Before: 0xffffffc010123552: d50323bf (AUTIASP)  
After:  0xffffffc010123552: f85f8e5e (SCS_POP)
```

6. Key DWARF Symbol Relationships

DWARF Symbol Purpose in PatchSCS Effect
CIE.augmentation_string Validate DWARF format Must be "zR" or fail
CIE.code_alignment_factor Calculate instruction offsets Usually 1 (byte granularity)
CIE.return_address_register Validate ARM64 ABI Must be 30 (x30/LR)
CIE.fde_pointer_format Determine address size 4-byte vs 8-byte offsets
FDE.initial_loc Function start address Base for location tracking
DW_CFA_advance_loc* Move through function Updates current location
DW_CFA_negate_ra_state Mark PAC locations Triggers patching at (loc - 4)

7. Potential Failure Points

1. Bad CIE Data: Wrong augmentation string or register number
2. Location Calculation Errors: loc - 4 points to wrong memory
3. Opcode Misinterpretation: Unknown CFI opcodes break parsing
4. Wrong Instructions: Location doesn't contain PAC instructions
5. Cache Coherency: Patched instructions not flushed properly

8. Debugging the Complete Flow

Add these strategic debug points:

```c
// In scs_patch()
pr_info("PATCH: Processing frame: size=%u, cie_id=0x%x\n", 
        frame->size, frame->cie_id_or_pointer);

// In scs_handle_fde_frame()  
pr_info("FDE: Start=0x%llx, dry_run=%d\n", loc, dry_run);

// Before each opcode in the loop
pr_info("CFI: [loc=0x%llx] opcode=0x%02x\n", loc, current_opcode);

// At patch decision point
case DW_CFA_negate_ra_state:
    pr_info("PATCH: Found at loc=0x%llx, will patch 0x%llx\n", 
            loc, loc - 4);
    u32 current_insn = le32_to_cpup((void *)(loc - 4));
    pr_info("PATCH: Current instruction: 0x%x\n", current_insn);
    if (!dry_run)
        scs_patch_loc(loc - 4);
    break;
```

This gives you complete visibility into what the driver is doing with the DWARF data and where it's attempting to patch.

