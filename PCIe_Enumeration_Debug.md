Below is an in‐depth explanation—covering from bootloader handoff through detailed kernel PCIe enumeration steps (with actual code excerpts)—that shows how Linux discovers and configures PCIe devices. This explanation also highlights key debugging points and log outputs that developers can use to trace problems.

---

## **1. From Bootloader to Kernel: The Early Stages**

### **A. Firmware and Bootloader (BIOS/UEFI + GRUB)**
- **Firmware Initialization:**  
  When the system powers on, the firmware (BIOS or UEFI) initializes essential hardware, including the PCIe Root Complex. Early enumeration may occur here, setting initial bus numbers.
  
- **Bootloader Role:**  
  GRUB (or another bootloader) then loads the Linux kernel and initial RAM disk (initrd) into memory. For example, you may see GRUB output like:
  ```
  GRUB 2.04~beta3-6ubuntu8
  Loading Linux kernel: /boot/vmlinuz-5.10.0-8-amd64
  Loading initial ramdisk: /boot/initrd.img-5.10.0-8-amd64
  ```
  GRUB also passes kernel parameters (e.g., `pci=debug`) to increase the verbosity of PCI logs.

### **B. Kernel Startup**
- **start_kernel():**  
  Once control passes to the kernel, the function `start_kernel()` (found in `init/main.c`) initializes basic subsystems: memory management, scheduler, ACPI, and so on.
  
- **Early PCI Subsystem Initialization:**  
  The kernel then calls initialization routines such as `pci_init()`, which registers PCI host bridges and sets up internal data structures. This code typically resides in `drivers/pci/pci.c`.

---

## **2. Detailed PCIe Enumeration in the Linux Kernel**

The Linux PCI subsystem follows several key phases to enumerate devices, allocate resources, and bind drivers.

### **A. Scanning the PCI Bus**

#### **1. pci_scan_bus() – The Core Enumerator**
- **What It Does:**  
  This function walks through all possible device and function numbers on a given bus. It uses low-level configuration space reads (often via `pci_read_config_word()`) to check if a device is present.

- **Simplified Code Excerpt:**
  ```c
  static void pci_scan_bus(struct pci_bus *bus)
  {
      int dev, fn;
      for (dev = 0; dev < PCI_SLOT_MAX; dev++) {
          for (fn = 0; fn < PCI_FUNC_MAX; fn++) {
              u16 vendor;
              vendor = pci_read_config_word(bus, dev, fn, PCI_VENDOR_ID);
              if (vendor == 0xffff)
                  continue;  // No device present at this dev/function

              // Create a pci_dev structure for the device.
              struct pci_dev *pdev = pci_create_dev(bus, dev, fn);
              pci_scan_device(pdev);

              // If function 0 isn’t multi-function, skip the rest.
              if (fn == 0 && !pci_is_multi_function(pdev))
                  break;
          }
      }
  }
  ```
  **Key Details:**
  - **Vendor ID Check:** A read returning 0xffff means no device is installed.
  - **Multi-function Handling:** If function 0 is not multi-function, the loop skips other functions.
  - **Device Creation:** A new `pci_dev` structure is allocated to hold the device’s configuration details.

#### **2. pci_scan_device() and pci_scan_bridge()**
- **Device Probing:**  
  After detecting a device, `pci_scan_device()` is called to read further configuration registers (like Class Code, BARs, etc.).
- **PCI Bridges:**  
  For devices that are bridges (e.g., a PCIe switch), `pci_scan_bridge()` is invoked. This function assigns a new bus number for the subordinate bus and recurses into it:
  ```c
  if (pci_is_pcie(pdev) && pci_is_bridge(pdev)) {
      pci_scan_bridge(pdev);
  }
  ```
  This ensures that every downstream bus is scanned.

### **B. Reading Configuration Space and BAR Probing**

#### **1. Reading Configuration Registers**
- **Standard Config Space:**  
  The kernel reads key registers such as Vendor ID, Device ID, Revision ID, Class Code, etc., using functions like:
  ```c
  u16 vendor = pci_read_config_word(pdev, PCI_VENDOR_ID);
  u16 device = pci_read_config_word(pdev, PCI_DEVICE_ID);
  ```
