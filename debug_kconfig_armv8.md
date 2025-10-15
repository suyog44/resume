
1. Enable Kconfig Dependency Tracing

Add this to your Kconfig files to trace dependencies:

```kconfig
# Temporary debug patch for arch/arm64/Kconfig

config AS_HAS_ARMV8_2
    $(info KCONFIG_DEPS: AS_HAS_ARMV8_2 being evaluated)
    $(info KCONFIG_DEPS:   - Direct dependencies: $(firstword $(filter-out y,$(AS_HAS_ARMV8_2_dep))))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.2-a)

config AS_HAS_ARMV8_3
    $(info KCONFIG_DEPS: AS_HAS_ARMV8_3 being evaluated) 
    $(info KCONFIG_DEPS:   - Direct dependencies: $(firstword $(filter-out y,$(AS_HAS_ARMV8_3_dep))))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.3-a)

config AS_HAS_ARMV8_4
    $(info KCONFIG_DEPS: AS_HAS_ARMV8_4 being evaluated)
    $(info KCONFIG_DEPS:   - Direct dependencies: $(firstword $(filter-out y,$(AS_HAS_ARMV8_4_dep))))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.4-a)

config AS_HAS_ARMV8_5
    $(info KCONFIG_DEPS: AS_HAS_ARMV8_5 being evaluated)
    $(info KCONFIG_DEPS:   - Direct dependencies: $(firstword $(filter-out y,$(AS_HAS_ARMV8_5_dep))))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.5-a)
```

2. Create a Kconfig Dependency Tracker

Add this script to trace what selects each config:

scripts/kconfig/trace_deps.sh:

```bash
#!/bin/bash
echo "=== KCONFIG DEPENDENCY TRACER ==="

# Find all Kconfig files that might select ARMv8 features
find . -name "Kconfig*" -exec grep -l -E "select.*AS_HAS_ARMV8" {} \; | while read file; do
    echo "Checking: $file"
    grep -n -B5 -A5 "select.*AS_HAS_ARMV8" "$file"
    echo "---"
done

# Check reverse dependencies
echo "=== REVERSE DEPENDENCIES ==="
for config in AS_HAS_ARMV8_2 AS_HAS_ARMV8_3 AS_HAS_ARMV8_4 AS_HAS_ARMV8_5; do
    echo "Config: $config"
    find . -name "Kconfig*" -exec grep -H "select.*$config" {} \;
    find . -name "Kconfig*" -exec grep -H "depends.*$config" {} \;
    echo ""
done
```

3. Enhanced Kconfig with Stack Tracing

Create a more advanced Kconfig debug patch:

```kconfig
# Enhanced debug for ARMv8 configs

menu "ARMv8 architectural features"

config DEBUG_ARMV8_DEPS
    bool "Debug ARMv8 dependencies"
    default y
    help
      Enable this to see what enables ARMv8 features

if DEBUG_ARMV8_DEPS
config AS_HAS_ARMV8_2
    $(info $(shell echo "KCONFIG_TRACE: [$$(date +%T)] $$(pwd)/Kconfig:$$(grep -n "config AS_HAS_ARMV8_2" arch/arm64/Kconfig | cut -d: -f1): AS_HAS_ARMV8_2"))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.2-a)
    
config AS_HAS_ARMV8_3  
    $(info $(shell echo "KCONFIG_TRACE: [$$(date +%T)] $$(pwd)/Kconfig:$$(grep -n "config AS_HAS_ARMV8_3" arch/arm64/Kconfig | cut -d: -f1): AS_HAS_ARMV8_3"))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.3-a)
    
config AS_HAS_ARMV8_4
    $(info $(shell echo "KCONFIG_TRACE: [$$(date +%T)] $$(pwd)/Kconfig:$$(grep -n "config AS_HAS_ARMV8_4" arch/arm64/Kconfig | cut -d: -f1): AS_HAS_ARMV8_4"))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.4-a)
    
config AS_HAS_ARMV8_5
    $(info $(shell echo "KCONFIG_TRACE: [$$(date +%T)] $$(pwd)/Kconfig:$$(grep -n "config AS_HAS_ARMV8_5" arch/arm64/Kconfig | cut -d: -f1): AS_HAS_ARMV8_5"))
    def_bool $(cc-option,-Wa$(comma)-march=armv8.5-a)
endif

endmenu
```

4. Use Kconfig's Built-in Dependency Analysis

```bash
# Method 1: Use listnewconfig to see implicit changes
make ARCH=arm64 listnewconfig

# Method 2: Check implied dependencies  
./scripts/kconfig/check_config.sh

# Method 3: Generate dependency graph
make ARCH=arm64 menuconfig
# Then press Shift + ? on any config to see its dependencies
```

