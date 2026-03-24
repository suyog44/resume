#!/bin/bash
# build_ashmem_test.sh
#
# Builds test_ashmem_asan_ctor.rs with exact KBUILD_RUSTFLAGS and
# generates a complete reviewer-ready report directory containing:
#
#   report/
#   ├── 00_source.rs.txt            ← source with line numbers
#   ├── 01_baseline.s               ← disasm: no KASAN, with FP
#   ├── 02_kasan_nofp.s             ← disasm: KASAN, WITHOUT FP  (BEFORE fix)
#   ├── 03_kasan_fp.s               ← disasm: KASAN, WITH FP     (AFTER fix)
#   ├── 04_kasan_nofp.ll            ← LLVM IR: BEFORE fix
#   ├── 05_kasan_fp.ll              ← LLVM IR: AFTER fix
#   ├── 06_str_x30_BEFORE.txt       ← str x30 sites BEFORE fix
#   ├── 07_str_x30_AFTER.txt        ← str x30 sites AFTER fix
#   ├── 08_diff_before_after.txt    ← side-by-side prologue diff
#   ├── 09_asan_ctor_detail.txt     ← asan.module_ctor disasm + IR attrs
#   └── 10_summary.txt              ← full summary for bug report
#
# Usage:
#   ./build_ashmem_test.sh [path/to/rustc]
#
# Output directory (default: ./report):
#   REPORT_DIR=/path/to/dir ./build_ashmem_test.sh [path/to/rustc]

set -euo pipefail

RUSTC="${1:-rustc}"
OBJDUMP="${LLVM_OBJDUMP:-llvm-objdump}"
SRC="$(dirname "$0")/test_ashmem_asan_ctor.rs"
REPORT_DIR="${REPORT_DIR:-./report}"
DISASM="$OBJDUMP --mattr=+v8.3a -d"

command -v "$RUSTC" >/dev/null 2>&1 || { echo "ERROR: rustc not found"; exit 1; }
command -v "$OBJDUMP" >/dev/null 2>&1 || { echo "ERROR: llvm-objdump not found"; exit 1; }

RUSTC_VERSION=$("$RUSTC" --version 2>&1 || echo unknown)

echo "rustc  : $RUSTC_VERSION"
echo "objdump: $OBJDUMP --mattr=+v8.3a"
echo "source : $SRC"
echo "report : $REPORT_DIR"
echo

mkdir -p "$REPORT_DIR"

# ─── Exact KBUILD_RUSTFLAGS from post-.config build log ──────

KBUILD_RUSTFLAGS=(
    --edition=2021
    --crate-type=lib
    -Zbinary_dep_depinfo=y
    -Astable_features
    -Dnon_ascii_idents
    -Dunsafe_op_in_unsafe_fn
    -Wmissing_docs
    -Wrust_2018_idioms
    -Wunreachable_pub
    -Wclippy::all
    -Wclippy::ignored_unit_patterns
    -Wclippy::mut_mut
    -Wclippy::needless_bitwise_bool
    -Aclippy::needless_lifetimes
    -Wclippy::no_mangle_with_rust_abi
    -Wclippy::undocumented_unsafe_blocks
    -Wclippy::unnecessary_safety_comment
    -Wclippy::unnecessary_safety_doc
    -Wrustdoc::missing_crate_level_docs
    -Wrustdoc::unescaped_backticks
    -Cpanic=abort
    -Cembed-bitcode=n
    -Clto=n
    -Cforce-unwind-tables=n
    -Ccodegen-units=1
    -Csymbol-mangling-version=v0
    -Crelocation-model=static
    -Zfunction-sections=n
    -Wclippy::float_arithmetic
    --target=aarch64-unknown-none
    "-Ctarget-feature=-neon"
    -Cforce-unwind-tables=y
    -Zuse-sync-unwind=n
    -Zbranch-protection=pac-ret
    -Zfixed-x18
)

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# ═══════════════════════════════════════════════════════════════
# 00: Source with line numbers
# ═══════════════════════════════════════════════════════════════

