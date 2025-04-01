Here’s how you can **intentionally create a null pointer dereference** in a kernel driver and use **ftrace** to debug the issue.

---

## **Step 1: Writing a Driver with a Null Pointer Dereference**
Create a simple **character driver** that triggers a **NULL pointer dereference**.

### **`null_deref.c`**
```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>

#define DEVICE_NAME "nulldev"

static struct cdev my_cdev;
static struct class *my_class;
static dev_t dev_num;

static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {
    char *ptr = NULL;  // NULL pointer
    printk(KERN_INFO "About to dereference NULL pointer\n");

    *ptr = 'A';  // This will cause a kernel oops

    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = my_read,
};

static int __init my_init(void) {
    alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    my_class = class_create(THIS_MODULE, DEVICE_NAME);
    device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, dev_num, 1);
    printk(KERN_INFO "Null dereference module loaded\n");
    return 0;
}

static void __exit my_exit(void) {
    cdev_del(&my_cdev);
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "Null dereference module unloaded\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kernel Debugging");
MODULE_DESCRIPTION("Module to demonstrate null dereferencing issue");
```

---

## **Step 2: Compile and Load the Module**
### **Compile**
```sh
make -C /lib/modules/$(uname -r)/build M=$PWD modules
```

### **Load the module**
```sh
sudo insmod null_deref.ko
```

### **Trigger the Issue**
```sh
cat /dev/nulldev
```
- This will **trigger a kernel OOPS** due to the NULL pointer dereference.
- Run `dmesg` to check the kernel log.

---

## **Step 3: Debugging the Crash Using ftrace**
### **A. Enable ftrace**
```sh
sudo mount -t debugfs none /sys/kernel/debug
cd /sys/kernel/debug/tracing
echo function > current_tracer
echo 1 > tracing_on
```

### **B. Check the Call Trace**
```sh
sudo cat /sys/kernel/debug/tracing/trace
```
Example output:
```
[  123.456789] Unable to handle kernel NULL pointer dereference at virtual address 00000000
[  123.456799] PC is at my_read+0x10/0x20 [null_deref]
[  123.456804] LR is at my_read+0x5/0x20 [null_deref]
[  123.456810] Call trace:
[  123.456820]  my_read+0x10/0x20 [null_deref]
[  123.456830]  __vfs_read+0x30/0x120
[  123.456840]  vfs_read+0x50/0x100
[  123.456850]  sys_read+0x40/0x80
```

---

## **Step 4: Analyzing the Call Trace**
### **Breakdown of the Call Trace**
```
[  123.456799] PC is at my_read+0x10/0x20 [null_deref]
```
- **PC (Program Counter)** shows where the crash happened.
- `my_read+0x10` means **offset 0x10 in `my_read` function** caused the issue.

### **Finding the Faulty Line**
1. **Check the function `my_read()`**
   ```c
   static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {
       char *ptr = NULL;  // NULL pointer
       printk(KERN_INFO "About to dereference NULL pointer\n");

       *ptr = 'A';  // This will cause a kernel oops
       return 0;
   }
   ```
2. **Using objdump to confirm the offset**
   ```sh
   objdump -d null_deref.ko | grep my_read
   ```
   - If it shows:
     ```
     0000000000000010:  MOVB [RAX], 0x41  # NULL dereference here
     ```
   - We confirm that `*ptr = 'A';` is at offset **0x10**, matching the ftrace output.

---

## **Step 5: Fix the Issue**
Modify `my_read()` to avoid dereferencing NULL:
```c
char buf[10] = "Hello";
char *ptr = buf;  // Valid pointer
*ptr = 'A';  // Safe operation
```
- Recompile and reload the module.

---

## **Summary**
1. **Create a kernel module with a NULL dereference bug.**
2. **Trigger the bug** by reading the `/dev/nulldev` device.
3. **Use ftrace** to trace function calls.
4. **Analyze the call trace** to find the faulty line.
5. **Use objdump** to verify the exact instruction.
6. **Fix the bug** by ensuring valid pointer usage.

**Kprobes/Kretprobes vs. Ftrace** – When to Use Which?  

Both **Kprobes** and **Ftrace** are used for kernel debugging and tracing, but they serve different purposes and are useful in different scenarios.  

---

## **1. When to Use Kprobes and Kretprobes Instead of Ftrace?**  

