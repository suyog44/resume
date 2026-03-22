
```c
// test.c — KASAN frame-pointer regression reproducer
//
// Build:
//   clang-19 -S -O2 -target aarch64-linux-gnu \
//     -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
//     test.c -o no_kasan.s
//
//   clang-19 -S -O2 -target aarch64-linux-gnu \
//     -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
//     -fsanitize=kernel-address test.c -o with_kasan.s
//
// Check:
//   grep -n 'str.*x30' no_kasan.s    # expect: none
//   grep -n 'str.*x30' with_kasan.s  # BUG if present
//   grep -n 'stp.*x29.*x30' with_kasan.s

typedef unsigned long u64;

// Prevent LTO / inlining so each function gets its own prologue
#define noinline __attribute__((noinline, noipa))

volatile int sink;

// Case 1: Simple leaf with stack-local memory access
// KASAN must instrument the read/write to buf[].
// Sanitizer inserts redzone alloca — may confuse frame lowering
// into thinking x29 is unnecessary.
noinline
int leaf_stack_access(int idx)
{
    int buf[16];
    buf[idx] = 42;
    return buf[idx];
}

// Case 2: Leaf with a global volatile access
// Minimal stack usage — sanitizer adds shadow check for &sink,
// may keep function "simple enough" to skip frame record.
noinline
void leaf_global_write(int val)
{
    sink = val;
}

// Case 3: Non-leaf calling an opaque extern
// Has a real call, so x30 must be saved regardless.
// But sanitizer redzones around locals may alter how
// the backend decides to save x29.
extern int opaque(int *p);

noinline
int non_leaf_with_local(int x)
{
    int local = x + 1;
    return opaque(&local);
}

// Case 4: Small struct copy through pointer — triggers
// __asan_loadN / __asan_storeN for the struct-sized access.
// The widened shadow check changes the function's alloca layout.
struct small {
    u64 a;
    u64 b;
};

noinline
void struct_copy(struct small *dst, const struct small *src)
{
    *dst = *src;
}

// Case 5: Conditional memory access with early return.
// Sanitizer must instrument the branch-guarded access.
// Short-path vs long-path may cause the backend to emit
// different prologue styles per path if shrink-wrapping kicks in.
noinline
int conditional_access(int *p, int flag)
{
    if (!flag)
        return 0;
    return *p + 1;
}

// Case 6: alloca-like variable-length access pattern.
// Forces dynamic stack adjustment — the sanitizer's redzone
// insertion on top of this is the most likely trigger.
noinline
int vla_like(int n)
{
    int arr[n]; // VLA
    for (int i = 0; i < n; i++)
        arr[i] = i;
    return arr[n - 1];
}

// Driver to prevent dead-code elimination
int main(void)
{
    struct small a = {1, 2}, b;
    int x = 0;

    x += leaf_stack_access(3);
    leaf_global_write(x);
    x += non_leaf_with_local(x);
    struct_copy(&b, &a);
    x += conditional_access(&x, 1);
    x += vla_like(8);

    return x;
}
```

## What to Check

```bash
# Compile both versions
clang-19 -S -O2 -target aarch64-linux-gnu \
  -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
  test.c -o no_kasan.s

clang-19 -S -O2 -target aarch64-linux-gnu \
  -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
  -fsanitize=kernel-address test.c -o with_kasan.s

# Quick diff — shows which functions regressed
echo "=== no_kasan.s ==="
grep -B1 'str.*x30\|stp.*x29.*x30' no_kasan.s

echo "=== with_kasan.s ==="
grep -B1 'str.*x30\|stp.*x29.*x30' with_kasan.s

# Confirm IR attribute survived the sanitizer pass
clang-19 -S -emit-llvm -O2 -target aarch64-linux-gnu \
  -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
  -fsanitize=kernel-address test.c -o - | \
  grep 'frame-pointer'
```
```
$ grep -B1 'str.*x30\|stp.*x29.*x30' no_kasan.s
// %bb.0:
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
// %bb.0:
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
        .cfi_def_cfa_offset 32
        stp     x29, x30, [sp, #16]             // 16-byte Folded Spill
--
// %bb.0:
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
// %bb.0:
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
// %bb.0:
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
        .cfi_def_cfa_offset 48
        stp     x29, x30, [sp, #16]             // 16-byte Folded Spill
$ grep -B1 'str.*x30\|stp.*x29.*x30' with_kasan.s
// %bb.0:                               // %entry
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
// %bb.0:                               // %entry
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
// %bb.0:                               // %entry
        stp     x29, x30, [sp, #-32]!           // 16-byte Folded Spill
--
// %bb.0:                               // %entry
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
        .cfi_def_cfa_offset 32
        stp     x29, x30, [sp, #16]             // 16-byte Folded Spill
--
// %bb.0:                               // %entry
        stp     x29, x30, [sp, #-48]!           // 16-byte Folded Spill
--
        .cfi_def_cfa_offset 48
        stp     x29, x30, [sp, #16]             // 16-byte Folded Spill
--
// %bb.0:
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill
--
// %bb.0:
        stp     x29, x30, [sp, #-16]!           // 16-byte Folded Spill

```

```
attributes #0 = { mustprogress nofree noinline norecurse nosync nounwind sanitize_address willreturn memory(none) uwtable "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #2 = { nofree noinline norecurse nounwind sanitize_address memory(readwrite, argmem: none) uwtable "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #3 = { noinline nounwind sanitize_address uwtable "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #4 = { "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #5 = { mustprogress nofree noinline norecurse nosync nounwind sanitize_address willreturn memory(argmem: readwrite) uwtable "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #6 = { mustprogress nofree noinline norecurse nosync nounwind sanitize_address willreturn memory(argmem: read) uwtable "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #7 = { nofree noinline norecurse nosync nounwind sanitize_address memory(none) uwtable "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #8 = { nounwind sanitize_address uwtable "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
attributes #9 = { nounwind uwtable "frame-pointer"="all" "target-cpu"="generic" "target-features"="+fp-armv8,+neon,+outline-atomics,+v8a,-fmv" }
!4 = !{i32 7, !"frame-pointer", i32 2}

```
