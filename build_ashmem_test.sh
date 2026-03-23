#!/bin/bash
# build_ashmem_test.sh
#
# Builds test_ashmem_asan_ctor.rs using the exact KBUILD_RUSTFLAGS
# from the GKI android16-6.12 vienna_consolidate KASAN build log.
#
# Usage:
#   ./build_ashmem_test.sh [path/to/rustc]
#
# The rustc must be the kernel's nightly with -Zsanitizer support.
# Typically at: kernel_platform/prebuilts/rust/linux-x86/bin/rustc

set -euo pipefail

RUSTC="${1:-rustc}"
OBJDUMP="${LLVM_OBJDUMP:-llvm-objdump}"
SRC="$(dirname "$0")/test_ashmem_asan_ctor.rs"

command -v "$RUSTC" >/dev/null 2>&1 || { echo "ERROR: rustc not found"; exit 1; }
command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "ERROR: llvm-objdump not found"; exit 1; }

echo "rustc  : $("$RUSTC" --version 2>&1 || echo unknown)"
echo "objdump: $OBJDUMP"
echo "source : $SRC"
echo

# ── Exact KBUILD_RUSTFLAGS from build log line 13719 ──
# (minus the lint flags which we handle with #![allow] in the source)
KBUILD_RUSTFLAGS=(
    --edition=2021
    --crate-type=lib
    -Zbinary_dep_depinfo=y
    -Astable_features
    -Dnon_ascii_idents
    -Dunsafe_op_in_unsafe_fn
    -Cpanic=abort
    -Cembed-bitcode=n
    -Clto=n
    -Cforce-unwind-tables=n
    -Ccodegen-units=1
    -Csymbol-mangling-version=v0
    -Crelocation-model=static
    -Zfunction-sections=n
    --target=aarch64-unknown-none
    -Ctarget-feature=-neon
    -Cforce-unwind-tables=y
    -Zuse-sync-unwind=n
    -Zbranch-protection=pac-ret
    -Zfixed-x18
    --emit=obj
)

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# ═══════════════════════════════════════════════════════════════
echo "═══════════════════════════════════════════════════════════"
echo "  Build 1: BASELINE — no KASAN, with -Cforce-frame-pointers=yes"
echo "═══════════════════════════════════════════════════════════"
BASELINE="$TMPDIR/baseline.o"
"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" \
    -Cforce-frame-pointers=yes \
    -o "$BASELINE" "$SRC" 2>&1 || { echo "COMPILE FAILED"; exit 1; }
echo "  OK → $BASELINE"

# ═══════════════════════════════════════════════════════════════
echo
echo "═══════════════════════════════════════════════════════════"
echo "  Build 2: KASAN + -Cforce-frame-pointers=yes (proposed fix)"
echo "═══════════════════════════════════════════════════════════"
KASAN_FP="$TMPDIR/kasan_fp.o"
"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" \
    -Cforce-frame-pointers=yes \
    -Zsanitizer=kernel-address \
    -o "$KASAN_FP" "$SRC" 2>&1 || { echo "COMPILE FAILED"; exit 1; }
echo "  OK → $KASAN_FP"

# ═══════════════════════════════════════════════════════════════
echo
echo "═══════════════════════════════════════════════════════════"
echo "  Build 3: KASAN WITHOUT -Cforce-frame-pointers (current broken)"
echo "═══════════════════════════════════════════════════════════"
KASAN_NOFP="$TMPDIR/kasan_nofp.o"
"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" \
    -Zsanitizer=kernel-address \
    -o "$KASAN_NOFP" "$SRC" 2>&1 || { echo "COMPILE FAILED"; exit 1; }
echo "  OK → $KASAN_NOFP"

echo
echo

# ═══════════════════════════════════════════════════════════════
# Analysis
# ═══════════════════════════════════════════════════════════════

