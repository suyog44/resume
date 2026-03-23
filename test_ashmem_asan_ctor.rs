
#![no_std]
#![allow(dead_code, unused_variables)]

use core::{
    cell::UnsafeCell,
    pin::Pin,
    ptr::null_mut,
    sync::atomic::{AtomicBool, AtomicPtr, AtomicUsize, Ordering},
};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

// ═══════════════════════════════════════════════════════════════
// EXACT statics from ashmem.rs lines 73-76
// These trigger asan.module_ctor generation under KASAN.
// ═══════════════════════════════════════════════════════════════

static NUM_PIN_IOCTLS_WAITING: AtomicUsize = AtomicUsize::new(0);
static IGNORE_UNSET_PROT_READ: AtomicBool = AtomicBool::new(false);
static IGNORE_UNSET_PROT_EXEC: AtomicBool = AtomicBool::new(false);
static ASHMEM_FOPS_PTR: AtomicPtr<u8> = AtomicPtr::new(null_mut());
static LRU_COUNT: AtomicUsize = AtomicUsize::new(0);

// ═══════════════════════════════════════════════════════════════
// Constants matching ashmem.rs
// ═══════════════════════════════════════════════════════════════

const ASHMEM_NAME_LEN: usize = 256;
const ASHMEM_NAME_PREFIX_LEN: usize = 11;
const ASHMEM_FULL_NAME_LEN: usize = ASHMEM_NAME_LEN + ASHMEM_NAME_PREFIX_LEN;
const ASHMEM_NAME_PREFIX: [u8; ASHMEM_NAME_PREFIX_LEN] = *b"dev/ashmem/";
const ASHMEM_MAX_SIZE: usize = usize::MAX >> 1;

const PROT_READ: usize = 0x1;
const PROT_WRITE: usize = 0x2;
const PROT_EXEC: usize = 0x4;
const PROT_MASK: usize = PROT_EXEC | PROT_READ | PROT_WRITE;

type Result<T> = core::result::Result<T, i32>;
const EINVAL: i32 = -22;
const ENOTTY: i32 = -25;
const EPERM: i32 = -1;

// ═══════════════════════════════════════════════════════════════
// Simulated kernel uaccess types — pub to avoid private_interfaces
// ═══════════════════════════════════════════════════════════════

pub struct UserSliceReader { addr: usize, remaining: usize }
pub struct UserSliceWriter { addr: usize, remaining: usize }

impl UserSliceReader {
    #[inline(never)]
    fn read_all(self, buf: &mut [u8]) -> Result<()> {
        for i in 0..core::cmp::min(self.remaining, buf.len()) {
            unsafe { buf[i] = core::ptr::read_volatile((self.addr + i) as *const u8); }
        }
        Ok(())
    }
}

impl UserSliceWriter {
    #[inline(never)]
    fn write_all(self, buf: &[u8]) -> Result<()> {
        for i in 0..core::cmp::min(self.remaining, buf.len()) {
            unsafe { core::ptr::write_volatile((self.addr + i) as *mut u8, buf[i]); }
        }
        Ok(())
    }
}

fn user_slice_reader(addr: usize, len: usize) -> UserSliceReader {
    UserSliceReader { addr, remaining: len }
}
fn user_slice_writer(addr: usize, len: usize) -> UserSliceWriter {
    UserSliceWriter { addr, remaining: len }
}

fn ioc_size(cmd: u32) -> usize { ((cmd >> 16) & 0x3FFF) as usize }

// ═══════════════════════════════════════════════════════════════
// AshmemInner — mirrors ashmem.rs:138-145
// ═══════════════════════════════════════════════════════════════

struct AshmemInner {
    size: usize,
    prot_mask: usize,
    name: Option<[u8; ASHMEM_NAME_LEN]>,
    name_len: usize,
    mapped: bool,
}

// ═══════════════════════════════════════════════════════════════
// Ashmem — mirrors ashmem.rs:133-136
// Real code uses Mutex<AshmemInner> for interior mutability.
// We use UnsafeCell to avoid the &T→&mut T cast error.
// ═══════════════════════════════════════════════════════════════

pub struct Ashmem {
    inner: UnsafeCell<AshmemInner>,
}