| **Scenario**                     | **Use Kprobes/Kretprobes?** | **Use Ftrace?** |
|----------------------------------|----------------------------|----------------|
| Debugging a specific function (before/after execution) | ✅ Yes | ❌ No (Ftrace traces all calls) |
| Intercepting function parameters | ✅ Yes | ❌ No (Ftrace doesn’t modify execution) |
| Tracking return values of functions | ✅ Yes (use Kretprobes) | ❌ No |
| Attaching probes dynamically without recompiling | ✅ Yes | ❌ No (Ftrace requires enabling at boot) |
| Tracing function execution timing & call graph | ❌ No | ✅ Yes |
| Checking kernel performance (latency, scheduling, etc.) | ❌ No | ✅ Yes |
| Getting a list of function calls (global tracing) | ❌ No | ✅ Yes |

### **When to Use Kprobes/Kretprobes Instead of Ftrace?**
- When you **only** need to probe a specific function.
- When you want to **capture function parameters**.
- When you need to **modify the execution** (e.g., filter calls, log errors).
- When tracing **return values** (use Kretprobes).

### **When to Use Ftrace Instead?**
- When you need **high-performance tracing**.
- When you want **to see all function calls** in a subsystem.
- When you need **a call graph or execution timing**.

---

## **2. Kprobe Example: Debugging a Function Call**
Let’s say you want to debug `do_sys_open()` in the kernel.

### **Step 1: Create a Kprobe**
```c
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>

static struct kprobe kp;

int handler_pre(struct kprobe *p, struct pt_regs *regs) {
    printk(KERN_INFO "Kprobe: do_sys_open() called with filename: %s\n",
           (char *)regs->si);  // Second argument (filename)
    return 0;
}

static int __init kprobe_init(void) {
    kp.symbol_name = "do_sys_open";  // Probe the do_sys_open() function
    kp.pre_handler = handler_pre;  // Pre-handler (before function executes)
    
    if (register_kprobe(&kp) < 0) {
        printk(KERN_ERR "Failed to register kprobe\n");
        return -1;
    }
    
    printk(KERN_INFO "Kprobe registered for do_sys_open\n");
    return 0;
}

static void __exit kprobe_exit(void) {
    unregister_kprobe(&kp);
    printk(KERN_INFO "Kprobe unregistered\n");
}

module_init(kprobe_init);
module_exit(kprobe_exit);
MODULE_LICENSE("GPL");
```

### **Step 2: Compile and Insert the Module**
```sh
make -C /lib/modules/$(uname -r)/build M=$PWD modules
sudo insmod kprobe_test.ko
```

### **Step 3: Test the Probe**
Run:
```sh
touch testfile.txt
```
Check `dmesg`:
```sh
dmesg | tail -10
```
Output:
```
Kprobe: do_sys_open() called with filename: testfile.txt
```
The **kprobe intercepted do_sys_open()** and extracted the filename argument.

---

## **3. Kretprobe Example: Capturing Return Values**
If you want to check the **return value of do_sys_open()**, use a **kretprobe**.

### **Step 1: Modify the Probe**
```c
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>

static struct kretprobe krp;

int handler_ret(struct kretprobe_instance *ri, struct pt_regs *regs) {
    printk(KERN_INFO "Kretprobe: do_sys_open() returned fd: %ld\n", regs->ax);
    return 0;
}

static int __init kretprobe_init(void) {
    krp.kp.symbol_name = "do_sys_open";  // Probe the function
    krp.handler = handler_ret;  // Run after function returns
    krp.maxactive = 20;  // Max concurrent probes
    
    if (register_kretprobe(&krp) < 0) {
        printk(KERN_ERR "Failed to register kretprobe\n");
        return -1;
    }
    
    printk(KERN_INFO "Kretprobe registered for do_sys_open\n");
    return 0;
}

static void __exit kretprobe_exit(void) {
    unregister_kretprobe(&krp);
    printk(KERN_INFO "Kretprobe unregistered\n");
}

module_init(kretprobe_init);
module_exit(kretprobe_exit);
MODULE_LICENSE("GPL");
```

### **Step 2: Load and Test**
```sh
sudo insmod kretprobe_test.ko
```
Run:
```sh
cat /etc/passwd
```
Check logs:
```sh
dmesg | tail -10
```
Output:
```
Kretprobe: do_sys_open() returned fd: 3
```
- The **kretprobe captured the return value** of `do_sys_open()`, which is the file descriptor.

---