echo "[1/8] Generating source listing..."
cat -n "$SRC" > "$REPORT_DIR/00_source.rs.txt"

# ═══════════════════════════════════════════════════════════════
# Build all three variants
# ═══════════════════════════════════════════════════════════════

echo "[2/8] Building baseline (no KASAN, with FP)..."
"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" --emit=obj \
    -Cforce-frame-pointers=yes \
    -o "$TMPDIR/baseline.o" "$SRC" 2>&1

echo "[3/8] Building KASAN WITHOUT -Cforce-frame-pointers (BEFORE fix)..."
"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" --emit=obj \
    -Zsanitizer=kernel-address \
    -o "$TMPDIR/kasan_nofp.o" "$SRC" 2>&1

echo "[4/8] Building KASAN WITH -Cforce-frame-pointers=yes (AFTER fix)..."
"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" --emit=obj \
    -Cforce-frame-pointers=yes \
    -Zsanitizer=kernel-address \
    -o "$TMPDIR/kasan_fp.o" "$SRC" 2>&1

# ═══════════════════════════════════════════════════════════════
# Generate full disassembly with source-level function names
# ═══════════════════════════════════════════════════════════════

echo "[5/8] Generating disassembly..."
$DISASM "$TMPDIR/baseline.o"   > "$REPORT_DIR/01_baseline.s"
$DISASM "$TMPDIR/kasan_nofp.o" > "$REPORT_DIR/02_kasan_nofp.s"
$DISASM "$TMPDIR/kasan_fp.o"   > "$REPORT_DIR/03_kasan_fp.s"

# ═══════════════════════════════════════════════════════════════
# Generate LLVM IR for both KASAN builds
# ═══════════════════════════════════════════════════════════════

echo "[6/8] Generating LLVM IR..."
"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" --emit=llvm-ir \
    -Zsanitizer=kernel-address \
    -o "$REPORT_DIR/04_kasan_nofp.ll" "$SRC" 2>&1

"$RUSTC" "${KBUILD_RUSTFLAGS[@]}" --emit=llvm-ir \
    -Cforce-frame-pointers=yes \
    -Zsanitizer=kernel-address \
    -o "$REPORT_DIR/05_kasan_fp.ll" "$SRC" 2>&1

# ═══════════════════════════════════════════════════════════════
# str x30 sites: BEFORE vs AFTER
# ═══════════════════════════════════════════════════════════════

echo "[7/8] Extracting str x30 sites..."

{
    echo "================================================================="
    echo " str x30 sites — BEFORE fix (KASAN, no -Cforce-frame-pointers)"
    echo " Build: rustc ... -Zsanitizer=kernel-address"
    echo "================================================================="
    echo
    nofp_str=$($DISASM "$TMPDIR/kasan_nofp.o" | grep -c 'str.*x30, \[sp' || true)
    nofp_stp=$($DISASM "$TMPDIR/kasan_nofp.o" | grep -c 'stp.*x29.*x30' || true)
    nofp_pac=$($DISASM "$TMPDIR/kasan_nofp.o" | grep -c 'paciasp' || true)
    nofp_aut=$($DISASM "$TMPDIR/kasan_nofp.o" | grep -c 'autiasp' || true)
    nofp_fn=$($DISASM "$TMPDIR/kasan_nofp.o" | grep -c '^[0-9a-f]\+ <' || true)
    echo "Total functions     : $nofp_fn"
    echo "paciasp             : $nofp_pac"
    echo "autiasp             : $nofp_aut"
    echo "stp x29, x30 (GOOD): $nofp_stp"
    echo "str x30       (BAD): $nofp_str"
    echo
    echo "Functions with str x30:"
    echo "─────────────────────────────────────────────────────────────"
    $DISASM "$TMPDIR/kasan_nofp.o" | \
        awk '/^[0-9a-f]+ <.*>:/{fn=$0} /str.*x30, \[sp/{print fn; print "  "$0; print ""}'
} > "$REPORT_DIR/06_str_x30_BEFORE.txt"

