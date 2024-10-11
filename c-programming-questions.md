

### 1. Basic Bitwise Operators

**Description:**
Basic bitwise operators include AND (`&`), OR (`|`), XOR (`^`), NOT (`~`), left shift (`<<`), and right shift (`>>`). These are used for manipulating bits in variables.

**Code Example:**

```c
#include <stdio.h>

int main() {
    unsigned int a = 0b1010; // 10 in binary
    unsigned int b = 0b1100; // 12 in binary

    unsigned int and_result = a & b; // Bitwise AND
    unsigned int or_result = a | b;  // Bitwise OR
    unsigned int xor_result = a ^ b; // Bitwise XOR
    unsigned int not_result = ~a;    // Bitwise NOT

    unsigned int left_shift = a << 1;  // Left shift by 1
    unsigned int right_shift = a >> 1; // Right shift by 1

    printf("AND Result: %u\n", and_result);
    printf("OR Result: %u\n", or_result);
    printf("XOR Result: %u\n", xor_result);
    printf("NOT Result: %u\n", not_result);
    printf("Left Shift: %u\n", left_shift);
    printf("Right Shift: %u\n", right_shift);

    return 0;
}
```

---

### 2. Setting, Clearing, and Toggling Bits

**Description:**
Manipulating individual bits is common in kernel programming for setting flags or configuring hardware registers.

**Code Example:**

```c
#include <stdio.h>

#define BIT(n) (1UL << (n))

int main() {
    unsigned int flags = 0;

    // Set bit 2
    flags |= BIT(2);

    // Clear bit 2
    flags &= ~BIT(2);

    // Toggle bit 3
    flags ^= BIT(3);

    printf("Flags: %u\n", flags);

    return 0;
}
```

---

### 3. Checking if a Bit is Set

**Description:**
Often, you need to check if a specific bit is set in a variable.

**Code Example:**

```c
#include <stdio.h>

#define BIT(n) (1UL << (n))

int main() {
    unsigned int status = 0b1010; // 10 in binary

    // Check if bit 3 is set
    if (status & BIT(3)) {
        printf("Bit 3 is set.\n");
    } else {
        printf("Bit 3 is not set.\n");
    }

    return 0;
}
```

---

### 4. Using Bitfields in Structures

**Description:**
Bitfields allow you to pack multiple variables into a single word, which is useful for memory-mapped hardware registers.

**Code Example:**

```c
#include <stdio.h>

struct cpu_flags {
    unsigned int carry : 1;
    unsigned int zero  : 1;
    unsigned int sign  : 1;
    unsigned int overflow : 1;
};

int main() {
    struct cpu_flags flags = {0};

    // Set the zero flag
    flags.zero = 1;

    // Check the zero flag
    if (flags.zero) {
        printf("Zero flag is set.\n");
    }

    return 0;
}
```

---

### 5. Atomic Bit Operations

**Description:**
In multi-threaded environments like the kernel, atomic operations are essential to prevent race conditions.

**Code Example:**

```c
#include <linux/atomic.h>

void set_flag_atomic(unsigned long *flags, int bit) {
    set_bit(bit, flags);
}

void clear_flag_atomic(unsigned long *flags, int bit) {
    clear_bit(bit, flags);
}

int test_flag_atomic(unsigned long *flags, int bit) {
    return test_bit(bit, flags);
}
```

---

### 6. Bitmask Operations

**Description:**
Bitmasks are used to manipulate multiple bits at once.

**Code Example:**

```c
#include <stdio.h>

#define READ_PERMISSION  (1 << 0)
#define WRITE_PERMISSION (1 << 1)
#define EXECUTE_PERMISSION (1 << 2)

int main() {
    unsigned int permissions = 0;

    // Grant read and write permissions
    permissions |= (READ_PERMISSION | WRITE_PERMISSION);

    // Revoke write permission
    permissions &= ~WRITE_PERMISSION;

    // Check permissions
    if (permissions & READ_PERMISSION) {
        printf("Read permission granted.\n");
    }

    if (permissions & WRITE_PERMISSION) {
        printf("Write permission granted.\n");
    } else {
        printf("Write permission revoked.\n");
    }

    return 0;
}
```

