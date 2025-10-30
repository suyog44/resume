Based on all the code you've shared, I'll create a comprehensive debug patch that will pinpoint exactly where the null pointer dereference is occurring. Let me create a targeted debug strategy:

Complete Debug Patch for Your Scenario

1. Enhanced map_kernel function (add to your mapping code):

```c
static void __init map_kernel(u64 kaslr_offset, u64 va_offset, int root_level)
{
	bool enable_scs = IS_ENABLED(CONFIG_UNWIND_PATCH_PAC_INTO_SCS);
	bool twopass = IS_ENABLED(CONFIG_RELOCATABLE);
	u64 pgdp = (u64)init_pg_dir + PAGE_SIZE;
	pgprot_t text_prot = PAGE_KERNEL_ROX;
	pgprot_t data_prot = PAGE_KERNEL;
	pgprot_t prot;

	// Add debug headers
	#include <linux/printk.h>
	#define pr_fmt(fmt) "KERNEL_MAP: " fmt
	
	pr_info("=== KERNEL MAPPING START ===\n");
	pr_info("kaslr_offset: 0x%llx, va_offset: 0x%llx, root_level: %d\n", 
	        kaslr_offset, va_offset, root_level);
	
	/* ... existing feature checks ... */

	// Enhanced debug for SCS decision
	pr_info("SCS_DECISION: CONFIG_UNWIND_PATCH_PAC_INTO_SCS=%d\n", 
	        IS_ENABLED(CONFIG_UNWIND_PATCH_PAC_INTO_SCS));
	pr_info("SCS_DECISION: cpu_has_pac()=%d, cpu_has_bti()=%d\n", 
	        cpu_has_pac(), cpu_has_bti());
	pr_info("SCS_DECISION: Initial enable_scs=%d, twopass=%d\n", 
	        enable_scs, twopass);

	if (IS_ENABLED(CONFIG_ARM64_PTR_AUTH_KERNEL) && cpu_has_pac())
		enable_scs = false;
	if (IS_ENABLED(CONFIG_ARM64_BTI_KERNEL) && cpu_has_bti()) {
		enable_scs = false;
		/* ... BTI setup ... */
	}

	pr_info("SCS_DECISION: Final enable_scs=%d\n", enable_scs);
	
	/* Map all code read-write on the first pass if needed */
	twopass |= enable_scs;
	prot = twopass ? data_prot : text_prot;

	pr_info("MAPPING: First pass with %s permissions\n", 
	        twopass ? "READ-WRITE" : "READ-ONLY");
	
	/* ... existing mapping calls ... */

	dsb(ishst);
	idmap_cpu_replace_ttbr1(init_pg_dir);

	if (twopass) {
		pr_info("TWOPASS: Starting second pass processing\n");
		
		if (IS_ENABLED(CONFIG_RELOCATABLE)) {
			pr_info("TWOPASS: Relocating kernel...\n");
			relocate_kernel(kaslr_offset);
			pr_info("TWOPASS: Kernel relocation completed\n");
		}
		
		if (enable_scs) {
			pr_info("=== SCS PATCHING START ===\n");
			
			// CRITICAL: Validate all addresses before patching
			pr_info("SCS_PREP: __eh_frame_start = %p\n", __eh_frame_start);
			pr_info("SCS_PREP: __eh_frame_end = %p\n", __eh_frame_end);
			pr_info("SCS_PREP: va_offset = 0x%llx\n", va_offset);
			
			u64 eh_frame_target = (u64)__eh_frame_start + va_offset;
			pr_info("SCS_PREP: Target address = 0x%llx\n", eh_frame_target);
			
			size_t eh_frame_size = __eh_frame_end - __eh_frame_start;
			pr_info("SCS_PREP: EH frame size = %zu bytes\n", eh_frame_size);
			
			// Validate the target address range
			if (!__eh_frame_start || !__eh_frame_end) {
				pr_err("SCS_CRITICAL: EH frame symbols are NULL!\n");
				pr_err("SCS_CRITICAL: __eh_frame_start=%p, __eh_frame_end=%p\n",
				       __eh_frame_start, __eh_frame_end);
				goto skip_scs;
			}
			
			if (eh_frame_size <= 0 || eh_frame_size > SZ_1M) {
				pr_err("SCS_CRITICAL: Invalid EH frame size: %zu\n", eh_frame_size);
				goto skip_scs;
			}
			
			if (eh_frame_target < PAGE_OFFSET) {
				pr_err("SCS_CRITICAL: Target address below PAGE_OFFSET: 0x%llx\n", 
				       eh_frame_target);
				goto skip_scs;
			}
			
			pr_info("SCS_PREP: All validations passed, calling scs_patch...\n");
			
			// THE ACTUAL PATCHING CALL
			int patch_result = scs_patch((u8 *)eh_frame_target, eh_frame_size);
			
			pr_info("SCS_PATCH: scs_patch returned: %d\n", patch_result);
			
			asm("ic ialluis");
			dynamic_scs_is_enabled = true;
			pr_info("SCS_PATCH: Patching completed successfully\n");
			
skip_scs:
			pr_info("=== SCS PATCHING END ===\n");
		}

		pr_info("TWOPASS: Unmapping text segment for remapping...\n");
		unmap_segment(init_pg_dir, va_offset, _stext, _etext, root_level);
		dsb(ishst);
		isb();
		__tlbi(vmalle1);
		isb();

		pr_info("TWOPASS: Remapping text as read-only...\n");
		map_segment(init_pg_dir, NULL, va_offset, _stext, _etext,
			    text_prot, true, root_level);
		map_segment(init_pg_dir, NULL, va_offset, __inittext_begin,
			    __inittext_end, text_prot, false, root_level);
	}
	
	pr_info("TWOPASS: Copying to swapper_pg_dir...\n");
	memcpy((void *)swapper_pg_dir + va_offset, init_pg_dir, PAGE_SIZE);
	dsb(ishst);
	idmap_cpu_replace_ttbr1(swapper_pg_dir);
	
	pr_info("=== KERNEL MAPPING COMPLETED ===\n");
}
```