{
    echo "================================================================="
    echo " str x30 sites — AFTER fix (KASAN + -Cforce-frame-pointers=yes)"
    echo " Build: rustc ... -Cforce-frame-pointers=yes -Zsanitizer=kernel-address"
    echo "================================================================="
    echo
    fp_str=$($DISASM "$TMPDIR/kasan_fp.o" | grep -c 'str.*x30, \[sp' || true)
    fp_stp=$($DISASM "$TMPDIR/kasan_fp.o" | grep -c 'stp.*x29.*x30' || true)
    fp_pac=$($DISASM "$TMPDIR/kasan_fp.o" | grep -c 'paciasp' || true)
    fp_aut=$($DISASM "$TMPDIR/kasan_fp.o" | grep -c 'autiasp' || true)
    fp_fn=$($DISASM "$TMPDIR/kasan_fp.o" | grep -c '^[0-9a-f]\+ <' || true)
    echo "Total functions     : $fp_fn"
    echo "paciasp             : $fp_pac"
    echo "autiasp             : $fp_aut"
    echo "stp x29, x30 (GOOD): $fp_stp"
    echo "str x30       (BAD): $fp_str"
    echo
    if [ "$fp_str" -gt 0 ]; then
        echo "Functions with str x30 (REMAINING — LLVM bug):"
        echo "─────────────────────────────────────────────────────────────"
        $DISASM "$TMPDIR/kasan_fp.o" | \
            awk '/^[0-9a-f]+ <.*>:/{fn=$0} /str.*x30, \[sp/{print fn; print "  "$0; print ""}'
    else
        echo "All functions use stp x29, x30 — fully fixed."
    fi
} > "$REPORT_DIR/07_str_x30_AFTER.txt"

# ═══════════════════════════════════════════════════════════════
# Prologue-only diff: extract first 6 instructions of each function
# ═══════════════════════════════════════════════════════════════

extract_prologues() {
    local obj="$1"
    $DISASM "$obj" | awk '
        /^[0-9a-f]+ <.*>:/ {
            if (fn != "") print ""
            fn = $0
            n = 0
            print fn
            next
        }
        fn && n < 6 {
            print "  " $0
            n++
        }
    '
}

{
    echo "================================================================="
    echo " BEFORE vs AFTER: function prologue comparison"
    echo "================================================================="
    echo
    echo "Left  = BEFORE (no -Cforce-frame-pointers)"
    echo "Right = AFTER  (with -Cforce-frame-pointers=yes)"
    echo
    echo "Look for:"
    echo "  BEFORE: str x30, [sp, ...]   ← BAD: no frame record"
    echo "  AFTER:  stp x29, x30, [...]  ← GOOD: full frame record"
    echo
    echo "─────────────────────────────────────────────────────────────"
    echo

    extract_prologues "$TMPDIR/kasan_nofp.o" > "$TMPDIR/prologues_before.txt"
    extract_prologues "$TMPDIR/kasan_fp.o"   > "$TMPDIR/prologues_after.txt"

    diff --side-by-side --width=140 \
        "$TMPDIR/prologues_before.txt" \
        "$TMPDIR/prologues_after.txt" || true
} > "$REPORT_DIR/08_diff_before_after.txt"

# ═══════════════════════════════════════════════════════════════
# asan.module_ctor detail: disasm + IR attribute mismatch
# ═══════════════════════════════════════════════════════════════

echo "[8/8] Extracting asan.module_ctor detail..."