- **Extended PCIe Capabilities:**  
  For PCI Express devices, additional registers starting at offset 0x40 (or later for extended capabilities) are parsed.

#### **2. Probing BARs for Resource Requirements**
- **Determining Resource Size:**  
  To find out how much memory or I/O space a device needs, the kernel writes a value (typically 0xffffffff) to a BAR, reads it back, and calculates the size.
  ```c
  for (i = 0; i < PCI_NUM_RESOURCES; i++) {
      u32 bar_val = pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0 + (i * 4));
      if (!bar_val)
          continue;
  
      resource_size_t size = pci_size_of_bar(pdev, i);
      if (!size)
          continue;
  
      // Log or store the resource size for allocation.
      printk(KERN_DEBUG "Device %s BAR %d size: %pa\n", pci_name(pdev), i, &size);
  }
  ```
  **Explanation:**  
  - The method of writing all 1’s and then reading back determines the number of address bits used, thereby computing the size.

### **C. Resource Allocation and Mapping**

#### **1. pci_assign_resource() Function**
- **Resource Allocation:**  
  Once the kernel knows the sizes required, it allocates non-conflicting physical address ranges to each device’s BAR.
  ```c
  static int pci_assign_resource(struct pci_dev *dev)
  {
      int i;
      for (i = 0; i < PCI_NUM_RESOURCES; i++) {
          resource_size_t size = pci_size_of_bar(dev, i);
          if (!size)
              continue;
  
          // Example: allocate memory region based on resource type.
          pci_resource_start(dev, i) = pci_resource_alloc(dev, i, size);
          if (!pci_resource_start(dev, i)) {
              printk(KERN_ERR "Failed to allocate resource for BAR %d\n", i);
              return -ENODEV;
          }
      }
      return 0;
  }
  ```
  **Note:**  
  Actual resource allocation involves interactions with the system resource manager to ensure that address ranges don’t overlap.

#### **2. Memory Mapping**
- **Mapping to Virtual Space:**  
  Once assigned, device memory regions are mapped into the kernel’s virtual address space using routines like `ioremap()`. This lets device drivers access device registers easily.

### **D. Driver Binding and Probing**
- **PCI ID Matching:**  
  The PCI subsystem matches each `pci_dev` structure against PCI driver ID tables. These tables are declared in drivers (using `MODULE_DEVICE_TABLE(pci, ...)`).
- **Probe Call:**  
  The matched driver’s probe function is invoked:
  ```c
  static int my_driver_probe(struct pci_dev *pdev, const struct pci_device_id *id)
  {
      int ret = pci_enable_device(pdev);
      if (ret)
          return ret;
  
      // Allocate resources, map memory, and initialize interrupts.
      ret = pci_assign_resource(pdev);
      if (ret)
          return ret;
  
      // Final initialization and binding.
      return 0;
  }
  ```
  Log messages during this stage can look like:
  ```
  [    0.890123] nouveau 0000:01:00.0: enabling device (0000 -> 0002)
  ```

---

## **3. Debugging PCIe Enumeration in Kernel Code**

### **A. Using printk() and Kernel Logs**
- **Verbose PCI Logging:**  
  Developers can enable extra debugging in the PCI subsystem by adding kernel parameters like `pci=debug` and inserting `printk(KERN_DEBUG …)` statements in functions like `pci_scan_bus()` or `pci_assign_resource()`.
- **Example Debug Print:**
  ```c
  printk(KERN_DEBUG "pci_scan_bus: scanning bus %d, dev %d, fn %d\n", bus->number, dev, fn);
  ```

### **B. ftrace for Function-Level Tracing**
- **Tracing Key Functions:**  
  ftrace can be configured to trace PCI functions. For instance, to trace `pci_scan_bus()`, one might run:
  ```sh
  echo function > /sys/kernel/debug/tracing/current_tracer
  echo pci_scan_bus > /sys/kernel/debug/tracing/set_ftrace_filter
  echo 1 > /sys/kernel/debug/tracing/tracing_on
  cat /sys/kernel/debug/tracing/trace
  ```
- **Benefits:**  
  This technique helps developers see the sequence of function calls during enumeration and pinpoint where issues (like missed devices or resource conflicts) occur.

