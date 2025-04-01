### **1. Inter-Process Communication (IPC) - Shared Memory**
```c
// shared_mem.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#define SHM_SIZE 1024

int main() {
    key_t key = ftok("shmfile", 65);
    int shmid = shmget(key, SHM_SIZE, 0666 | IPC_CREAT);
    char *data = (char *)shmat(shmid, NULL, 0);

    strcpy(data, "Hello from Shared Memory!");
    printf("Data written: %s\n", data);

    sleep(5);  // Simulate another process reading

    shmdt(data);
    shmctl(shmid, IPC_RMID, NULL);
    return 0;
}
```

### **2. DMA Driver (Character Device)**
```c
// dma_driver.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>

#define DEVICE_NAME "dma_dev"

static int dev_open(struct inode *inode, struct file *file) {
    pr_info("DMA device opened\n");
    return 0;
}

static int dev_release(struct inode *inode, struct file *file) {
    pr_info("DMA device closed\n");
    return 0;
}

static struct file_operations fops = {
    .open = dev_open,
    .release = dev_release,
};

static int __init dma_init(void) {
    register_chrdev(240, DEVICE_NAME, &fops);
    pr_info("DMA driver initialized\n");
    return 0;
}

static void __exit dma_exit(void) {
    unregister_chrdev(240, DEVICE_NAME);
    pr_info("DMA driver removed\n");
}

module_init(dma_init);
module_exit(dma_exit);
MODULE_LICENSE("GPL");
```

### **3. Kernel Mode Driver - Simple Read/Write**
```c
// simple_driver.c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "simple_drv"
#define BUF_SIZE 256

static char device_buffer[BUF_SIZE];

static ssize_t dev_read(struct file *filp, char __user *buffer, size_t len, loff_t *offset) {
    return simple_read_from_buffer(buffer, len, offset, device_buffer, BUF_SIZE);
}

static ssize_t dev_write(struct file *filp, const char __user *buffer, size_t len, loff_t *offset) {
    return simple_write_to_buffer(device_buffer, BUF_SIZE, offset, buffer, len);
}

static int dev_open(struct inode *inode, struct file *file) {
    pr_info("Device opened\n");
    return 0;
}

static int dev_release(struct inode *inode, struct file *file) {
    pr_info("Device closed\n");
    return 0;
}

static struct file_operations fops = {
    .open = dev_open,
    .release = dev_release,
    .read = dev_read,
    .write = dev_write,
};

static int __init simple_init(void) {
    register_chrdev(240, DEVICE_NAME, &fops);
    pr_info("Simple driver initialized\n");
    return 0;
}

static void __exit simple_exit(void) {
    unregister_chrdev(240, DEVICE_NAME);
    pr_info("Simple driver removed\n");
}

module_init(simple_init);
module_exit(simple_exit);
MODULE_LICENSE("GPL");
```

Here are more advanced sample programs:

---

### **4. Interrupt Handling in a Kernel Module**
```c
// irq_handler.c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>

#define IRQ_NO 1  // Assume we are handling keyboard interrupt

static irqreturn_t irq_handler(int irq, void *dev_id) {
    pr_info("Interrupt received: IRQ %d\n", irq);
    return IRQ_HANDLED;
}

static int __init irq_init(void) {
    int result = request_irq(IRQ_NO, irq_handler, IRQF_SHARED, "irq_handler", (void *)(irq_handler));
    if (result) {
        pr_err("Failed to request IRQ %d\n", IRQ_NO);
        return result;
    }
    pr_info("IRQ handler registered\n");
    return 0;
}

static void __exit irq_exit(void) {
    free_irq(IRQ_NO, (void *)(irq_handler));
    pr_info("IRQ handler unregistered\n");
}

module_init(irq_init);
module_exit(irq_exit);
MODULE_LICENSE("GPL");
```

---

### **5. Using Ftrace for Debugging Kernel Code**
```sh
# Enable function tracing
echo function > /sys/kernel/debug/tracing/current_tracer

# Trace a specific function
echo "do_sys_open" > /sys/kernel/debug/tracing/set_ftrace_filter

# Start tracing
echo 1 > /sys/kernel/debug/tracing/tracing_on

# Read the trace log
cat /sys/kernel/debug/tracing/trace

# Stop tracing
echo 0 > /sys/kernel/debug/tracing/tracing_on
```
This can be used to analyze system calls or specific driver functions.