---

### 7. Finding the Position of the First Set Bit

**Description:**
Finding the first set bit is a common operation, and the kernel provides optimized functions for it.

**Code Example:**

```c
#include <linux/kernel.h>

void find_first_set_bit(unsigned long bitmap) {
    int pos = __ffs(bitmap);
    if (pos >= 0) {
        printk(KERN_INFO "First set bit at position: %d\n", pos);
    } else {
        printk(KERN_INFO "No bits are set.\n");
    }
}
```

---

### 8. Counting the Number of Set Bits

**Description:**
Counting set bits (population count) is used in various algorithms.

**Code Example:**

```c
#include <linux/kernel.h>

void count_set_bits(unsigned long bitmap) {
    int count = hweight_long(bitmap);
    printk(KERN_INFO "Number of set bits: %d\n", count);
}
```

---

### 9. Swapping Bits or Bytes

**Description:**
Swapping bits or bytes is useful in endian conversions and data manipulation.

**Code Example:**

```c
#include <stdio.h>

unsigned int swap_bytes(unsigned int x) {
    return ((x & 0xFF000000) >> 24) |
           ((x & 0x00FF0000) >> 8)  |
           ((x & 0x0000FF00) << 8)  |
           ((x & 0x000000FF) << 24);
}

int main() {
    unsigned int num = 0x12345678;
    unsigned int swapped = swap_bytes(num);

    printf("Original: 0x%X\n", num);
    printf("Swapped: 0x%X\n", swapped);

    return 0;
}
```

---

### 10. Bit Manipulation Macros in the Kernel

**Description:**
The Linux kernel provides various macros for efficient bit manipulation.

**Code Example:**

```c
#include <linux/kernel.h>

void example_macros(unsigned long bitmap) {
    if (test_bit(5, &bitmap)) {
        printk(KERN_INFO "Bit 5 is set.\n");
    }

    set_bit(3, &bitmap);
    clear_bit(2, &bitmap);
    change_bit(1, &bitmap); // Toggles bit 1
}
```

---

### 11. Endianness and Bit Operations

**Description:**
Understanding how endianness affects bitwise operations is crucial, especially when dealing with different architectures.

**Code Example:**

```c
#include <linux/byteorder/little_endian.h>
#include <linux/byteorder/big_endian.h>

void endian_example() {
    uint16_t num = 0x1234;
    uint16_t swapped = __swab16(num); // Swaps byte order

    printk(KERN_INFO "Original: 0x%X\n", num);
    printk(KERN_INFO "Swapped: 0x%X\n", swapped);
}
```

---

### 12. Aligning Memory Addresses

**Description:**
Alignment is important for performance and hardware requirements.

**Code Example:**

```c
#include <linux/kernel.h>

void align_address_example(void *ptr) {
    unsigned long addr = (unsigned long)ptr;
    unsigned long aligned_addr = ALIGN(addr, 16);

    printk(KERN_INFO "Original Address: %p\n", ptr);
    printk(KERN_INFO "Aligned Address: %p\n", (void *)aligned_addr);
}
```

---

### 13. Bitwise Rotation

**Description:**
Rotating bits is used in cryptographic algorithms and checksums.

**Code Example:**

```c
#include <stdio.h>

unsigned int rotate_left(unsigned int value, int shift) {
    return (value << shift) | (value >> (32 - shift));
}

unsigned int rotate_right(unsigned int value, int shift) {
    return (value >> shift) | (value << (32 - shift));
}

int main() {
    unsigned int num = 0x12345678;
    unsigned int rotated_left = rotate_left(num, 8);
    unsigned int rotated_right = rotate_right(num, 8);

    printf("Original: 0x%X\n", num);
    printf("Rotated Left: 0x%X\n", rotated_left);
    printf("Rotated Right: 0x%X\n", rotated_right);

    return 0;
}
```

---

### 14. Bit Reversal

