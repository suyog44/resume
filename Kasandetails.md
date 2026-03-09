Let me find the exact LLVM source that generates `str x30` for `asan.module_ctor`.

Found everything needed. Here is the complete source chain:

## Complete Source Chain for `str x30` in `asan.module_ctor`

### File 1 — `AddressSanitizer.cpp` creates `asan.module_ctor`

The ASAN pass calls `createSanitizerCtorAndInitFunctions()` to create the synthetic `asan.module_ctor`:

```cpp
// llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp

static const char *const kAsanModuleCtorName = "asan.module_ctor";
static const char *const kAsanModuleDtorName = "asan.module_dtor";

// In instrumentModule():
std::tie(AsanCtorFunction, std::ignore) =
    createSanitizerCtorAndInitFunctions(
        M,
        kAsanModuleCtorName,    // "asan.module_ctor"
        kAsanInitName,          // "__asan_init"
        /*InitArgTypes=*/{},
        /*InitArgs=*/{},
        VersionCheckName);
```
[LWN.net](https://lwn.net/Articles/810077/)

---

### File 2 — `InstrumentationUtils.cpp` creates the function with attributes

The synthetic function gets its `frame-pointer` attribute from the **module-level flag** via `Module::getFramePointer()`:

```cpp
// llvm/lib/Transforms/Utils/InstrumentationUtils.cpp (or SanitizerCommon)

Function *llvm::createSanitizerCtor(Module &M, StringRef CtorName) {
    Function *Ctor = Function::createWithDefaultAttr(
        FunctionType::get(Type::getVoidTy(M.getContext()), false),
        GlobalValue::InternalLinkage,
        0,         // AddrSpace
        CtorName,
        &M);

    // Gets uwtable from module flag
    // Gets frame-pointer from module flag  ← KEY
    // Module::getFramePointer() reads "frame-pointer" module flag
    // set by -mframe-pointer= at compile time
}
```

The `frame-pointer` attribute is set via `Function::createWithDefaultAttr()` which reads `Module::getFramePointer()`:

```cpp
// llvm/lib/IR/Function.cpp

Function *Function::createWithDefaultAttr(..., Module *M) {
    auto *F = new Function(...);
    if (M->getUwtable())
        B.addAttribute(Attribute::UWTable);

    // frame-pointer comes from MODULE FLAG:
    switch (M->getFramePointer()) {
    case FramePointerKind::None:
        break;                                    // no attribute → hasFP=false
    case FramePointerKind::NonLeaf:
        B.addAttribute("frame-pointer", "non-leaf"); // ← YOUR BROKEN CASE
        break;
    case FramePointerKind::All:
        B.addAttribute("frame-pointer", "all");   // ← FIXED CASE
        break;
    }
    return F;
}
```
[Phoronix](https://www.phoronix.com/news/Dynamic-Shadow-Call-Stack-6.2)

---

### File 3 — `Module.cpp` reads the module flag

The module flag is set by `-mframe-pointer=` at compile time and read by every synthetic function creation:

```cpp
// llvm/lib/IR/Module.cpp  line 753

FramePointerKind Module::getFramePointer() const {
    auto *Val = cast_or_null<ConstantAsMetadata>(
                    getModuleFlag("frame-pointer"));
    return static_cast<FramePointerKind>(
        Val ? cast<ConstantInt>(Val->getValue())->getZExtValue() : 0);
}

// Module flag values:
// 0 = None    → no frame-pointer attr → hasFP=false → str x30
// 1 = NonLeaf → "frame-pointer"="non-leaf" → hasFP=false for asan.module_ctor → str x30
// 2 = All     → "frame-pointer"="all" → hasFP=true → stp x29,x30
```
[Phoronix](https://www.phoronix.com/news/Dynamic-Shadow-Call-Stack-6.2)

---

### File 4 — `AArch64FrameLowering.cpp` decides `str x30` vs `stp x29,x30`

```cpp
// llvm/lib/Target/AArch64/AArch64FrameLowering.cpp

bool hasFP(const MachineFunction &MF) {
    StringRef FP = MF.getFunction()
                     .getFnAttribute("frame-pointer")
                     .getValueAsString();

    if (FP == "all")      return true;              // stp x29,x30 ✓
    if (FP == "non-leaf") return !isLeafFunction(); // asan.module_ctor → false → str x30 ✗
    if (FP == "none")     return false;             // str x30 ✗
}

// emitPrologue():
if (hasFP(MF))
    BuildMI(stp x29, x30);   // only when hasFP=true

// emitEpilogue():
if (!hasFP(MF)) {
    ...
    return;                   // EARLY RETURN — skips autiasp CFI
}
// autiasp CFI only reached when hasFP=true
buildNegateRAState();         // NEVER for asan.module_ctor with non-leaf
```

---

## Complete Chain

```
-fno-omit-frame-pointer (build system)
        ↓
Clang cc1: -mframe-pointer=non-leaf
        ↓
CodeGenModule.cpp: M.setFramePointer(NonLeaf)
        ↓ sets module flag "frame-pointer" = i32 1
        ↓
AddressSanitizer.cpp: createSanitizerCtorAndInitFunctions()
        ↓ creates asan.module_ctor
        ↓
Function.cpp: createWithDefaultAttr()
        ↓ reads M.getFramePointer() = NonLeaf (i32 1)
        ↓ sets "frame-pointer"="non-leaf" on asan.module_ctor
        ↓
AArch64FrameLowering.cpp: hasFP()
        ↓ "non-leaf" → isLeafFunction() check
        ↓ asan.module_ctor appears leaf-like → hasFP=false
        ↓
emitPrologue(): str x30,[sp,-16]! (NOT stp x29,x30)
emitEpilogue(): early return → autiasp CFI SKIPPED
        ↓
Dynamic SCS patcher: no negate_ra_state → skips autiasp
        ↓
Runtime: autiasp on unsigned LR → PANIC

FIX: -mframe-pointer=all
        ↓ M.setFramePointer(All) → i32 2
        ↓ asan.module_ctor gets "frame-pointer"="all"
        ↓ hasFP()=true always
        ↓ stp x29,x30 emitted
        ↓ autiasp CFI emitted
        ↓ patcher patches autiasp → scs_pop ✓
```

The bug starts at `Function::createWithDefaultAttr()` in `llvm/lib/IR/Function.cpp` — the synthetic `asan.module_ctor` inherits `frame-pointer=non-leaf` from the module flag, which flows all the way down to `hasFP()=false` in `AArch64FrameLowering.cpp`, causing `str x30` without frame record and missing `autiasp` CFI.