// SAFETY: In real kernel code, Mutex provides Send+Sync.
// Here UnsafeCell is only accessed from #[inline(never)] fns.
unsafe impl Sync for Ashmem {}
unsafe impl Send for Ashmem {}

impl Ashmem {
    /// Simulates Mutex::lock() — returns &mut AshmemInner
    ///
    /// SAFETY: caller must ensure exclusive access (in real code,
    /// the Mutex guarantees this).
    #[inline(always)]
    unsafe fn lock(&self) -> &mut AshmemInner {
        &mut *self.inner.get()
    }

    // ── ashmem.rs:300 — set_name ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
        let inner = unsafe { self.lock() };
        if inner.mapped { return Err(EINVAL); }
        let mut name = [0u8; ASHMEM_NAME_LEN];
        let len = core::cmp::min(reader.remaining, ASHMEM_NAME_LEN - 1);
        let r = UserSliceReader { addr: reader.addr, remaining: len };
        r.read_all(&mut name[..len])?;
        name[len] = 0;
        inner.name = Some(name);
        inner.name_len = len;
        Ok(0)
    }

    // ── ashmem.rs:315 — get_name ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
        let inner = unsafe { self.lock() };
        match inner.name {
            Some(ref name) => {
                let mut full_name = [0u8; ASHMEM_FULL_NAME_LEN];
                full_name[..ASHMEM_NAME_PREFIX_LEN].copy_from_slice(&ASHMEM_NAME_PREFIX);
                let copy_len = core::cmp::min(inner.name_len, ASHMEM_NAME_LEN);
                full_name[ASHMEM_NAME_PREFIX_LEN..ASHMEM_NAME_PREFIX_LEN + copy_len]
                    .copy_from_slice(&name[..copy_len]);
                writer.write_all(&full_name[..ASHMEM_NAME_PREFIX_LEN + copy_len])?;
                Ok(0)
            }
            None => {
                writer.write_all(b"dev/ashmem/")?;
                Ok(0)
            }
        }
    }

    // ── ashmem.rs:334 — set_size ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_set_size(self: Pin<&Self>, size: usize) -> Result<isize> {
        let inner = unsafe { self.lock() };
        if inner.mapped { return Err(EINVAL); }
        if size == 0 || size >= ASHMEM_MAX_SIZE { return Err(EINVAL); }
        inner.size = size;
        Ok(0)
    }

    // ── ashmem.rs:343 — get_size ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_get_size(self: Pin<&Self>) -> Result<isize> {
        let inner = unsafe { self.lock() };
        Ok(inner.size as isize)
    }

    // ── ashmem.rs:347 — set_prot_mask (reads IGNORE_UNSET_PROT_* statics) ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_set_prot_mask(self: Pin<&Self>, mut prot: usize) -> Result<isize> {
        let inner = unsafe { self.lock() };

        if IGNORE_UNSET_PROT_READ.load(Ordering::Relaxed) {
            prot |= PROT_READ;
        }
        if IGNORE_UNSET_PROT_EXEC.load(Ordering::Relaxed) {
            prot |= PROT_EXEC;
        }
        if prot & !PROT_MASK != 0 { return Err(EINVAL); }
        if prot & !inner.prot_mask != 0 { return Err(EINVAL); }
        inner.prot_mask = prot;
        Ok(0)
    }

    // ── ashmem.rs:373 — get_prot_mask ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_get_prot_mask(self: Pin<&Self>) -> Result<isize> {
        let inner = unsafe { self.lock() };
        Ok(inner.prot_mask as isize)
    }

    // ── ashmem.rs:389 — pin_unpin (accesses NUM_PIN_IOCTLS_WAITING) ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
        let inner = unsafe { self.lock() };
        if inner.size == 0 { return Err(EINVAL); }

        NUM_PIN_IOCTLS_WAITING.fetch_add(1, Ordering::Relaxed);

        let mut pin_buf = [0u8; 8];
        reader.read_all(&mut pin_buf)?;

        let _offset = u32::from_ne_bytes([pin_buf[0], pin_buf[1], pin_buf[2], pin_buf[3]]);
        let _len = u32::from_ne_bytes([pin_buf[4], pin_buf[5], pin_buf[6], pin_buf[7]]);

        NUM_PIN_IOCTLS_WAITING.fetch_sub(1, Ordering::Relaxed);

        match cmd {
            0x40087707 => Ok(0), // PIN
            0x40087708 => Ok(0), // UNPIN
            0x00007709 => Ok(0), // GET_PIN_STATUS
            _ => Err(EINVAL),
        }
    }

    // ── ashmem.rs:257 — main ioctl dispatch ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_ioctl(me: Pin<&Ashmem>, cmd: u32, arg: usize) -> Result<isize> {
        let size = ioc_size(cmd);
        match cmd {
            0x41007701 => me.ashmem_set_name(user_slice_reader(arg, size)),
            0x81007702 => me.ashmem_get_name(user_slice_writer(arg, size)),
            0x40087703 => me.ashmem_set_size(arg),
            0x00007704 => me.ashmem_get_size(),
            0x40047705 => me.ashmem_set_prot_mask(arg),
            0x00007706 => me.ashmem_get_prot_mask(),
            0x40087707 | 0x40087708 | 0x00007709 => {
                me.ashmem_pin_unpin(cmd, user_slice_reader(arg, size))
            }
            0x0000770A => Err(EPERM),
            _ => Err(ENOTTY),
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// ashmem.rs:78 — reads NUM_PIN_IOCTLS_WAITING
// ═══════════════════════════════════════════════════════════════

#[inline(never)]
#[no_mangle]
pub fn shrinker_should_stop() -> bool {
    NUM_PIN_IOCTLS_WAITING.load(Ordering::Relaxed) > 0
}

// ═══════════════════════════════════════════════════════════════
// ashmem.rs:506 — reads ASHMEM_FOPS_PTR
// ═══════════════════════════════════════════════════════════════

#[no_mangle]
pub unsafe extern "C" fn is_ashmem_file(file: *mut u8) -> bool {
    let fops_ptr = ASHMEM_FOPS_PTR.load(Ordering::Relaxed);
    if file.is_null() || fops_ptr.is_null() {
        return false;
    }
    file == fops_ptr
}

// ═══════════════════════════════════════════════════════════════
// Shrinker callbacks — access LRU_COUNT
// ═══════════════════════════════════════════════════════════════

#[inline(never)]
#[no_mangle]
pub fn ashmem_shrink_count() -> usize {
    LRU_COUNT.load(Ordering::Relaxed)
}

#[inline(never)]
#[no_mangle]
pub fn ashmem_shrink_scan(nr: usize) -> usize {
    if shrinker_should_stop() { return 0; }
    let count = LRU_COUNT.load(Ordering::Relaxed);
    let scanned = core::cmp::min(nr, count);
    LRU_COUNT.fetch_sub(scanned, Ordering::Relaxed);
    scanned
}

// ═══════════════════════════════════════════════════════════════
// Force all statics and functions to be emitted
// ═══════════════════════════════════════════════════════════════

#[inline(never)]
#[no_mangle]
pub fn force_emit_all() -> isize {
    let ashmem = Ashmem {
        inner: UnsafeCell::new(AshmemInner {
            size: 0,
            prot_mask: PROT_MASK,
            name: None,
            name_len: 0,
            mapped: false,
        }),
    };

    let pinned = unsafe { Pin::new_unchecked(&ashmem) };
    let mut r: isize = 0;

    ASHMEM_FOPS_PTR.store(0x1000 as *mut u8, Ordering::Relaxed);

    for &(cmd, arg) in &[
        (0x41007701u32, 0x2000usize),
        (0x81007702, 0x3000),
        (0x40087703, 4096),
        (0x00007704, 0),
        (0x40047705, 0x3),
        (0x00007706, 0),
        (0x40087707, 0x4000),
        (0x40087708, 0x5000),
        (0x00007709, 0x6000),
        (0x0000770A, 0),
        (0xDEAD, 0),
    ] {
        match Ashmem::ashmem_ioctl(pinned, cmd, arg) {
            Ok(v) => r = r.wrapping_add(v),
            Err(e) => r = r.wrapping_add(e as isize),
        }
    }

    r = r.wrapping_add(shrinker_should_stop() as isize);
    r = r.wrapping_add(ashmem_shrink_count() as isize);
    r = r.wrapping_add(ashmem_shrink_scan(10) as isize);
    r = r.wrapping_add(unsafe { is_ashmem_file(null_mut()) } as isize);

    r
}