## **4. When NOT to Use Kprobes**
- **Performance-sensitive areas**: Kprobes introduce overhead.
- **Interrupt context**: Can cause issues if not handled properly.
- **Frequent calls**: If the function is executed thousands of times per second, use Ftrace instead.

---

## **5. Final Thoughts**
| **Feature**      | **Kprobes/Kretprobes** | **Ftrace** |
|----------------|----------------------|-----------|
| Specific function probing | ✅ Yes | ❌ No |
| Capture arguments | ✅ Yes | ❌ No |
| Capture return values | ✅ Yes | ❌ No |
| Global function tracing | ❌ No | ✅ Yes |
| Call graph analysis | ❌ No | ✅ Yes |
| Performance impact | 🔴 High | 🟢 Low |

- **Use Kprobes/Kretprobes for deep debugging**.
- **Use Ftrace for lightweight tracing and performance monitoring**.

### **What is Kdump and When is it Used?**  

**Kdump** is a **crash-dump mechanism** for the Linux kernel that helps **analyze kernel crashes (OOPS, panics, and hangs)**. It captures a **memory dump (vmcore) of the crashed kernel** using a **second kernel (crash kernel)**.

---

## **1. Why Do We Use Kdump?**
Kdump is used in scenarios where the system crashes, and we need to debug the cause.  

### **Common Use Cases:**
| **Scenario** | **Why Use Kdump?** |
|-------------|---------------------|
| Kernel panic or OOPS | Capture memory state for analysis |
| Device driver crashes | Debug buggy drivers |
| Memory corruption | Identify invalid memory access |
| Deadlocks & hangs | Analyze kernel locks and scheduler issues |
| Hardware issues | Detect faulty memory or CPU failures |
| Production system debugging | Post-mortem debugging without rebooting to normal mode |

---

## **2. How Kdump Works?**
1. **Reserve a small memory region (crashkernel) at boot.**
2. **On a crash**, the primary kernel loads a **secondary kernel (crash kernel)** from reserved memory.
3. The crash kernel **dumps the memory (vmcore) to disk, network, or USB**.
4. The system **reboots normally**, and the dump is analyzed using `crash` or `gdb`.

---

## **3. How to Enable and Use Kdump?**
### **Step 1: Install Kdump Tools**
For Ubuntu/Debian:
```sh
sudo apt install kdump-tools crash kexec-tools
```
For Fedora/RHEL:
```sh
sudo yum install kexec-tools crash
```

### **Step 2: Configure Kernel to Reserve Memory**
Edit **GRUB** config:
```sh
sudo nano /etc/default/grub
```
Add:
```sh
GRUB_CMDLINE_LINUX="crashkernel=256M"
```
Then update GRUB:
```sh
sudo update-grub
```
Reboot:
```sh
sudo reboot
```

### **Step 3: Enable Kdump**
```sh
sudo systemctl enable kdump
sudo systemctl start kdump
```
Check if Kdump is active:
```sh
kdump-config show
```

### **Step 4: Trigger a Kernel Crash**
You can manually trigger a crash using `sysrq`:
```sh
echo c > /proc/sysrq-trigger
```
This will cause a **kernel panic**, and Kdump will capture a crash dump.

### **Step 5: Analyze the Crash Dump**
After reboot, check the dump location (`/var/crash` by default):
```sh
ls /var/crash/
```
Use `crash` to analyze:
```sh
sudo crash /usr/lib/debug/boot/vmlinux-$(uname -r) /var/crash/`date +%Y-%m-%d`/vmcore
```

---

## **4. How Kdump Helps Debug Kernel Crashes**
Using the `crash` tool, you can:
- **Backtrace the crash** (`bt` command)
- **Inspect memory regions** (`kmem`)
- **List processes running before crash** (`ps`)
- **Check kernel logs** (`log`)

Example:
```sh
crash> bt
PID: 1234 TASK: ffffffff810d4780 CPU: 0 COMMAND: "my_driver"
 #0 [ffff88003a0b7b58] machine_kexec at ffffffff810307a2
 #1 [ffff88003a0b7bb8] __crash_kexec at ffffffff810a25cb
 #2 [ffff88003a0b7c68] panic at ffffffff8105f52f
 #3 [ffff88003a0b7c98] do_exit at ffffffff8102d49c
```
This helps locate **which driver, function, or module caused the crash**.

---

## **5. Summary**
- **Kdump captures kernel memory during a crash for post-mortem analysis.**
- Used for **debugging kernel panics, OOPS, driver issues, and memory corruption**.
- Uses a **secondary kernel** to dump **vmcore** without corrupting primary kernel data.
- Essential for **kernel debugging in production systems**.