---

### **6. PCIe Driver (Basic Skeleton)**
```c
// pcie_driver.c
#include <linux/module.h>
#include <linux/pci.h>

#define PCI_VENDOR_ID 0x8086  // Example Vendor ID
#define PCI_DEVICE_ID 0x1234  // Example Device ID

static struct pci_device_id pcie_ids[] = {
    { PCI_DEVICE(PCI_VENDOR_ID, PCI_DEVICE_ID) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, pcie_ids);

static int pcie_probe(struct pci_dev *pdev, const struct pci_device_id *ent) {
    pr_info("PCIe device detected: Vendor=0x%x, Device=0x%x\n", PCI_VENDOR_ID, PCI_DEVICE_ID);
    return 0;
}

static void pcie_remove(struct pci_dev *pdev) {
    pr_info("PCIe device removed\n");
}

static struct pci_driver pcie_driver = {
    .name = "pcie_sample",
    .id_table = pcie_ids,
    .probe = pcie_probe,
    .remove = pcie_remove,
};

static int __init pcie_init(void) {
    return pci_register_driver(&pcie_driver);
}

static void __exit pcie_exit(void) {
    pci_unregister_driver(&pcie_driver);
}

module_init(pcie_init);
module_exit(pcie_exit);
MODULE_LICENSE("GPL");
```

---
Here are more advanced examples covering **DMA, MMU handling, and user-space integration**.

---

### **7. DMA Engine API Example (Scatter-Gather Mode)**
```c
// dma_client.c
#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>

#define DMA_BUF_SIZE 4096

static dma_addr_t dma_src, dma_dst;
static void *src_buf, *dst_buf;
static struct dma_chan *dma_chan;
static struct completion dma_complete;

static void dma_callback(void *param) {
    pr_info("DMA Transfer Completed\n");
    complete(&dma_complete);
}

static int __init dma_client_init(void) {
    struct dma_device *dma_dev;
    struct dma_async_tx_descriptor *desc;

    dma_chan = dma_request_chan(NULL, "dma0chan0");
    if (IS_ERR(dma_chan)) {
        pr_err("Failed to get DMA channel\n");
        return PTR_ERR(dma_chan);
    }

    dma_dev = dma_chan->device;
    src_buf = dma_alloc_coherent(dma_dev->dev, DMA_BUF_SIZE, &dma_src, GFP_KERNEL);
    dst_buf = dma_alloc_coherent(dma_dev->dev, DMA_BUF_SIZE, &dma_dst, GFP_KERNEL);
    
    if (!src_buf || !dst_buf) {
        pr_err("DMA buffer allocation failed\n");
        return -ENOMEM;
    }

    memset(src_buf, 0xAA, DMA_BUF_SIZE);
    memset(dst_buf, 0x00, DMA_BUF_SIZE);

    init_completion(&dma_complete);
    desc = dmaengine_prep_dma_memcpy(dma_chan, dma_dst, dma_src, DMA_BUF_SIZE, DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
    desc->callback = dma_callback;
    dmaengine_submit(desc);
    dma_async_issue_pending(dma_chan);

    wait_for_completion(&dma_complete);
    
    dma_free_coherent(dma_dev->dev, DMA_BUF_SIZE, src_buf, dma_src);
    dma_free_coherent(dma_dev->dev, DMA_BUF_SIZE, dst_buf, dma_dst);
    dma_release_channel(dma_chan);

    return 0;
}

static void __exit dma_client_exit(void) {
    pr_info("DMA client module unloaded\n");
}

module_init(dma_client_init);
module_exit(dma_client_exit);
MODULE_LICENSE("GPL");
```
This example uses the **DMA Engine API** for memory transfers.

---

