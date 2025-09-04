## **Step-by-Step Debugging CTORS with Trace32**

### **1. Load Kernel ELF and Symbols**
```plaintext
Data.LOAD.Elf <path-to-vmlinux> /NoCODE
```
- This loads all symbols so you can use names for functions and arrays.

### **2. Locate ctors Array in Memory**
```plaintext
MAP.List /Name=_ctors*
```
- Identify `_ctors_start` and `_ctors_end` symbols.

### **3. List ctors Pointers**
```plaintext
Data.List &_ctors_start &_ctors_end /Format:Pointer
```
- Shows all addresses of functions registered as constructors.

### **4. Set Breakpoint on do_ctors Function**
```plaintext
Break.Set do_ctors
Go
```
- This halts at the entry of the constructor handler.

### **5. Step to the Function Pointer Call**
- Use `Step` or `Step.Line` to reach the instruction that calls each constructor pointer.
- Identify the actual function call in `do_ctors` (should be something like `BLR xN`).

### **6. Set Breakpoint on the Call Instruction**
- When you reach the constructor pointer call:
```plaintext
Break.Set <address-of-call-instruction>
```
- Or set a breakpoint on all branch/call instructions for broad coverage:
```plaintext
Break.Set /Branch
```

### **7. (Optional) Set Conditional Breakpoint for Bad Pointers**
- You can break only if a pointer is NULL or below kernel space:
```plaintext
Break.Set <address-of-call-instruction> /Condition fn == 0
Break.Set <address-of-call-instruction> /Condition fn < PAGE_OFFSET
```

### **8. Inspect Pointer Before Execution**
- At each hit, check the function pointer:
```plaintext
Data.dump fn
Register.View
```
- Optionally, resolve to a function name:
```plaintext
MAP.Resolve fn
```

### **9. Automate Pointer Name Listing**
- Loop over ctors array to resolve all entries:
```plaintext
for.var ptr = &_ctors_start to &_ctors_end step 8()
    MAP.Resolve [ptr]
enddo
```
- This prints the name of every constructor.

### **10. Follow Any Crash or Exception**
- If a NULL pointer dereference happens (as shown in your log), inspect address and stack at exception:
```plaintext
Frame.view
Register.View
```

***

## **Summary Table**

| Step  | Goal                        | Trace32 Command / Action                            |
|-------|-----------------------------|-----------------------------------------------------|
| 1     | Load symbols                | `Data.LOAD.Elf <vmlinux> /NoCODE`                  |
| 2     | Find ctors array            | `MAP.List /Name=_ctors*`                            |
| 3     | Raw list ctors pointers     | `Data.List &_ctors_start &_ctors_end /Format:Pointer`|
| 4     | Break on entry              | `Break.Set do_ctors`                                |
| 5-6   | Step to call & set bp       | `Step`, `Break.Set <addr>` or `Break.Set /Branch`   |
| 7     | Conditional bp for NULL     | `Break.Set <addr> /Condition fn==0`                 |
| 8     | Inspect pointer/name        | `Data.dump fn`, `MAP.Resolve fn`, `Register.View`   |
| 9     | Automate listing            | Loop+`MAP.Resolve [ptr]`                            |
| 10    | Examine after exception     | `Frame.view`, `Register.View`                       |

***

**Follow these steps sequentially to catch bad constructor pointers and investigate exactly which pointer or call corrupted memory, caused a NULL dereference, or triggered a crash[1].**

Citations:
[1] selected_image_6065279594276162566.jpg https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/images/62800069/f73dd2c3-9332-4687-bd82-f723963cbc7e/selected_image_6065279594276162566.jpg