5. Bazel-Specific Kconfig Dependency Tracing

Create a Bazel rule that traces Kconfig dependencies:

In your BUILD file:

```python
genrule(
    name = "trace_kconfig_deps",
    srcs = [
        "arch/arm64/Kconfig",
        ".config",
    ],
    outs = ["kconfig_dependencies.txt"],
    cmd = """
        echo "=== KCONFIG DEPENDENCY ANALYSIS ===" > $@
        echo "Timestamp: $$(date)" >> $@
        echo "" >> $@
        
        # Find what selects ARMv8 configs
        echo "=== WHAT SELECTS ARMv8 CONFIGS ===" >> $@
        find . -name "Kconfig*" -exec grep -l "select.*AS_HAS_ARMV8" {} \; >> $@
        echo "" >> $@
        
        # Show the actual select statements
        echo "=== SELECT STATEMENTS ===" >> $@
        find . -name "Kconfig*" -exec grep -H "select.*AS_HAS_ARMV8" {} \; >> $@
        echo "" >> $@
        
        # Show depends statements
        echo "=== DEPENDS STATEMENTS ===" >> $@
        find . -name "Kconfig*" -exec grep -H "depends.*AS_HAS_ARMV8" {} \; >> $@
        echo "" >> $@
        
        # Show current config state
        echo "=== CURRENT ARMv8 CONFIG STATE ===" >> $@
        grep "AS_HAS_ARMV8" $(location .config) >> $@
    """,
)
```

6. Run with Kconfig Debug Environment

```bash
# Set environment variables for Kconfig debugging
export KCONFIG_DEBUG=1
export KCONFIG_TRACE=1

# Run Bazel with environment variables
bazel build --action_env=KCONFIG_DEBUG=1 --action_env=KCONFIG_TRACE=1 //your:target

# Or run make directly to see dependencies
make ARCH=arm64 KCONFIG_DEBUG=1 olddefconfig 2>&1 | grep -E "selecting|selects|AS_HAS_ARMV8"
```

7. Create a Dependency Graph

scripts/kconfig/generate_deps.py:

```python
#!/usr/bin/env python3
import os
import re

def find_kconfig_dependencies():
    armv8_selectors = {}
    
    for root, dirs, files in os.walk('.'):
        for file in files:
            if file.startswith('Kconfig'):
                filepath = os.path.join(root, file)
                with open(filepath, 'r') as f:
                    content = f.read()
                    
                # Find configs that select ARMv8 features
                selects = re.findall(r'config\s+(\w+).*?select\s+(AS_HAS_ARMV8_\w+)', content, re.DOTALL)
                for selector, selected in selects:
                    if selected not in armv8_selectors:
                        armv8_selectors[selected] = []
                    armv8_selectors[selected].append((selector, filepath))
                    
                # Find depends relationships
                depends = re.findall(r'config\s+(\w+).*?depends on.*?(AS_HAS_ARMV8_\w+)', content, re.DOTALL)
                for dependent, depends_on in depends:
                    print(f"DEPENDS: {dependent} depends on {depends_on} in {filepath}")
    
    return armv8_selectors

if __name__ == "__main__":
    deps = find_kconfig_dependencies()
    for armv8_config, selectors in deps.items():
        print(f"\n{armv8_config} is selected by:")
        for selector, filepath in selectors:
            print(f"  - {selector} in {filepath}")
```

8. Quick Debug Command

Run this comprehensive dependency check:

```bash
#!/bin/bash
echo "=== KCONFIG DEPENDENCY DEBUG ==="

# 1. Clean and force reconfiguration
make ARCH=arm64 distclean

# 2. Run with dependency tracing
{
    echo "=== STARTING KCONFIG TRACE ==="
    make ARCH=arm64 KCONFIG_DEBUG=1 defconfig 2>&1 | grep -E "selecting|selected by|AS_HAS_ARMV8"
} > kconfig_deps.log 2>&1

# 3. Analyze results
echo "=== DEPENDENCY ANALYSIS ==="
grep -E "selecting|selected by" kconfig_deps.log | head -20

# 4. Show final config
echo "=== FINAL CONFIG ==="
grep "AS_HAS_ARMV8" .config
```

9. Check Specific Config Selection

To trace a specific config:

```bash
# Method 1: Use menuconfig's search
make ARCH=arm64 menuconfig
# Press '/' and search for AS_HAS_ARMV8_5

# Method 2: Check specific symbol
./scripts/kconfig/conf --olddefconfig Kconfig 2>&1 | grep -A10 -B10 "AS_HAS_ARMV8_5"
```

The most effective approach is usually Method 1 (Kconfig dependency tracing) combined with Method 5 (Bazel-specific tracing). This will show you exactly when each ARMv8 config is evaluated and what other configs are selecting them.