### **8. MMU Handling - Mapping Kernel Memory to User Space**
```c
// mmap_driver.c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "mmap_drv"
#define PAGE_COUNT 2

static char *kernel_buffer;

static int mmap_open(struct inode *inode, struct file *file) {
    pr_info("Memory-mapped device opened\n");
    return 0;
}

static int mmap_close(struct inode *inode, struct file *file) {
    pr_info("Memory-mapped device closed\n");
    return 0;
}

static int mmap_mmap(struct file *file, struct vm_area_struct *vma) {
    unsigned long pfn = virt_to_phys(kernel_buffer) >> PAGE_SHIFT;
    return remap_pfn_range(vma, vma->vm_start, pfn, PAGE_SIZE * PAGE_COUNT, vma->vm_page_prot);
}

static struct file_operations fops = {
    .open = mmap_open,
    .release = mmap_close,
    .mmap = mmap_mmap,
};

static int __init mmap_init(void) {
    int ret = register_chrdev(240, DEVICE_NAME, &fops);
    if (ret < 0) {
        pr_err("Failed to register device\n");
        return ret;
    }

    kernel_buffer = kmalloc(PAGE_SIZE * PAGE_COUNT, GFP_KERNEL);
    if (!kernel_buffer)
        return -ENOMEM;

    pr_info("Memory-mapped driver initialized\n");
    return 0;
}

static void __exit mmap_exit(void) {
    unregister_chrdev(240, DEVICE_NAME);
    kfree(kernel_buffer);
    pr_info("Memory-mapped driver removed\n");
}

module_init(mmap_init);
module_exit(mmap_exit);
MODULE_LICENSE("GPL");
```
This allows a user-space process to directly access kernel memory.

---

### **9. User-Space Integration - Read/Write a Device File**
#### **Kernel Module**
```c
// user_dev.c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "user_dev"
static char msg[100];

static ssize_t dev_read(struct file *filp, char __user *buffer, size_t len, loff_t *offset) {
    return simple_read_from_buffer(buffer, len, offset, msg, strlen(msg));
}

static ssize_t dev_write(struct file *filp, const char __user *buffer, size_t len, loff_t *offset) {
    return simple_write_to_buffer(msg, sizeof(msg), offset, buffer, len);
}

static struct file_operations fops = {
    .read = dev_read,
    .write = dev_write,
};

static int __init user_dev_init(void) {
    register_chrdev(240, DEVICE_NAME, &fops);
    pr_info("User device initialized\n");
    return 0;
}

static void __exit user_dev_exit(void) {
    unregister_chrdev(240, DEVICE_NAME);
    pr_info("User device removed\n");
}

module_init(user_dev_init);
module_exit(user_dev_exit);
MODULE_LICENSE("GPL");
```

#### **User-Space Code to Interact with the Kernel Module**
```c
// user_test.c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
    int fd = open("/dev/user_dev", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    write(fd, "Hello, Kernel!", 14);
    char buffer[50];
    read(fd, buffer, sizeof(buffer));
    printf("Read from device: %s\n", buffer);

    close(fd);
    return 0;
}
```
This program interacts with the kernel driver via **read/write system calls**.

---

Lock mechanisms are critical in the kernel to ensure **synchronization, data integrity, and concurrency control** when multiple threads or processes access shared resources. Here’s why they are essential:

### **1. Preventing Race Conditions**
   - Multiple CPU cores or processes can access shared kernel data structures simultaneously.
   - Without locks, a race condition might occur where two threads modify the same variable at the same time, leading to **corrupt data**.

### **2. Ensuring Atomic Operations**
   - Kernel operations (e.g., updating a process list, modifying DMA buffers) need to be atomic.
   - Locks ensure that one thread completes its operation before another starts.

### **3. Avoiding Deadlocks and Priority Inversions**
   - The kernel must carefully manage lock acquisition to avoid **deadlocks** (where two threads wait indefinitely for each other).
   - **Priority inversion** (low-priority process holding a lock needed by a high-priority process) is another issue managed through priority inheritance.

### **4. Maintaining Performance in Multi-Core Systems**
   - On SMP (Symmetric Multiprocessing) systems, multiple CPUs operate concurrently.
   - Proper locking mechanisms (like **RCU**, spinlocks, and mutexes) ensure efficient parallel execution without unnecessary contention.

---

## **Types of Kernel Locks**

### **1. Spinlocks (`spinlock_t`)**
   - Used when the critical section is **short**.
   - Busy-waits in a loop instead of sleeping.
   - Used in **interrupt handlers** and **high-speed operations**.

   ```c
   spinlock_t my_lock;
   spin_lock(&my_lock);
   // Critical section
   spin_unlock(&my_lock);
   ```

### **2. Mutex (`struct mutex`)**
   - Used when the critical section might **block**.
   - **Puts the process to sleep** if the lock is already held.
   - Suitable for **long operations** like file I/O.

   ```c
   struct mutex my_mutex;
   mutex_init(&my_mutex);
   mutex_lock(&my_mutex);
   // Critical section
   mutex_unlock(&my_mutex);
   ```

