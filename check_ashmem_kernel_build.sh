#!/bin/bash
# check_ashmem_kernel_build.sh
#
# Checks the ACTUAL ashmem Rust object from a kernel build for
# str x30 vs stp x29, x30 prologues.
#
# This inspects the real compiled .o file — no standalone compilation needed.
#
# Usage:
#   ./check_ashmem_kernel_build.sh /path/to/kernel_platform
#
# Example (Bazel/Kleaf):
#   ./check_ashmem_kernel_build.sh /local/mnt/workspace/sburadka/ASPEN_KASAN/kernel_platform
#
# The script searches for the ashmem Rust object in the Bazel output tree
# and also checks rust/core objects.

set -euo pipefail

KERNEL_PLATFORM="${1:-.}"
OBJDUMP="${LLVM_OBJDUMP:-llvm-objdump}"

command -v "$OBJDUMP" >/dev/null 2>&1 || {
    # Try kernel prebuilts
    PREBUILT="$KERNEL_PLATFORM/prebuilts/clang/host/linux-x86/clang-r530567/bin/llvm-objdump"
    if [ -x "$PREBUILT" ]; then
        OBJDUMP="$PREBUILT"
    else
        echo "ERROR: llvm-objdump not found. Set LLVM_OBJDUMP or pass kernel_platform path."
        exit 1
    fi
}

echo "objdump : $OBJDUMP"
echo "platform: $KERNEL_PLATFORM"
echo

# ═══════════════════════════════════════════════════════════════════
# Find the ashmem Rust object file
# ═══════════════════════════════════════════════════════════════════

echo "Searching for ashmem Rust objects..."

ASHMEM_OBJS=$(find "$KERNEL_PLATFORM" -path '*/bazel*' -name '*.o' 2>/dev/null | \
    xargs grep -l 'ashmem' 2>/dev/null | head -5 || true)

# Also search by filename pattern
ASHMEM_OBJS2=$(find "$KERNEL_PLATFORM/out" -name 'ashmem*.o' -o -name 'ashmem*.rlib' 2>/dev/null || true)

# Search for the Rust core object too
CORE_OBJS=$(find "$KERNEL_PLATFORM/out" -name 'core*.o' -path '*/rust/*' 2>/dev/null | head -3 || true)

# Combine all
ALL_OBJS=""
for obj in $ASHMEM_OBJS $ASHMEM_OBJS2 $CORE_OBJS; do
    if [ -f "$obj" ] && file "$obj" | grep -q 'ELF.*aarch64'; then
        ALL_OBJS="$ALL_OBJS $obj"
    fi
done

if [ -z "$ALL_OBJS" ]; then
    echo "No aarch64 ELF objects found. Try these manual steps:"
    echo
    echo "  # Find the ashmem object in bazel output:"
    echo "  find \$KERNEL_PLATFORM/out -name 'ashmem*' -name '*.o' | head -10"
    echo
    echo "  # Or check the vmlinux directly:"
    echo "  $OBJDUMP -d \$KERNEL_PLATFORM/out/msm-kernel-vienna-consolidate/dist/vmlinux | \\"
    echo "    awk '/^[0-9a-f]+ <.*>:/{fn=\$0} /str.*x30, \\[sp/{print fn; print \"  \"\$0}'"
    echo
    echo "  # Check specifically for asan.module_ctor:"
    echo "  $OBJDUMP -d vmlinux | grep -B1 -A10 '<asan.module_ctor'"
    exit 1
fi

echo "Found objects:"
for obj in $ALL_OBJS; do
    echo "  $obj"
done
echo

# ═══════════════════════════════════════════════════════════════════
# Analyze each object
# ═══════════════════════════════════════════════════════════════════

