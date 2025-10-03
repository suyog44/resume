Perfect! Here's how to recreate the function to print a complete hex dump of the constructor functions with raw bytes:

🖨️ Complete Hex Dump Function

```c
void do_ctors_with_hexdump(void)
{
    ctor_fn_t *fn;
    unsigned long ctor_count = 0;
    
    printk(KERN_INFO "=== Complete Constructor Hex Dump ===\n");
    
    for (fn = __ctors_start; fn < __ctors_end; fn++, ctor_count++) {
        unsigned char *code_ptr = (unsigned char *)(*fn);
        unsigned long function_size = 0;
        int i, j;
        
        /* Calculate function size by finding next function (or use reasonable limit) */
        if (fn + 1 < __ctors_end) {
            function_size = (unsigned long)(*(fn + 1)) - (unsigned long)(*fn);
        } else {
            /* Last function - use reasonable size limit */
            function_size = 512; /* Assume max 512 bytes per function */
        }
        
        printk(KERN_INFO "\n--- Constructor [%lu] at 0x%px (size: %lu bytes) ---\n",
               ctor_count, *fn, function_size);
        
        /* Print hex dump in rows of 16 bytes */
        for (i = 0; i < function_size; i += 16) {
            /* Print address */
            printk(KERN_INFO "%04x: ", i);
            
            /* Print hex bytes */
            for (j = 0; j < 16; j++) {
                if (i + j < function_size) {
                    printk(KERN_CONT "%02x ", code_ptr[i + j]);
                } else {
                    printk(KERN_CONT "   "); /* Padding for incomplete lines */
                }
                
                if (j == 7) printk(KERN_CONT " "); /* Space between groups */
            }
            
            printk(KERN_CONT " |");
            
            /* Print ASCII representation */
            for (j = 0; j < 16 && i + j < function_size; j++) {
                unsigned char c = code_ptr[i + j];
                printk(KERN_CONT "%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
            }
            
            printk(KERN_CONT "|\n");
        }
        
        /* Call the constructor after dumping */
        printk(KERN_INFO "Calling constructor...\n");
        (*fn)();
    }
    
    printk(KERN_INFO "=== Finished %lu constructors ===\n", ctor_count);
}
```

🔧 Smart Size Detection Version

```c
void do_ctors_smart_dump(void)
{
    ctor_fn_t *fn;
    
    printk(KERN_INFO "=== Smart Constructor Dump ===\n");
    
    for (fn = __ctors_start; fn < __ctors_end; fn++) {
        unsigned char *code_ptr = (unsigned char *)(*fn);
        unsigned long next_addr;
        unsigned long size;
        
        /* Determine where this function ends */
        if (fn + 1 < __ctors_end) {
            next_addr = (unsigned long)(*(fn + 1));
        } else {
            /* For last function, look for common function endings */
            next_addr = (unsigned long)(*fn) + 256; /* Conservative estimate */
        }
        
        size = next_addr - (unsigned long)(*fn);
        if (size > 1024) size = 256; /* Sanity check */
        
        printk(KERN_INFO "\nConstructor: 0x%px (dumping %lu bytes)\n", *fn, size);
        dump_memory_hex(code_ptr, size);
        
        (*fn)();
    }
}

/* Helper function to dump memory */
void dump_memory_hex(unsigned char *data, unsigned long size)
{
    unsigned long i, j;
    
    for (i = 0; i < size; i += 16) {
        /* Address */
        printk(KERN_INFO "%04lx: ", i);
        
        /* Hex bytes */
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                printk(KERN_CONT "%02x ", data[i + j]);
            } else {
                printk(KERN_CONT "   ");
            }
            if (j == 7) printk(KERN_CONT " ");
        }
        
        printk(KERN_CONT " |");
        
        /* ASCII */
        for (j = 0; j < 16 && i + j < size; j++) {
            unsigned char c = data[i + j];
            printk(KERN_CONT "%c", (c >= 0x20 && c <= 0x7E) ? c : '.');
        }
        
        printk(KERN_CONT "|\n");
    }
}
```

⚡ Compact Raw Bytes Only Version

```c
void do_ctors_raw_dump(void)
{
    ctor_fn_t *fn;
    
    for (fn = __ctors_start; fn < __ctors_end; fn++) {
        unsigned char *code = (unsigned char *)(*fn);
        unsigned long size = 64; /* Dump first 64 bytes */
        unsigned long i;
        
        printk(KERN_INFO "RAW 0x%px: ", *fn);
        
        /* Print raw bytes in continuous stream */
        for (i = 0; i < size; i++) {
            printk(KERN_CONT "%02x", code[i]);
            if (i < size - 1) printk(KERN_CONT " ");
        }
        printk(KERN_CONT "\n");
        
        (*fn)();
    }
}
```

🎯 Example Output

```
=== Complete Constructor Hex Dump ===

--- Constructor [0] at 0xffffffff81234500 (size: 48 bytes) ---
0000: 55 48 89 e5 41 57 41 56  41 55 41 54 53 48 83 ec | UH..AWAVAUATSH..
0010: 18 48 c7 c7 00 45 23 81  e8 aa bb cc dd 48 89 c3 | .H...E#......H..
0020: 85 c0 75 07 48 89 d8 48  83 c4 18 5b c9 c3 48 89 | ..u.H..H...[..H.

Calling constructor...

--- Constructor [1] at 0xffffffff81234600 (size: 64 bytes) ---
0000: 55 48 89 e5 48 83 ec 10  48 c7 45 f8 00 00 00 00 | UH..H...H.E.....
0010: 48 8b 05 aa bb cc dd 48  89 45 f0 48 8b 45 f0 48 | H......H.E.H.E.H
0020: 8b 00 48 89 c7 e8 ee ff  ff ff 48 8b 45 f0 48 c7 | ..H.......H.E.H.
0030: 00 00 00 00 00 48 83 c4  10 5d c3 00 00 00 00 00 | .....H...]......

Calling constructor...
```

🔍 Function Size Detection Methods

```c
/* More sophisticated size detection */
unsigned long detect_function_size(unsigned char *code)
{
    /* Method 1: Look for common function endings (ret instruction) */
    for (int i = 0; i < 512; i++) {
        /* x86 ret instruction is 0xC3 */
        if (code[i] == 0xC3) {
            return i + 1;
        }
        /* x86 ret with stack adjustment might be 0xC2 xx xx */
        if (code[i] == 0xC2) {
            return i + 3;
        }
    }
    
    /* Method 2: Conservative default */
    return 128;
}
```

💡 Usage Tips

1. For complete analysis: Use the first function with full hex+ASCII dump
2. For raw data extraction: Use the compact raw bytes version
3. Adjust size limits based on your architecture and needs
4. Be careful with size detection - functions might have multiple return points

This gives you both the complete memory content and preserves the raw bytes for analysis!