### **3. Read-Copy Update (RCU)**
   - Used for **read-heavy workloads**.
   - Readers don’t block writers, improving performance.
   - Used in **network stacks, process lists**.

   ```c
   rcu_read_lock();
   // Read data
   rcu_read_unlock();
   synchronize_rcu(); // Used when modifying data
   ```

### **4. Read/Write Locks (`rwlock_t`)**
   - Allows **multiple readers but only one writer**.
   - Used in **filesystem and shared resource management**.

   ```c
   rwlock_t my_rwlock;
   read_lock(&my_rwlock);
   // Read data
   read_unlock(&my_rwlock);

   write_lock(&my_rwlock);
   // Modify data
   write_unlock(&my_rwlock);
   ```

### **5. Seqlocks (`seqlock_t`)**
   - Used when **reads happen frequently, but writes are rare**.
   - Writers increment a sequence number.
   - Readers retry if they detect an update.

   ```c
   seqlock_t my_seqlock;
   unsigned seq;
   do {
       seq = read_seqbegin(&my_seqlock);
       // Read data
   } while (read_seqretry(&my_seqlock, seq));

   write_seqlock(&my_seqlock);
   // Modify data
   write_sequnlock(&my_seqlock);
   ```

---

## **When to Use Which Lock?**
| **Scenario**                 | **Lock Type**  |
|------------------------------|---------------|
| Short, non-blocking sections | Spinlock      |
| Blocking operations (file I/O) | Mutex        |
| High read, low write workloads | RCU         |
| Multiple readers, one writer | Read/Write Lock |
| Fast readers, rare writers | Seqlock |

### **How to Decide Which Lock to Use in the Kernel?**  

Choosing the right lock depends on **performance, concurrency requirements, and potential blocking**. Here's a structured approach:

---

## **1. Identify the Shared Resource**  
- **Is the resource accessed by multiple threads/processes?**  
  - If **no**, locking may not be necessary.  
  - If **yes**, continue.  

---

## **2. Check if Sleep is Allowed**  
| Condition | Lock Type |
|-----------|----------|
| If the code runs in **interrupt context** | **Spinlock** (Sleeping is not allowed) |
| If the code runs in **process context** | **Mutex, Read/Write Locks, RCU, Seqlock** (Sleeping is allowed) |

Example:  
- **Interrupt handlers → Spinlocks**
- **User-space system calls → Mutex**

---

## **3. How Long is the Critical Section?**  
| Critical Section Length | Lock Type |
|-------------------------|----------|
| **Short (few CPU cycles)** | **Spinlock** |
| **Long (involves I/O, blocking calls, waiting)** | **Mutex** |

Example:  
- **Modifying a small global variable → Spinlock**  
- **Accessing a file in a driver → Mutex**  

---

## **4. Read vs. Write Heavy Operations**  
| Access Pattern | Lock Type |
|---------------|----------|
| **More reads than writes (e.g., network stack, filesystem lookup)** | **RCU, Read/Write Locks, Seqlocks** |
| **Balanced reads and writes** | **Read/Write Lock** |
| **More writes than reads** | **Spinlock or Mutex** |

Example:  
- **Process table access → RCU** (Many reads, few writes)  
- **Shared resource modified frequently → Spinlock/Mutex**  

---

## **5. Single vs. Multi-Core Considerations**  
| System Type | Lock Type |
|------------|----------|
| **Single-core (no parallel execution)** | **Simple Mutex or No Lock Needed** |
| **Multi-core (SMP, parallel execution)** | **Spinlocks, RCU, Read/Write Locks** |

Example:  
- **Old embedded systems (single-core) → Mutex**  
- **Modern multi-core systems → Spinlocks, RCU**  

---

## **6. Is the Lock Contended?**  
| Contention Level | Lock Type |
|------------------|----------|
| **Low contention (rare conflicts)** | **Simple Mutex, Spinlock** |
| **High contention (frequent access)** | **RCU, Read/Write Lock, Seqlock** |

Example:  
- **Log buffers (rarely accessed) → Simple Mutex**  
- **Process table (frequently accessed) → RCU**  

---