### **C. DebugFS Interfaces**
- **Live PCI Configurations:**  
  DebugFS entries (e.g., `/sys/kernel/debug/pci/0000:00:00.0/config`) allow developers to inspect the raw configuration space of devices in real time. This can verify whether BARs are programmed correctly.
- **Example Command:**
  ```sh
  cat /sys/kernel/debug/pci/0000:00:00.0/config
  ```

### **D. Kernel Source Code Review**
- **Reviewing Code Paths:**  
  Examining the source in directories such as `drivers/pci/` (e.g., `pci.c`, `pci_regs.c`) reveals how enumeration, resource allocation, and driver binding are implemented. This aids in understanding unexpected behavior seen in logs.

---

## **4. Sample Log Flow from Boot to PCIe Enumeration**

Here’s a synthetic (but realistic) log sequence that you might see:

1. **Bootloader Handoff:**
   ```
   GRUB 2.04~beta3-6ubuntu8
   Loading Linux kernel: /boot/vmlinuz-5.10.0-8-amd64
   Loading initial ramdisk: /boot/initrd.img-5.10.0-8-amd64
   ```
2. **Early Kernel Boot:**
   ```
   [    0.000000] Booting Linux on physical CPU 0x0
   [    0.123456] ACPI: Core revision 20191214
   ```
3. **PCI Host Bridge Registration:**
   ```
   [    0.345678] pci 0000:00:00.0: [8086:1234] type 00 class 0x060000
   [    0.345678] pci 0000:00:00.0: reg 0x10: [mem 0xfe800000-0xfe8fffff 64bit]
   ```
4. **PCI Bus Scanning:**
   ```
   [    0.456789] pci 0000:00:1c.0: PCI bridge to [bus 01]
   [    0.567890] pci 0000:00:1c.1: PCI bridge to [bus 02]
   ```
5. **Device Detection and BAR Probing:**
   ```
   [    0.678901] pci 0000:01:00.0: [10de:1db6] type 00 class 0x030000
   [    0.678901] pci 0000:01:00.0: BAR 0: assigned [mem 0xc0000000-0xc0ffffff 64bit]
   ```
6. **Driver Binding:**
   ```
   [    0.890123] nouveau 0000:01:00.0: enabling device (0000 -> 0002)
   ```

---

## **5. Summary**

- **Bootloader Handoff:** GRUB/UEFI loads the kernel; initial PCI setup may occur in firmware.
- **Kernel Initialization:**  
  The kernel calls `start_kernel()`, then `pci_init()`, which in turn registers PCI host bridges.
- **PCIe Enumeration:**  
  The kernel scans bus 0 using routines such as `pci_scan_bus()`, reads configuration space (including extended PCIe capabilities), probes BARs, calculates resource sizes, and assigns resources with functions like `pci_assign_resource()`.
- **Driver Binding:**  
  Devices are matched to drivers using PCI ID tables and are initialized via their probe routines.
- **Debugging Techniques:**  
  Developers leverage kernel logs, ftrace, DebugFS, and direct source code instrumentation (printk) to diagnose issues.
Below is a detailed, line‐by‐line walkthrough of two key functions from the Linux PCI subsystem: **pci_scan_bus()** and **pci_assign_resource()**. These functions are central to how the kernel enumerates and allocates resources for PCI/PCIe devices. For clarity, the code excerpts are simplified versions based on the actual kernel source, and commentary is provided for each major step.

---

## **A. Detailed Walkthrough of pci_scan_bus()**

The function **pci_scan_bus()** is responsible for iterating over all potential PCI devices on a given bus and creating a device structure when one is found. Consider the following pseudocode:

```c
static void pci_scan_bus(struct pci_bus *bus)
{
    int dev, fn;
    /* Loop over each device number on the bus */
    for (dev = 0; dev < PCI_SLOT_MAX; dev++) {
        /* Loop over each function number (0-7) */
        for (fn = 0; fn < PCI_FUNC_MAX; fn++) {
            u16 vendor;

            /* 
             * Read the vendor ID from the configuration space.
             * A value of 0xffff indicates that no device exists at this dev/function.
             */
            vendor = pci_read_config_word(bus, dev, fn, PCI_VENDOR_ID);
            if (vendor == 0xffff)
                continue;  // Skip to next function if no device is found

            /* 
             * If a device is found, create and initialize a pci_dev structure.
             * This structure holds all information for the device.
             */
            struct pci_dev *pdev = pci_create_dev(bus, dev, fn);
            if (!pdev) {
                printk(KERN_ERR "Failed to create pci_dev for bus %d, dev %d, fn %d\n",
                       bus->number, dev, fn);
                continue;
            }

            /* 
             * Perform further scanning of the device.
             * This might include reading BARs, class codes, and PCIe capabilities.
             */
            pci_scan_device(pdev);

            /* 
             * If function 0 is not a multi-function device, there's no need to scan
             * functions 1-7. The PCI spec defines that if the header type's multi-function
             * bit is not set, then only function 0 exists.
             */
            if (fn == 0 && !pci_is_multi_function(pdev))
                break;
        }
    }
}
```

### **Explanation of Key Steps:**

1. **Loop Over Device Numbers:**  
   The outer loop (`for (dev = 0; dev < PCI_SLOT_MAX; dev++)`) iterates through each possible device slot on the bus (usually 32 slots).

2. **Loop Over Functions:**  
   The inner loop (`for (fn = 0; fn < PCI_FUNC_MAX; fn++)`) handles multiple functions per device (up to 8).  
   - The function `pci_read_config_word()` reads the vendor ID from the PCI configuration space.
   - If the vendor ID is `0xffff`, it indicates an empty slot or non-existent function, so it continues to the next iteration.

3. **Device Creation:**  
   When a valid vendor ID is found, `pci_create_dev()` is called. This function allocates and initializes a `pci_dev` structure for the device, setting initial values like bus number, device number, and function number.

4. **Device Scanning:**  
   Once a device structure is created, `pci_scan_device()` is invoked. This function further reads the configuration space to get details such as:
   - Class code, revision ID, and header type.
   - Base Address Registers (BARs) to determine resource needs.
   - Extended PCI Express capabilities if available.

5. **Multi-function Device Check:**  
   If function 0 indicates the device isn’t multi-function (via the header type), the inner loop breaks, avoiding unnecessary reads for functions 1 through 7.

---

## **B. Detailed Walkthrough of pci_assign_resource()**

After enumeration, the kernel must allocate and assign non-conflicting memory or I/O address spaces to the devices. The function **pci_assign_resource()** handles this for each device’s BARs. Here’s a simplified version:

```c
static int pci_assign_resource(struct pci_dev *dev)
{
    int i;
    int ret = 0;

    /* Loop over each possible resource (BAR) of the device */
    for (i = 0; i < PCI_NUM_RESOURCES; i++) {
        resource_size_t size;

        /* Determine the size required by this BAR */
        size = pci_size_of_bar(dev, i);
        if (size == 0)
            continue;  // Skip if BAR is unused

        /*
         * Allocate a memory or I/O region that is large enough to satisfy this BAR.
         * The allocation routine ensures that the chosen address does not conflict with
         * addresses already assigned to other devices.
         */
        resource_size_t addr = pci_resource_alloc(dev, i, size);
        if (!addr) {
            printk(KERN_ERR "Resource allocation failed for device %s BAR %d\n",
                   pci_name(dev), i);
            ret = -ENODEV;
            break;
        }

        /* Program the allocated address into the device's BAR */
        pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + (i * 4), addr);
        printk(KERN_DEBUG "Assigned resource for %s BAR %d: addr %pa, size %pa\n",
               pci_name(dev), i, &addr, &size);
    }
    return ret;
}
```

### **Explanation of Key Steps:**

1. **Iterate Over Each BAR:**  
   The loop (`for (i = 0; i < PCI_NUM_RESOURCES; i++)`) covers each Base Address Register that a device might use (typically 6 for most devices).

2. **Calculate Required Resource Size:**  
   The function `pci_size_of_bar(dev, i)` writes a value (commonly all ones) to the BAR and reads it back to calculate the required size based on the bitmask that remains. If the size is zero, it means the BAR isn’t used.

3. **Resource Allocation:**  
   - `pci_resource_alloc(dev, i, size)` is responsible for selecting an appropriate address range that does not conflict with other devices.
   - If allocation fails (e.g., due to address space exhaustion), an error is logged and the function returns an error code.