**Description:**
Reversing bits is sometimes needed in algorithms like CRC calculations.

**Code Example:**

```c
#include <stdio.h>

unsigned int reverse_bits(unsigned int num) {
    unsigned int count = sizeof(num) * 8 - 1;
    unsigned int reverse_num = num;

    num >>= 1;
    while (num) {
        reverse_num <<= 1;
        reverse_num |= num & 1;
        num >>= 1;
        count--;
    }
    reverse_num <<= count;
    return reverse_num;
}

int main() {
    unsigned int num = 0b00000000000000000000000000001101;
    unsigned int reversed = reverse_bits(num);

    printf("Original: %u\n", num);
    printf("Reversed: %u\n", reversed);

    return 0;
}
```

---

### 15. Efficient Multiplication and Division by Powers of Two

**Description:**
Using shifts for multiplication or division by powers of two is more efficient than using the arithmetic operators.

**Code Example:**

```c
#include <stdio.h>

int main() {
    unsigned int num = 4;

    unsigned int multiplied = num << 3; // Multiply by 8
    unsigned int divided = num >> 1;    // Divide by 2

    printf("Multiplied by 8: %u\n", multiplied);
    printf("Divided by 2: %u\n", divided);

    return 0;
}
```

---

### 16. Conditional Operations Using Bitwise Operators

**Description:**
Bitwise operators can be used for conditional operations without branching, which is useful in performance-critical code.

**Code Example:**

```c
#include <stdio.h>

int main() {
    unsigned int condition = 0; // 0 or 1
    unsigned int result;

    // If condition is true (1), result is 100; else, result is 200
    result = (condition * 100) | ((~condition & 1) * 200);

    printf("Result: %u\n", result);

    return 0;
}
```

---

### 17. Accessing Hardware Registers

**Description:**
Bitwise operations are essential when reading from or writing to hardware registers.

**Code Example:**

```c
#include <linux/io.h>

#define DEVICE_REGISTER_ADDRESS 0xFFFF0000

void access_hardware_register(void) {
    void __iomem *reg_base = ioremap(DEVICE_REGISTER_ADDRESS, 0x100);

    if (reg_base) {
        unsigned int value = ioread32(reg_base);
        value |= (1 << 5); // Set bit 5
        iowrite32(value, reg_base);

        iounmap(reg_base);
    }
}
```

---

### 18. Bit Manipulation in Interrupt Handlers

**Description:**
Manipulating bits efficiently is crucial in interrupt handlers to maintain performance.

**Code Example:**

```c
#include <linux/interrupt.h>

irqreturn_t my_interrupt_handler(int irq, void *dev_id) {
    unsigned long status;

    // Read interrupt status register
    status = readl(STATUS_REGISTER);

    // Clear the handled interrupt
    writel(status, STATUS_REGISTER);

    // Process based on which bits are set
    if (status & BIT(0)) {
        // Handle interrupt type 0
    }

    if (status & BIT(1)) {
        // Handle interrupt type 1
    }

    return IRQ_HANDLED;
}
```

---

### 19. Implementing Spinlocks with Bitwise Operations

**Description:**
Spinlocks are synchronization primitives that can be implemented using atomic bitwise operations.

**Code Example:**

```c
#include <linux/spinlock.h>

spinlock_t my_lock;

void critical_section(void) {
    unsigned long flags;

    spin_lock_irqsave(&my_lock, flags);
    // Critical section code here
    spin_unlock_irqrestore(&my_lock, flags);
}
```

---

### 20. Bitwise Optimization Techniques

**Description:**
Optimizing code using bitwise tricks can enhance performance in critical sections.

**Code Example:**

```c
#include <stdio.h>

// Check if a number is a power of two
int is_power_of_two(unsigned int x) {
    return x && !(x & (x - 1));
}

int main() {
    unsigned int num = 16;

    if (is_power_of_two(num)) {
        printf("%u is a power of two.\n", num);
    } else {
        printf("%u is not a power of two.\n", num);
    }

    return 0;
}
```