## **Decision Tree for Choosing a Lock**  
```
  Is the resource shared?
       │
       ├── No  →  No lock needed
       │
       ├── Yes  
       │     │
       │     ├── Runs in Interrupt Context?  
       │     │      ├── Yes  →  Spinlock  
       │     │      ├── No  
       │     │          │
       │     │          ├── Short Critical Section?  
       │     │          │      ├── Yes  →  Spinlock  
       │     │          │      ├── No  
       │     │          │           │
       │     │          │           ├── Read-heavy?  
       │     │          │           │      ├── Yes  →  RCU / Read-Write Lock  
       │     │          │           │      ├── No  
       │     │          │           │           ├── Frequent writes?  
       │     │          │           │           │      ├── Yes  →  Spinlock/Mutex  
       │     │          │           │           │      ├── No  →  Read-Write Lock  
```

---

### **Examples of When to Use Each Lock**
| Scenario | Lock Type |
|----------|----------|
| Protecting a global counter in a driver | **Spinlock** |
| Protecting a list of open files in a filesystem | **Read/Write Lock** |
| Managing access to a network routing table | **RCU** |
| Synchronizing a large buffer with multiple writers | **Mutex** |
| Avoiding read-modify-write races in a high-speed counter | **Seqlock** |

---

Yes, **any driver can be accessed by multiple processes**, and that's one of the main reasons we need locks. Here’s why:

---

## **1. How Multiple Processes Access a Driver**
- When multiple processes open the same **character device** (e.g., `/dev/mydevice`), they can **read/write simultaneously**.
- Multiple system calls (e.g., `open()`, `read()`, `write()`) may execute concurrently on the same driver.
- Kernel threads and user-space processes may access shared kernel data structures at the same time.

**Example Scenario:**
- A **USB device driver** is accessed by two processes:
  - Process 1 writes data.
  - Process 2 reads data.
- If there’s no lock, **race conditions** can occur, leading to **data corruption or crashes**.

---

## **2. Why Locks are Needed in Device Drivers**
### **A. Preventing Race Conditions**
If two processes modify the same **shared variable** in the driver:
```c
static int counter = 0;

ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *pos) {
    counter++;  // Race condition possible
    return count;
}
```
- If **two processes** execute `counter++` at the same time, one update might be **lost**.
- Solution: Use a **mutex** or **spinlock**.

```c
static DEFINE_MUTEX(my_mutex);

ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *pos) {
    mutex_lock(&my_mutex);
    counter++;  
    mutex_unlock(&my_mutex);
    return count;
}
```

---

### **B. Synchronizing Access to Hardware**
- A **PCIe device** or **I2C/SPI peripheral** may receive commands from multiple processes.
- If two processes write at the same time, hardware may behave **unpredictably**.
- Solution: **Spinlocks** (for short locks) or **mutexes** (for long operations).

```c
static DEFINE_SPINLOCK(hw_lock);

void write_to_hardware(void) {
    spin_lock(&hw_lock);
    // Access hardware registers safely
    spin_unlock(&hw_lock);
}
```

---

### **C. Handling Shared Buffers (DMA, Network, Sound)**
- **Network drivers** use shared packet buffers (skb queues).
- **Audio drivers** manage shared memory for playback.
- **DMA buffers** may be accessed by multiple processes.
- Solution: **Read-Write Locks, RCU, Seqlocks**.

Example:
```c
rwlock_t buffer_lock;
void write_data(void) {
    write_lock(&buffer_lock);
    // Modify buffer
    write_unlock(&buffer_lock);
}
void read_data(void) {
    read_lock(&buffer_lock);
    // Read buffer
    read_unlock(&buffer_lock);
}
```

---

## **3. When is Locking Not Needed?**
- If the driver only serves **one process** at a time.
- If each process has its own **private buffer** (no shared state).
- If the driver is **read-only** (e.g., `/dev/random` or `/dev/urandom`).

---

## **4. Real-World Example: Mutex in a Character Device Driver**
```c
static DEFINE_MUTEX(my_mutex);

static ssize_t my_read(struct file *file, char __user *buf, size_t count, loff_t *pos) {
    mutex_lock(&my_mutex);
    // Read data safely
    mutex_unlock(&my_mutex);
    return count;
}

static ssize_t my_write(struct file *file, const char __user *buf, size_t count, loff_t *pos) {
    mutex_lock(&my_mutex);
    // Write data safely
    mutex_unlock(&my_mutex);
    return count;
}
```
- Ensures that **only one process** modifies the device at a time.

---

## **Conclusion**
- **Yes, drivers can be accessed by multiple processes**, and **locks are needed** to ensure **safe concurrent access**.
- **Choose the right lock** based on whether the resource is shared, how long it is used, and whether blocking is acceptable.