4. **Programming the BAR:**  
   Once an address is allocated, the BAR is programmed with the new address by writing to the PCI configuration space using `pci_write_config_dword()`. This tells the device where its memory or I/O region resides.

5. **Debug Logging:**  
   Debug messages (via `printk(KERN_DEBUG …)`) help trace resource assignments. These messages are invaluable when you use tools like dmesg or ftrace to verify that each BAR has been assigned the correct address and size.

---

## **C. Debugging Techniques for Deep Kernel Analysis**

### **1. Using ftrace to Trace Function Execution**
- **Setup ftrace:**  
  You can monitor the execution of these functions by filtering them in ftrace:
  ```sh
  echo function > /sys/kernel/debug/tracing/current_tracer
  echo pci_scan_bus > /sys/kernel/debug/tracing/set_ftrace_filter
  echo pci_assign_resource >> /sys/kernel/debug/tracing/set_ftrace_filter
  echo 1 > /sys/kernel/debug/tracing/tracing_on
  cat /sys/kernel/debug/tracing/trace
  ```
  This output shows you the order and timing of calls, helping you spot any anomalies in how devices are scanned and resources assigned.

### **2. Inserting Additional printk() Statements**
- **Instrumenting Code:**  
  Adding extra `printk()` statements in these functions can provide real-time insights during boot. For example, printing the bus, device, function numbers in `pci_scan_bus()` or printing allocated resource addresses in `pci_assign_resource()` helps verify that each step completes as expected.
  
### **3. Using DebugFS**
- **Live Configuration Inspection:**  
  Check the configuration of a specific device using:
  ```sh
  cat /sys/kernel/debug/pci/0000:01:00.0/config
  ```
  This shows the raw contents of the PCI configuration space, confirming that BARs are programmed correctly.

### **4. Analyzing Kernel Logs**
- **Review dmesg:**  
  After boot, use:
  ```sh
  dmesg | grep -i "pci"
  ```
  to see the detailed logs produced during PCI enumeration and resource assignment.

---

## **Summary**

- **pci_scan_bus():**  
  Iterates over potential device slots and functions, reads vendor IDs, creates `pci_dev` structures, and further probes each device. Special handling is provided for multi-function devices and PCI bridges.

- **pci_assign_resource():**  
  Determines resource requirements by probing BARs, allocates non-conflicting memory/I/O ranges, and programs these addresses back into the device configuration space.

- **Debugging:**  
  Tools like ftrace, printk() instrumentation, DebugFS, and kernel log analysis (dmesg) are crucial for diagnosing issues during these phases.

Below is a sample Realtek Ethernet PCIe driver. This is a simplified example intended for educational purposes—it registers as a PCI network device, maps BAR0 for device registers, allocates a network device, and implements minimal netdev operations. In a production driver (such as r8169), there would be much more handling for interrupts, DMA, error recovery, and advanced features.

