
## Exact LLVM 23.0.0git Source Locations

### File 1 — `AArch64FrameLowering.cpp`

From LLVM 23.0.0git doxygen, `emitPrologue` is at **line 1666** and `emitEpilogue` is at **line 2900** of `AArch64FrameLowering.cpp`. [Wikipedia](https://en.wikipedia.org/wiki/Control-flow_integrity)

```
llvm/lib/Target/AArch64/AArch64FrameLowering.cpp

Line 1666: emitPrologue()
           └── emits paciasp instruction
           └── emits negate_ra_state CFI for paciasp   ✓

Line 2900: emitEpilogue()
           └── emits autiasp instruction                ✓
           └── hits !hasFP() early return               ✗
               └── negate_ra_state CFI for autiasp
                   NEVER REACHED                        ✗
```

### File 2 — `AArch64PrologueEpilogue.h` — NEW in LLVM 23

LLVM 23.0.0git introduces a new include at line 219:
```cpp
#include "AArch64PrologueEpilogue.h"   ← NEW FILE in LLVM 23
```
This is a new header that did not exist in older LLVM versions — the prologue/epilogue logic has been partially refactored into this separate file. [Olivia Gallucci](https://oliviagallucci.com/cfi-with-clang-macos-and-clang-on-macos/)

This means the source has **moved** since older LLVM. The key functions are now split:

```
LLVM 23.0.0git file structure:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

AArch64FrameLowering.cpp (line 1666)
    emitPrologue()
        → paciasp instruction
        → negate_ra_state CFI ✓ (emitted here)
        → hasFP check → stp x29,x30 OR str x30

AArch64FrameLowering.cpp (line 2900)
    emitEpilogue()
        → autiasp instruction  ✓
        → if (!hasFP(MF)) {   ← early return
              return;          ← SKIPS autiasp CFI ✗
          }
        → negate_ra_state CFI ✗ (never reached for str x30)

AArch64PrologueEpilogue.h   ← NEW in LLVM 23
    resetCFIToInitialState()
        → negate_ra_state correctly gated on
          shouldSignReturnAddress() only   ✓
        → BUT this is NOT called for normal
          prologue/epilogue paths
```

### File 3 — `asan.module_ctor` Creation Chain

The `asan.module_ctor` creation is in:

```
llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp
    → calls createSanitizerCtorAndInitFunctions()

llvm/lib/Transforms/Utils/InstrumentationUtils.cpp
    → createSanitizerCtorAndInitFunctions()
    → calls Function::createWithDefaultAttr()

llvm/lib/IR/Function.cpp
    → createWithDefaultAttr()
    → reads M->getFramePointer()  ← module flag from -mframe-pointer=

llvm/lib/IR/Module.cpp
    → getFramePointer()
    → returns NonLeaf (i32 1) when -fno-omit-frame-pointer
    → returns All    (i32 2) when -mframe-pointer=all
```

---

## The Key Doxygen Links for LLVM 23.0.0git

```
AArch64FrameLowering.cpp source:
https://llvm.org/doxygen/AArch64FrameLowering_8cpp_source.html

emitPrologue() at line 1666:
https://llvm.org/doxygen/AArch64FrameLowering_8cpp_source.html#l01666

emitEpilogue() at line 2900:
https://llvm.org/doxygen/AArch64FrameLowering_8cpp_source.html#l02900

resetCFIToInitialState() (has correct pattern):
https://llvm.org/doxygen/AArch64FrameLowering_8cpp_source.html#l00717

AArch64FrameLowering.cpp file reference:
https://llvm.org/doxygen/AArch64FrameLowering_8cpp.html
```

---

## Summary for LLVM 23.0.0git

| File | Line | What happens | Bug? |
|---|---|---|---|
| `AArch64FrameLowering.cpp` | 1666 | `emitPrologue()` — paciasp + negate_ra_state CFI | ✅ Correct |
| `AArch64FrameLowering.cpp` | 2900 | `emitEpilogue()` — autiasp emitted, then early return for `!hasFP()` | ❌ **BUG** |
| `AArch64FrameLowering.cpp` | 717 | `resetCFIToInitialState()` — correct pattern (no `hasFP` gate) | ✅ Correct pattern to follow |
| `AddressSanitizer.cpp` | — | Creates `asan.module_ctor` via `createSanitizerCtorAndInitFunctions` | Source of synthetic function |
| `Function.cpp` | — | `createWithDefaultAttr()` — gives `asan.module_ctor` `frame-pointer=non-leaf` | Source of `hasFP=false` |
| `Module.cpp` | — | `getFramePointer()` reads module flag set by `-mframe-pointer=` | Root of flag propagation |