You can practice kernel programming, driver development, and synchronization mechanisms like locks directly on your **Raspberry Pi**. Here’s how to set up a proper **Linux kernel development environment** and test **locking mechanisms**.

---

## **1. Setting Up the Raspberry Pi for Kernel Development**
### **A. Get the Right Development Setup**
- Use a **Raspberry Pi 5** or **Raspberry Pi Zero W 2** (since you have both).
- Set up **cross-compilation** or compile directly on the Pi.

### **B. Install Required Tools**
On the Pi, install necessary packages:
```sh
sudo apt update
sudo apt install -y build-essential raspberrypi-kernel-headers git bc bison flex libssl-dev
```
If compiling on another system (e.g., x86 machine):
```sh
sudo apt install -y gcc-aarch64-linux-gnu
```

### **C. Get the Raspberry Pi Kernel Source**
```sh
git clone --depth=1 https://github.com/raspberrypi/linux.git
cd linux
```
To match your current Pi kernel:
```sh
uname -r  # Check current kernel version
```
Then check out the matching branch.

---

## **2. Writing a Simple Kernel Module to Test Locks**
You can write a **character driver** to test locks on the Raspberry Pi.

### **A. Sample Kernel Module Using Mutex**
```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "locktest"

static struct cdev my_cdev;
static struct class *my_class;
static dev_t dev_num;
static DEFINE_MUTEX(my_mutex);

static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {
    mutex_lock(&my_mutex);
    printk(KERN_INFO "Reading from device\n");
    mutex_unlock(&my_mutex);
    return 0;
}

static ssize_t my_write(struct file *file, const char __user *buf, size_t len, loff_t *offset) {
    mutex_lock(&my_mutex);
    printk(KERN_INFO "Writing to device\n");
    mutex_unlock(&my_mutex);
    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = my_read,
    .write = my_write,
};

static int __init my_init(void) {
    alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    my_class = class_create(THIS_MODULE, DEVICE_NAME);
    device_create(my_class, NULL, dev_num, NULL, DEVICE_NAME);
    cdev_init(&my_cdev, &fops);
    cdev_add(&my_cdev, dev_num, 1);
    printk(KERN_INFO "Lock test module loaded\n");
    return 0;
}

static void __exit my_exit(void) {
    cdev_del(&my_cdev);
    device_destroy(my_class, dev_num);
    class_destroy(my_class);
    unregister_chrdev_region(dev_num, 1);
    printk(KERN_INFO "Lock test module unloaded\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raspberry Pi Dev");
MODULE_DESCRIPTION("Kernel module to test mutex locking");
```

### **B. Compile and Load the Module**
```sh
make -C /lib/modules/$(uname -r)/build M=$PWD modules
sudo insmod locktest.ko
```

### **C. Test with Multiple Processes**
Open two terminals:
```sh
echo "Test" > /dev/locktest &
cat /dev/locktest &
```
- The **mutex** ensures only one process accesses the device at a time.

---

## **3. Practicing Spinlocks on the Pi**
Modify the module to use **spinlocks** instead of mutex:
```c
static spinlock_t my_spinlock;

static ssize_t my_read(struct file *file, char __user *buf, size_t len, loff_t *offset) {
    spin_lock(&my_spinlock);
    printk(KERN_INFO "Reading from device\n");
    spin_unlock(&my_spinlock);
    return 0;
}
```
Recompile and test **with multiple processes**.

---

## **4. Debugging Locks on the Pi**
Use `dmesg` to check kernel logs:
```sh
dmesg | tail -50
```
Enable **lock debugging**:
```sh
echo "1" > /sys/kernel/debug/lockdep/prove_locking
```
Use `ftrace` to check lock usage:
```sh
sudo mount -t debugfs none /sys/kernel/debug
sudo cat /sys/kernel/debug/tracing/trace
```

---

## **5. Cross-Compile for the Raspberry Pi**
If developing on an x86 machine:
```sh
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make -C /path/to/kernel M=$PWD modules
scp locktest.ko pi@raspberrypi:/home/pi/
```
Then SSH into the Pi and load the module.

---

## **Next Steps**
- **Experiment with RCU and Read-Write Locks**.
- **Modify a real driver** (e.g., SPI, I2C) to test synchronization.
- **Test locks under heavy load** using `stress-ng`.

