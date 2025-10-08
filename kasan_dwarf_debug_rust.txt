The UNWIND_PATCH_PAC_INTO_SCS kernel configuration option enables a dynamic code patching mechanism on ARM64 devices. This feature transparently converts functions using Pointer Authentication (PAC) for return address protection into functions that use Shadow Call Stack (SCS), another hardware-assisted security feature for protecting return addresses.

🛡️ Understanding PAC and SCS

To understand what UNWIND_PATCH_PAC_INTO_SCS does, you first need to know the two security features it connects:

· PAC (Pointer Authentication): Uses cryptographic signatures (paciasp/autiasp instruction pairs) to protect return addresses on the stack from corruption.
· SCS (Shadow Call Stack): Uses a separate, secure shadow stack (str x30, [x18], #8/ldr x30, [x18, #-8]! instruction pairs) to protect return addresses.

Some ARM64 systems support both features, but using them simultaneously on the same function is redundant and inefficient. The UNWIND_PATCH_PAC_INTO_SCS feature solves this by dynamically patching the kernel code at boot time.

⚙️ How The Patching Mechanism Works

When enabled, UNWIND_PATCH_PAC_INTO_SCS performs a dynamic transformation of the kernel's executable code during boot:

1. Scans for PAC Instructions: The kernel analyzes the DWARF debug metadata to locate all functions protected by PAC.
2. Patches Code in Memory: It replaces the PAC instruction sequences with SCS sequences. Here's a concrete example of what this patching does:

Let's say a function in the source code is compiled to use PAC, and its assembly looks like this before patching:

```asm
example_function:
    paciasp          // PAC instruction at entry
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    ...
    ldp x29, x30, [sp], #16
    autiasp          // PAC instruction before return
    ret
```

After UNWIND_PATCH_PAC_INTO_SCS runs, the same function in memory would be patched to use SCS instead:

```asm
example_function:
    str x30, [x18], #8   // SCS instruction at entry
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    ...
    ldp x29, x30, [sp], #16
    ldr x30, [x18, #-8]! // SCS instruction before return
    ret
```

🤔 Why This Patching Is Necessary

The primary purpose is to avoid redundancy and potential conflicts while maintaining security. Running both PAC and SCS on the same function is unnecessary overhead. This patching allows the kernel to standardize on one protection mechanism (SCS) where it is preferred or required, ensuring all code is consistently protected without the performance cost of two overlapping security schemes.

⚠️ Common Issues and Debugging

Problems can occur if the DWARF metadata used to locate PAC instructions is inaccurate. If the metadata is wrong, the patching might create inconsistent function prologues and epilogues, leading to kernel crashes.

For instance, you might find a function that has been partially patched, leading to a dangerous mix of both techniques:

```asm
// Mixed prologue/epilogue - CAN CAUSE CRASH
inconsistent_function:
    str x30, [x18], #8   // SCS prologue
    ...
    autiasp              // PAC epilogue - MISMATCH!
    ret
```

This is the type of issue you encountered with your KASAN-enabled kernel. Debugging such issues involves checking the DWARF metadata with tools like llvm-readelf --unwind to verify the unwind information correctly identifies the locations of all PAC instructions.