2. Enhanced scs_patch function (replace your existing one):

```c
#include <linux/printk.h>
#include <linux/kernel.h>

#define pr_fmt(fmt) "SCS_PATCH: " fmt

// Safe memory validation
static bool __always_inline scs_validate_memory(const void *ptr, size_t size)
{
    if (!ptr) {
        pr_err("NULL pointer passed to validation!\n");
        return false;
    }
    
    if ((u64)ptr < PAGE_OFFSET) {
        pr_err("Pointer below PAGE_OFFSET: %p\n", ptr);
        return false;
    }
    
    // Check if we can safely read the first byte
    if (probe_kernel_read((void *)&size, ptr, 1)) {
        pr_err("Cannot read from address: %p\n", ptr);
        return false;
    }
    
    return true;
}

int scs_patch(const u8 eh_frame[], int size)
{
    pr_info("=== SCS_PATCH CALLED ===\n");
    pr_info("eh_frame: %p, size: %d\n", eh_frame, size);
    
    // CRITICAL VALIDATION
    if (!eh_frame) {
        pr_err("CRITICAL: eh_frame is NULL pointer!\n");
        dump_stack();
        return -EINVAL;
    }
    
    if (size <= 0) {
        pr_err("CRITICAL: Invalid size: %d\n", size);
        return -EINVAL;
    }
    
    if (!scs_validate_memory(eh_frame, size)) {
        pr_err("CRITICAL: eh_frame points to invalid memory!\n");
        pr_err("Address: %p, trying to access %d bytes\n", eh_frame, size);
        dump_stack();
        return -EFAULT;
    }
    
    pr_info("Memory validation passed, starting DWARF parsing...\n");
    
    int code_alignment_factor = 1;
    bool fde_use_sdata8 = false;
    const u8 *p = eh_frame;
    int frame_count = 0;
    
    pr_info("Starting frame processing with %d bytes total\n", size);
    
    while (size > 4) {
        const struct eh_frame *frame = (const void *)p;
        frame_count++;
        
        pr_info("Processing frame %d at offset %ld\n", 
                frame_count, p - eh_frame);
        
        // Validate frame header
        if (!scs_validate_memory(frame, sizeof(*frame))) {
            pr_err("Cannot access frame header at %p\n", frame);
            return -EFAULT;
        }
        
        pr_info("Frame %d: size=%u, cie_id=0x%x\n", 
                frame_count, frame->size, frame->cie_id_or_pointer);
        
        if (frame->size == 0 || frame->size == U32_MAX || frame->size > size) {
            pr_info("Terminating: invalid frame size or end of data\n");
            break;
        }

        if (frame->cie_id_or_pointer == 0) {
            pr_info("Frame %d: CIE processing\n", frame_count);
            
            // Validate CIE memory access
            if (!scs_validate_memory(frame, sizeof(*frame) + 10)) {
                pr_err("Cannot safely access CIE structure\n");
                return -EFAULT;
            }
            
            /* ... existing CIE processing with added debug ... */
            pr_info("CIE: aug_string='%.3s'\n", frame->augmentation_string);
            
        } else {
            pr_info("Frame %d: FDE processing\n", frame_count);
            
            // Enhanced FDE processing with safety
            int ret = scs_handle_fde_frame(frame, code_alignment_factor,
                           fde_use_sdata8, true); // Dry run
            if (ret) {
                pr_err("FDE dry run failed: %d\n", ret);
                return ret;
            }
            
            pr_info("FDE dry run passed, executing actual patch...\n");
            scs_handle_fde_frame(frame, code_alignment_factor,
                           fde_use_sdata8, false);
        }

        int frame_total_size = sizeof(frame->size) + frame->size;
        p += frame_total_size;
        size -= frame_total_size;
        
        pr_info("Frame %d completed, %d bytes remaining\n", frame_count, size);
    }
    
    pr_info("=== SCS_PATCH COMPLETED: Processed %d frames ===\n", frame_count);
    return 0;
}
```