analyze() {
    local obj="$1"
    local label="$2"

    echo "═══════════════════════════════════════════════════════════"
    echo "  $label"
    echo "═══════════════════════════════════════════════════════════"

    local str_count stp_count paciasp_count total_funcs ctor_count
    str_count=$("$OBJDUMP" -d "$obj" | grep -c 'str.*x30, \[sp' || true)
    stp_count=$("$OBJDUMP" -d "$obj" | grep -c 'stp.*x29.*x30' || true)
    paciasp_count=$("$OBJDUMP" -d "$obj" | grep -c 'paciasp' || true)
    total_funcs=$("$OBJDUMP" -d "$obj" | grep -c '^[0-9a-f]\+ <' || true)
    ctor_count=$("$OBJDUMP" -d "$obj" | grep -c 'asan.module_ctor' || true)

    echo
    echo "  Total functions          : $total_funcs"
    echo "  paciasp (PAC-RET)        : $paciasp_count"
    echo "  stp x29, x30 (GOOD)     : $stp_count"
    echo "  str x30       (BAD)     : $str_count"
    echo "  asan.module_ctor present : $([ "$ctor_count" -gt 0 ] && echo YES || echo NO)"

    if [ "$str_count" -gt 0 ]; then
        echo
        echo "  *** str x30 sites: ***"
        "$OBJDUMP" -d "$obj" | \
            awk '/^[0-9a-f]+ <.*>:/{fn=$0} /str.*x30, \[sp/{print "    " fn; print "      " $0}'
    fi

    if [ "$ctor_count" -gt 0 ]; then
        echo
        echo "  asan.module_ctor prologue:"
        "$OBJDUMP" -d "$obj" | \
            awk 'BEGIN{n=0} /asan.module_ctor>:/{found=1} found{print "    "$0; n++; if(n>8)found=0}'
    fi

    echo
    return "$str_count"
}

set +e

analyze "$BASELINE"  "BASELINE — no KASAN, with frame pointers"
baseline_bad=$?

analyze "$KASAN_NOFP" "KASAN WITHOUT -Cforce-frame-pointers (current build)"
nofp_bad=$?

analyze "$KASAN_FP"   "KASAN WITH -Cforce-frame-pointers=yes (proposed fix)"
fp_bad=$?

set -e

# ═══════════════════════════════════════════════════════════════
echo "═══════════════════════════════════════════════════════════"
echo "  VERDICT"
echo "═══════════════════════════════════════════════════════════"
echo
echo "  baseline (no KASAN)     : $baseline_bad str x30"
echo "  KASAN (no FP flag)      : $nofp_bad str x30"
echo "  KASAN (with FP flag)    : $fp_bad str x30"
echo

if [ "$fp_bad" -gt 0 ] && [ "$nofp_bad" -gt 0 ]; then
    echo "  RESULT: asan.module_ctor IGNORES -Cforce-frame-pointers=yes"
    echo
    echo "  Two bugs:"
    echo "  1. Kernel: KBUILD_RUSTFLAGS missing -Cforce-frame-pointers=yes"
    echo "     → fixes user functions but not asan.module_ctor"
    echo "  2. LLVM: asan.module_ctor skips frame-pointer=all attribute"
    echo "     → file at https://github.com/llvm/llvm-project/issues"
elif [ "$nofp_bad" -gt 0 ] && [ "$fp_bad" -eq 0 ]; then
    echo "  RESULT: -Cforce-frame-pointers=yes fixes everything"
    echo "  → only Makefile patch needed"
elif [ "$nofp_bad" -eq 0 ]; then
    echo "  RESULT: no str x30 in any build — your rustc/LLVM may"
    echo "  already handle this. Verify with exact kernel toolchain."
fi

echo
echo "═══════════════════════════════════════════════════════════"

# ═══════════════════════════════════════════════════════════════
# Optional: dump IR to show attribute mismatch
# ═══════════════════════════════════════════════════════════════

if [ "${DUMP_IR:-0}" = "1" ]; then
    echo
    echo "═══════════════════════════════════════════════════════════"
    echo "  LLVM IR attribute check"
    echo "═══════════════════════════════════════════════════════════"

    KASAN_IR="$TMPDIR/kasan.ll"
    "$RUSTC" "${KBUILD_RUSTFLAGS[@]}" \
        -Cforce-frame-pointers=yes \
        -Zsanitizer=kernel-address \
        --emit=llvm-ir -o "$KASAN_IR" "$SRC" 2>&1

    echo
    echo "  User function attributes (expect frame-pointer=all):"
    grep -m3 '"frame-pointer"' "$KASAN_IR" 2>/dev/null | head -3 | sed 's/^/    /'

    echo
    echo "  asan.module_ctor definition:"
    grep -A3 'define.*asan.module_ctor' "$KASAN_IR" 2>/dev/null | sed 's/^/    /'

    echo
    echo "  Module flags:"
    grep 'frame-pointer' "$KASAN_IR" 2>/dev/null | grep -v 'attributes' | head -3 | sed 's/^/    /'
fi
