static void __init do_initcalls(void)
{
    int level;
    
    pr_emerg("=== INITCALL ARRAY CONTENT ANALYSIS ===\n");
    
    for (level = 0; level < ARRAY_SIZE(initcall_levels) - 1; level++) {
        initcall_t *fn;
        int entry_count = (initcall_levels[level+1] - initcall_levels[level]);
        
        pr_emerg("Level %d: %px to %px (%d entries)\n", 
                 level, initcall_levels[level], initcall_levels[level+1], entry_count);
        
        /* Check if setup_max_cpus appears in the initcall array (it shouldn't!) */
        for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++) {
            if (*fn == (initcall_t)&setup_max_cpus) {
                pr_emerg("*** CRITICAL: Initcall array contains setup_max_cpus pointer! ***\n");
                pr_emerg("*** At index %ld in level %d ***\n", fn - initcall_levels[level], level);
            }
            
            /* Check for obviously invalid function pointers */
            if ((unsigned long)*fn < 0xffffffc080000000UL) {
                pr_emerg("*** SUSPICIOUS: Initcall function %px looks invalid ***\n", *fn);
            }
        }
        
        /* Now do the actual initcall execution with extra checks */
        for (fn = initcall_levels[level]; fn < initcall_levels[level+1]; fn++) {
            pr_emerg("Executing initcall %px -> %px\n", fn, *fn);
            
            if (initcall_blacklisted(fn))
                continue;
                
            (*fn)();
        }
    }
}