### **Hands-on Guide: Debugging a Real Kernel Crash with Kdump and GDB**  

This guide will help you **trigger a crash, capture the kernel dump (vmcore), and debug it using GDB and crash utility**.  

---

## **Step 1: Set Up Kdump**
### **1.1 Install Required Packages**  
For **Ubuntu/Debian**:
```sh
sudo apt install kexec-tools kdump-tools crash
```
For **RHEL/CentOS/Fedora**:
```sh
sudo yum install kexec-tools crash kernel-debuginfo
```

### **1.2 Configure GRUB to Reserve Memory for Kdump**
Edit the GRUB config:
```sh
sudo nano /etc/default/grub
```
Find the line:
```sh
GRUB_CMDLINE_LINUX=""
```
Modify it to:
```sh
GRUB_CMDLINE_LINUX="crashkernel=256M"
```
Save and update GRUB:
```sh
sudo update-grub  # For Ubuntu/Debian
sudo grub2-mkconfig -o /boot/grub2/grub.cfg  # For RHEL/Fedora
```
Reboot the system:
```sh
sudo reboot
```

### **1.3 Enable and Start Kdump**
```sh
sudo systemctl enable kdump
sudo systemctl start kdump
kdump-config show  # Check if kdump is active
```

---

## **Step 2: Trigger a Kernel Crash**
You can force a kernel panic to test Kdump.

### **2.1 Enable the Magic SysRq Key**
```sh
echo 1 | sudo tee /proc/sys/kernel/sysrq
```

### **2.2 Trigger a Kernel Panic**
```sh
echo c | sudo tee /proc/sysrq-trigger
```
- This will cause an **intentional kernel panic**.
- The system will **reboot**, and Kdump will capture the crash dump.

---

## **Step 3: Locate the Crash Dump**
After reboot, check the crash dump:
```sh
ls /var/crash/
```
You should see a `vmcore` file:
```sh
/var/crash/2025-03-31-11:30/vmcore
```

---

## **Step 4: Analyze the Crash Dump**
### **4.1 Install Debug Symbols**
For debugging, you need the **unstripped vmlinux** file:
```sh
sudo apt install linux-image-$(uname -r)-dbg  # Ubuntu/Debian
sudo yum install kernel-debuginfo  # RHEL/Fedora
```
Find the `vmlinux` file:
```sh
find /usr/lib/debug/boot/ -name "vmlinux-$(uname -r)"
```

### **4.2 Load the Crash Dump in GDB**
```sh
gdb /usr/lib/debug/boot/vmlinux-$(uname -r) /var/crash/2025-03-31-11:30/vmcore
```
Inside GDB, use:
```gdb
bt    # Show backtrace
info registers  # Check CPU registers
```

### **4.3 Load the Crash Dump in Crash Utility**
```sh
sudo crash /usr/lib/debug/boot/vmlinux-$(uname -r) /var/crash/2025-03-31-11:30/vmcore
```
Useful commands:
```sh
bt        # Show backtrace
log       # Show kernel logs
ps        # List processes before the crash
vm        # Show memory details
files     # Show open files
irq       # Show IRQ details
```

---

## **Step 5: Identify the Root Cause**
The `bt` command will show where the crash occurred. Example:
```
PID: 1234  TASK: ffff8e800081cd80  CPU: 0  COMMAND: "my_driver"
 #0 [ffff8e8001b7b588] machine_kexec at ffffffff810307a2
 #1 [ffff8e8001b7b5b8] __crash_kexec at ffffffff810a25cb
 #2 [ffff8e8001b7b668] panic at ffffffff8105f52f
 #3 [ffff8e8001b7b698] my_buggy_function at ffffffffa0012345 [my_driver]
```
- **The crash occurred in `my_buggy_function` in `my_driver`**.
- Now, inspect `my_buggy_function` in your driver code.

---

## **Step 6: Fix the Bug and Recompile**
- If the issue was **NULL pointer dereference**, check for **NULL checks**.
- If the issue was a **deadlock**, verify **lock usage**.
- If the issue was a **memory corruption**, check **buffer overflows**.

---

## **Summary**
✅ **Set up Kdump** to capture kernel crashes.  
✅ **Trigger a kernel panic** to generate a crash dump.  
✅ **Use GDB or Crash Utility** to analyze `vmcore`.  
✅ **Identify and fix the root cause** of the crash.  