3. Critical Safety Check in scs_patch_loc:

```c
static void __always_inline scs_patch_loc(u64 loc)
{
    pr_info("SCS_PATCH_LOC: Attempting to patch at 0x%llx\n", loc);
    
    // NULL pointer and boundary check
    if (loc == 0) {
        pr_err("SCS_CRITICAL: NULL pointer passed to scs_patch_loc!\n");
        dump_stack();
        return;
    }
    
    if (loc < PAGE_OFFSET) {
        pr_err("SCS_CRITICAL: Invalid address below PAGE_OFFSET: 0x%llx\n", loc);
        dump_stack();
        return;
    }
    
    // Safe memory read
    u32 insn;
    if (probe_kernel_read(&insn, (void *)loc, sizeof(insn))) {
        pr_err("SCS_CRITICAL: Cannot read instruction at 0x%llx\n", loc);
        return;
    }
    
    pr_info("SCS_PATCH_LOC: Read instruction 0x%x from 0x%llx\n", insn, loc);
    
    /* ... rest of existing patching logic with debug ... */
}
```

4. Add to early_map_kernel for complete tracing:

```c
asmlinkage void __init early_map_kernel(u64 boot_status, void *fdt)
{
    // Add at the beginning
    #include <linux/printk.h>
    #define pr_fmt(fmt) "EARLY_MAP: " fmt
    
    pr_info("=== EARLY KERNEL MAPPING START ===\n");
    pr_info("boot_status: 0x%llx, fdt: %p\n", boot_status, fdt);
    
    /* ... existing code with strategic debug prints ... */
    
    pr_info("EARLY_MAP: va_base=0x%llx, pa_base=0x%llx\n", va_base, pa_base);
    pr_info("EARLY_MAP: va_offset=0x%llx\n", va_base - pa_base);
    
    map_kernel(kaslr_offset, va_base - pa_base, root_level);
    
    pr_info("=== EARLY KERNEL MAPPING COMPLETED ===\n");
}
```

What This Debug Patch Will Reveal:

Scenario 1: NULL eh_frame symbols

```
EARLY_MAP: === EARLY KERNEL MAPPING START ===
KERNEL_MAP: === KERNEL MAPPING START ===
SCS_PREP: __eh_frame_start = (null)
SCS_CRITICAL: EH frame symbols are NULL!
```

Scenario 2: Invalid address calculation

```
SCS_PREP: Target address = 0x0
SCS_CRITICAL: Target address below PAGE_OFFSET: 0x0
```

Scenario 3: Memory access issues

```
SCS_PATCH: === SCS_PATCH CALLED ===
SCS_PATCH: eh_frame: 0xffffffc123456789, size: 8192
SCS_PATCH: Memory validation passed, starting DWARF parsing...
SCS_PATCH: Cannot access frame header at 0xffffffc123456789
```

Scenario 4: Actual null pointer in patching

```
SCS_PATCH_LOC: Attempting to patch at 0x0
SCS_CRITICAL: NULL pointer passed to scs_patch_loc!
```

Application Instructions:

1. Add the includes at the top of your mapping file:

```c
#include <linux/printk.h>
#include <linux/kernel.h>
```

1. Replace the map_kernel function with the debug version
2. Replace the scs_patch function with the debug version
3. Add the safety checks to scs_patch_loc
4. Add debug to early_map_kernel

Expected Outcome:

This will definitively show you:

· If SCS patching is being attempted on your ARMv8.2 system
· Where the null pointer is being generated
· What the invalid memory access is
· Whether the DWARF data is corrupted or addresses are miscalculated

The dump_stack() calls will show the exact call chain leading to the null pointer, which is crucial for understanding the root cause.