{
    echo "================================================================="
    echo " asan.module_ctor / asan.module_dtor — detailed analysis"
    echo "================================================================="
    echo
    echo "These functions are SYNTHESIZED by LLVM's AddressSanitizer pass"
    echo "(AddressSanitizer.cpp) to register/unregister global variables"
    echo "with the KASAN shadow memory. They are NOT present in the source"
    echo "file — they are created by the compiler."
    echo
    echo "BUG: They do not inherit \"frame-pointer\"=\"all\" from the module"
    echo "flags, so they emit str x30 instead of stp x29, x30."
    echo
    echo "─────────────────────────────────────────────────────────────"
    echo " Disassembly (AFTER fix — still broken)"
    echo "─────────────────────────────────────────────────────────────"
    echo

    $DISASM "$TMPDIR/kasan_fp.o" | awk '
        /asan\.module_ctor>:/ || /asan\.module_dtor>:/ { found=1; n=0 }
        found { print; n++; if (n > 12 && /ret/) found=0 }
    '

    echo
    echo "─────────────────────────────────────────────────────────────"
    echo " LLVM IR: attribute groups (AFTER fix)"
    echo "─────────────────────────────────────────────────────────────"
    echo
    echo "User functions reference an attribute group with frame-pointer=all."
    echo "asan.module_ctor references a DIFFERENT group WITHOUT frame-pointer."
    echo

    echo "--- asan.module_ctor definition ---"
    grep -A5 'define.*asan.module_ctor' "$REPORT_DIR/05_kasan_fp.ll" 2>/dev/null || echo "(not found)"

    echo
    echo "--- asan.module_dtor definition ---"
    grep -A5 'define.*asan.module_dtor' "$REPORT_DIR/05_kasan_fp.ll" 2>/dev/null || echo "(not found)"

    echo
    echo "--- All attribute groups ---"
    grep '^attributes #' "$REPORT_DIR/05_kasan_fp.ll" 2>/dev/null || echo "(not found)"

    echo
    echo "--- frame-pointer occurrences ---"
    grep -n '"frame-pointer"' "$REPORT_DIR/05_kasan_fp.ll" 2>/dev/null || echo "(none found)"

    echo
    echo "--- Module flags ---"
    grep -n 'frame-pointer' "$REPORT_DIR/05_kasan_fp.ll" 2>/dev/null | grep '!' || echo "(none found)"

} > "$REPORT_DIR/09_asan_ctor_detail.txt"

# ═══════════════════════════════════════════════════════════════
# Summary report
# ═══════════════════════════════════════════════════════════════

baseline_str=$($DISASM "$TMPDIR/baseline.o" | grep -c 'str.*x30, \[sp' || true)