```c
/*
 * Sample Realtek Ethernet PCIe Driver
 *
 * This is a simplified sample driver for a Realtek Ethernet PCIe device.
 * It registers a PCI device, maps the device registers, allocates a net_device,
 * and implements basic netdev operations.
 *
 * Note: This code is for educational purposes only and does not implement a full driver.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#define DRV_NAME "rtl_sample"
#define PCI_VENDOR_ID_REALTEK 0x10ec
#define PCI_DEVICE_ID_REALTEK_SAMPLE 0x8168  /* Example device ID for sample */

struct rtl_sample_priv {
    struct net_device *netdev;
    void __iomem *mmio;
    struct pci_dev *pdev;
};

/* Netdevice operations */
static int rtl_sample_open(struct net_device *dev)
{
    netif_start_queue(dev);
    dev_info(dev, "rtl_sample: device opened\n");
    return 0;
}

static int rtl_sample_stop(struct net_device *dev)
{
    netif_stop_queue(dev);
    dev_info(dev, "rtl_sample: device stopped\n");
    return 0;
}

static netdev_tx_t rtl_sample_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    /* In a real driver, transmit the packet over the network.
       Here, we simply free the skb and report success. */
    dev_info(dev, "rtl_sample: Transmitting packet of length %u\n", skb->len);
    dev_kfree_skb(skb);
    return NETDEV_TX_OK;
}

static const struct net_device_ops rtl_sample_netdev_ops = {
    .ndo_open       = rtl_sample_open,
    .ndo_stop       = rtl_sample_stop,
    .ndo_start_xmit = rtl_sample_start_xmit,
};

static int rtl_sample_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    int ret;
    struct net_device *netdev;
    struct rtl_sample_priv *priv;
    void __iomem *mmio;

    ret = pci_enable_device(pdev);
    if (ret)
        return ret;

    pci_set_master(pdev);

    ret = pci_request_regions(pdev, DRV_NAME);
    if (ret)
        goto disable_device;

    /* Map BAR0 for device registers */
    mmio = pci_iomap(pdev, 0, 0);
    if (!mmio) {
        ret = -EIO;
        goto release_regions;
    }

    /* Allocate the network device with private data area */
    netdev = alloc_etherdev(sizeof(struct rtl_sample_priv));
    if (!netdev) {
        ret = -ENOMEM;
        goto unmap;
    }
    SET_NETDEV_DEV(netdev, &pdev->dev);
    pci_set_drvdata(pdev, netdev);

    priv = netdev_priv(netdev);
    priv->netdev = netdev;
    priv->mmio   = mmio;
    priv->pdev   = pdev;

    /* Setup net_device operations and assign a default MAC address */
    netdev->netdev_ops = &rtl_sample_netdev_ops;
    eth_hw_addr_random(netdev);

    ret = register_netdev(netdev);
    if (ret)
        goto free_netdev;

    dev_info(&pdev->dev, "Realtek sample Ethernet driver loaded, device %s\n", netdev->name);
    return 0;

free_netdev:
    free_netdev(netdev);
unmap:
    pci_iounmap(pdev, mmio);
release_regions:
    pci_release_regions(pdev);
disable_device:
    pci_disable_device(pdev);
    return ret;
}

static void rtl_sample_remove(struct pci_dev *pdev)
{
    struct net_device *netdev = pci_get_drvdata(pdev);
    struct rtl_sample_priv *priv = netdev ? netdev_priv(netdev) : NULL;

    if (netdev) {
        unregister_netdev(netdev);
        if (priv && priv->mmio)
            pci_iounmap(pdev, priv->mmio);
        free_netdev(netdev);
    }
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    dev_info(&pdev->dev, "Realtek sample Ethernet driver removed\n");
}

/* PCI device ID table */
static const struct pci_device_id rtl_sample_ids[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_REALTEK, PCI_DEVICE_ID_REALTEK_SAMPLE), },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, rtl_sample_ids);

/* PCI driver structure */
static struct pci_driver rtl_sample_driver = {
    .name       = DRV_NAME,
    .id_table   = rtl_sample_ids,
    .probe      = rtl_sample_probe,
    .remove     = rtl_sample_remove,
};

module_pci_driver(rtl_sample_driver);

MODULE_AUTHOR("Realtek Sample Developer");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sample Realtek Ethernet PCIe Driver");
```

### Explanation

- **PCI Device ID Table:**  
  The `rtl_sample_ids` table registers a sample Realtek PCIe device (using vendor ID 0x10ec and an example device ID 0x8168). Adjust these values to match your specific hardware.

- **Probe Function:**  
  The `rtl_sample_probe()` function is called when the kernel finds a matching PCI device. It enables the device, requests its regions, maps BAR0, allocates a network device (using `alloc_etherdev`), sets up basic network operations, and registers the device.

- **Netdev Operations:**  
  Minimal netdev operations (`open`, `stop`, and `start_xmit`) are provided. In a full driver, these would interact with hardware to transmit and receive packets.

- **Remove Function:**  
  `rtl_sample_remove()` cleans up the allocated resources when the device is removed.

### Debugging Considerations

- **Kernel Logs:**  
  The driver uses `dev_info()` and `dev_err()` calls to log key events. These messages help in debugging and can be viewed using `dmesg`.

- **PCI and Netdev Tools:**  
  Use `lspci -vvv` to verify PCI device detection, and `ifconfig` or `ip link` to check network device registration.