analyze_object() {
    local obj="$1"
    local name=$(basename "$obj")

    echo "═══════════════════════════════════════════════════════════"
    echo "  $name"
    echo "  $obj"
    echo "═══════════════════════════════════════════════════════════"

    local str_count stp_count paciasp_count total_funcs ctor_count
    str_count=$("$OBJDUMP" -d "$obj" 2>/dev/null | grep -c 'str.*x30, \[sp' || true)
    stp_count=$("$OBJDUMP" -d "$obj" 2>/dev/null | grep -c 'stp.*x29.*x30' || true)
    paciasp_count=$("$OBJDUMP" -d "$obj" 2>/dev/null | grep -c 'paciasp' || true)
    total_funcs=$("$OBJDUMP" -d "$obj" 2>/dev/null | grep -c '^[0-9a-f]\+ <' || true)
    ctor_count=$("$OBJDUMP" -d "$obj" 2>/dev/null | grep -c '<asan.module_ctor>' || true)

    echo
    echo "  Total functions          : $total_funcs"
    echo "  paciasp (PAC-RET hints)  : $paciasp_count"
    echo "  stp x29, x30 (GOOD)     : $stp_count"
    echo "  str x30       (BAD)     : $str_count"
    echo "  asan.module_ctor present : $([ "$ctor_count" -gt 0 ] && echo YES || echo NO)"

    if [ "$str_count" -gt 0 ]; then
        echo
        echo "  *** Functions with str x30 — these BREAK SCS patching: ***"
        echo "  ─────────────────────────────────────────────────────────"
        "$OBJDUMP" -d "$obj" | \
            awk '/^[0-9a-f]+ <.*>:/{fn=$0} /str.*x30, \[sp/{print "    " fn; print "      " $0}'
    fi

    # Dump asan.module_ctor prologue if present
    if [ "$ctor_count" -gt 0 ]; then
        echo
        echo "  asan.module_ctor disassembly (first 12 instructions):"
        echo "  ─────────────────────────────────────────────────────────"
        "$OBJDUMP" -d "$obj" | \
            awk '/^[0-9a-f]+ <asan.module_ctor>:/{found=1}
                 found{print "    "$0; n++; if(n>12)found=0}'
    fi

    echo
}

TOTAL_BAD=0
for obj in $ALL_OBJS; do
    analyze_object "$obj"
    bad=$("$OBJDUMP" -d "$obj" 2>/dev/null | grep -c 'str.*x30, \[sp' || true)
    TOTAL_BAD=$((TOTAL_BAD + bad))
done

# ═══════════════════════════════════════════════════════════════════
# Also check vmlinux if available
# ═══════════════════════════════════════════════════════════════════

VMLINUX=$(find "$KERNEL_PLATFORM/out" -name 'vmlinux' -path '*/dist/*' 2>/dev/null | head -1 || true)

if [ -n "$VMLINUX" ] && [ -f "$VMLINUX" ]; then
    echo "═══════════════════════════════════════════════════════════"
    echo "  VMLINUX: $VMLINUX"
    echo "═══════════════════════════════════════════════════════════"

    vmlinux_str=$("$OBJDUMP" -d "$VMLINUX" 2>/dev/null | grep -c 'str.*x30, \[sp' || true)
    vmlinux_stp=$("$OBJDUMP" -d "$VMLINUX" 2>/dev/null | grep -c 'stp.*x29.*x30' || true)
    vmlinux_ctor=$("$OBJDUMP" -d "$VMLINUX" 2>/dev/null | grep -c '<asan.module_ctor>' || true)

    echo
    echo "  stp x29, x30 (GOOD) : $vmlinux_stp"
    echo "  str x30       (BAD) : $vmlinux_str"
    echo "  asan.module_ctor    : $vmlinux_ctor instance(s)"

    if [ "$vmlinux_str" -gt 0 ]; then
        echo
        echo "  *** ALL str x30 sites in vmlinux: ***"
        "$OBJDUMP" -d "$VMLINUX" | \
            awk '/^[0-9a-f]+ <.*>:/{fn=$0} /str.*x30, \[sp/{print "    " fn; print "      " $0}' | \
            head -60
    fi
    echo
fi

# ═══════════════════════════════════════════════════════════════════
echo "═══════════════════════════════════════════════════════════"
echo "  SUMMARY"
echo "═══════════════════════════════════════════════════════════"
echo
if [ "$TOTAL_BAD" -gt 0 ]; then
    echo "  $TOTAL_BAD str x30 prologue(s) found across Rust objects."
    echo "  These break SCS paciasp→push patching."
    echo
    echo "  If all are in asan.module_ctor: LLVM sanitizer bug"
    echo "  If any in user functions too : missing -Cforce-frame-pointers=yes"
else
    echo "  No str x30 found — all Rust functions use stp x29, x30."
fi
echo