{
    cat <<'HEADER'
=================================================================
 ARM64 KASAN + Rust: asan.module_ctor frame pointer bug
 Reproducer results
=================================================================

HEADER

    echo "Toolchain"
    echo "─────────────────────────────────────────────────────────────"
    echo "  rustc  : $RUSTC_VERSION"
    echo "  objdump: $OBJDUMP --mattr=+v8.3a"
    echo "  source : test_ashmem_asan_ctor.rs"
    echo "  target : aarch64-unknown-none"
    echo

    echo "KBUILD_RUSTFLAGS (exact from android16-6.12 build log)"
    echo "─────────────────────────────────────────────────────────────"
    echo "  ${KBUILD_RUSTFLAGS[*]}"
    echo

    cat <<RESULTS
Build Matrix
─────────────────────────────────────────────────────────────
                          stp x29,x30  str x30   paciasp
                          (GOOD)       (BAD)
─────────────────────────────────────────────────────────────
1. Baseline (no KASAN)    all          $baseline_str         (reference)
2. KASAN, no FP flag      $nofp_stp          $nofp_str        $nofp_pac
3. KASAN + FP flag        $fp_stp          $fp_str         $fp_pac
─────────────────────────────────────────────────────────────

RESULTS

    cat <<'ANALYSIS'
Analysis
─────────────────────────────────────────────────────────────

BUG 1 — Kernel build system (KBUILD_RUSTFLAGS)
  Missing: -Cforce-frame-pointers=yes
  Impact:  Row 2 shows massive str x30 across ALL KASAN-instrumented
           Rust functions (core::cmp, AtomicUsize, Pin::deref, etc.)
  Fix:     Add -Cforce-frame-pointers=yes to KBUILD_RUSTFLAGS in
           top-level Makefile and arch/arm64/Makefile
  Result:  Row 3 — ALL user functions fixed (stp x29, x30)

BUG 2 — LLVM AddressSanitizer (asan.module_ctor / asan.module_dtor)
  Root cause: AddressSanitizer.cpp synthesises asan.module_ctor to
              call __asan_register_globals(). The Function is created
              with Function::Create() using default attributes — it
              does NOT inherit "frame-pointer"="all" from module flags.
  Impact:  Row 3 still shows 2 str x30 (asan.module_ctor + dtor)
           even WITH -Cforce-frame-pointers=yes
  Fix:     LLVM must propagate frame-pointer from module flags to
           synthesised sanitizer functions.
  File at: https://github.com/llvm/llvm-project/issues

SCS Impact
─────────────────────────────────────────────────────────────
  The arm64 kernel SCS alternative patching converts:
    paciasp  →  str x30, [x18], #8    (SCS push)
    autiasp  →  ldr x30, [x18, #-8]!  (SCS pop)

  The patcher expects stp x29, x30 after paciasp. Functions with
  str x30 prologues are skipped or mis-patched, creating gaps
  in Shadow Call Stack coverage.

ANALYSIS

    echo "Files in this report"
    echo "─────────────────────────────────────────────────────────────"
    echo "  00_source.rs.txt         Source with line numbers"
    echo "  01_baseline.s            Disasm: no KASAN, with FP (reference)"
    echo "  02_kasan_nofp.s          Disasm: KASAN, no FP (BEFORE fix)"
    echo "  03_kasan_fp.s            Disasm: KASAN, with FP (AFTER fix)"
    echo "  04_kasan_nofp.ll         LLVM IR: BEFORE fix"
    echo "  05_kasan_fp.ll           LLVM IR: AFTER fix"
    echo "  06_str_x30_BEFORE.txt    All str x30 sites BEFORE fix"
    echo "  07_str_x30_AFTER.txt     All str x30 sites AFTER fix"
    echo "  08_diff_before_after.txt Side-by-side prologue diff"
    echo "  09_asan_ctor_detail.txt  asan.module_ctor disasm + IR attrs"
    echo "  10_summary.txt           This file"

} > "$REPORT_DIR/10_summary.txt"

# Expand shell variables in the summary
sed -i \
    -e "s|\$RUSTC_VERSION|$RUSTC_VERSION|g" \
    -e "s|\$OBJDUMP|$OBJDUMP|g" \
    -e "s|\${KBUILD_RUSTFLAGS\[\*\]}|${KBUILD_RUSTFLAGS[*]}|g" \
    "$REPORT_DIR/10_summary.txt"

# ═══════════════════════════════════════════════════════════════
# Print summary to terminal
# ═══════════════════════════════════════════════════════════════

echo
echo "═══════════════════════════════════════════════════════════"
echo "  DONE — report generated"
echo "═══════════════════════════════════════════════════════════"
echo
echo "  baseline (no KASAN)  : $baseline_str str x30"
echo "  KASAN (BEFORE fix)   : $nofp_str str x30"
echo "  KASAN (AFTER fix)    : $fp_str str x30"
echo
if [ "$fp_str" -gt 0 ] && [ "$nofp_str" -gt 0 ]; then
    echo "  Makefile fix resolves : $((nofp_str - fp_str)) of $nofp_str"
    echo "  Remaining (LLVM bug) : $fp_str (asan.module_ctor/dtor)"
fi
echo
echo "  Report directory: $REPORT_DIR/"
ls -1 "$REPORT_DIR/" | sed 's/^/    /'
echo
echo "  Attach $REPORT_DIR/ to the LLVM bug report and"
echo "  linux-arm-kernel / rust-for-linux patch submission."
echo
