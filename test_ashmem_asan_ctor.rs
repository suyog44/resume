//! Standalone reproducer for `asan.module_ctor` frame pointer issue.
//!
//! Mirrors the exact global statics from `drivers/staging/android/ashmem.rs`
//! (`android16-6.12`) that trigger LLVM `asan.module_ctor` generation under
//! `CONFIG_KASAN_GENERIC=y`.
//!
//! # Build
//!
//! Use the companion `build_ashmem_test.sh` script, or manually:
//!
//! ```text
//! rustc --edition=2021 --crate-type=lib \
//!   -Zbinary_dep_depinfo=y -Astable_features \
//!   -Dnon_ascii_idents -Dunsafe_op_in_unsafe_fn \
//!   -Wmissing_docs -Wrust_2018_idioms -Wunreachable_pub \
//!   -Wclippy::all -Wclippy::ignored_unit_patterns \
//!   -Wclippy::mut_mut -Wclippy::needless_bitwise_bool \
//!   -Aclippy::needless_lifetimes \
//!   -Wclippy::no_mangle_with_rust_abi \
//!   -Wclippy::undocumented_unsafe_blocks \
//!   -Wclippy::unnecessary_safety_comment \
//!   -Wclippy::unnecessary_safety_doc \
//!   -Wrustdoc::missing_crate_level_docs \
//!   -Wrustdoc::unescaped_backticks \
//!   -Cpanic=abort -Cembed-bitcode=n -Clto=n \
//!   -Cforce-unwind-tables=n -Ccodegen-units=1 \
//!   -Csymbol-mangling-version=v0 -Crelocation-model=static \
//!   -Zfunction-sections=n -Wclippy::float_arithmetic \
//!   --target=aarch64-unknown-none \
//!   -Ctarget-feature="-neon" \
//!   -Cforce-unwind-tables=y -Zuse-sync-unwind=n \
//!   -Zbranch-protection=pac-ret -Zfixed-x18 \
//!   -Cforce-frame-pointers=yes \
//!   -Zsanitizer=kernel-address \
//!   -o kasan_fp.o --emit=obj test_ashmem_asan_ctor.rs
//! ```

// Suppress all lints enabled by KBUILD_RUSTFLAGS that do not apply
// to a standalone test file outside the kernel tree.
#![no_std]
#![allow(missing_docs)]
#![allow(clippy::undocumented_unsafe_blocks)]
#![allow(clippy::unnecessary_safety_comment)]
#![allow(clippy::unnecessary_safety_doc)]

use core::cell::UnsafeCell;
use core::pin::Pin;
use core::ptr::null_mut;
use core::sync::atomic::{AtomicBool, AtomicPtr, AtomicUsize, Ordering};

/// Panic handler required for `no_std` crates.
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

// ─── Exact statics from ashmem.rs:73-76 ─────────────────────
// These are what trigger `asan.module_ctor` generation: LLVM's
// AddressSanitizer pass must register each global with the KASAN
// shadow memory by synthesising a module constructor.

static NUM_PIN_IOCTLS_WAITING: AtomicUsize = AtomicUsize::new(0);
static IGNORE_UNSET_PROT_READ: AtomicBool = AtomicBool::new(false);
static IGNORE_UNSET_PROT_EXEC: AtomicBool = AtomicBool::new(false);
static ASHMEM_FOPS_PTR: AtomicPtr<u8> = AtomicPtr::new(null_mut());
static LRU_COUNT: AtomicUsize = AtomicUsize::new(0);

// ─── Constants matching ashmem.rs ────────────────────────────

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

// ─── Simulated kernel uaccess ────────────────────────────────

struct UserSliceReader {
    addr: usize,
    remaining: usize,
}

struct UserSliceWriter {
    addr: usize,
    remaining: usize,
}

impl UserSliceReader {
    #[inline(never)]
    fn read_all(self, buf: &mut [u8]) -> Result<()> {
        let len = core::cmp::min(self.remaining, buf.len());
        let mut i = 0;
        while i < len {
            buf[i] = unsafe { core::ptr::read_volatile((self.addr + i) as *const u8) };
            i += 1;
        }
        Ok(())
    }
}

impl UserSliceWriter {
    #[inline(never)]
    fn write_all(self, buf: &[u8]) -> Result<()> {
        let len = core::cmp::min(self.remaining, buf.len());
        let mut i = 0;
        while i < len {
            unsafe { core::ptr::write_volatile((self.addr + i) as *mut u8, buf[i]) };
            i += 1;
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

fn ioc_size(cmd: u32) -> usize {
    ((cmd >> 16) & 0x3FFF) as usize
}

// ─── AshmemInner ─────────────────────────────────────────────

struct AshmemInner {
    size: usize,
    prot_mask: usize,
    name: Option<[u8; ASHMEM_NAME_LEN]>,
    name_len: usize,
    mapped: bool,
}

// ─── Ashmem ──────────────────────────────────────────────────
// Real ashmem.rs uses `Mutex<AshmemInner>` which wraps UnsafeCell
// internally. We use UnsafeCell directly for the standalone build.

struct Ashmem {
    inner: UnsafeCell<AshmemInner>,
}

unsafe impl Sync for Ashmem {}
unsafe impl Send for Ashmem {}

impl Ashmem {
    #[inline(always)]
    unsafe fn lock(&self) -> &mut AshmemInner {
        unsafe { &mut *self.inner.get() }
    }

    // ── ashmem.rs:300 — set_name ──
    #[inline(never)]
    fn set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
        let inner = unsafe { self.lock() };
        if inner.mapped {
            return Err(EINVAL);
        }
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
    fn get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
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
    fn set_size(self: Pin<&Self>, size: usize) -> Result<isize> {
        let inner = unsafe { self.lock() };
        if inner.mapped {
            return Err(EINVAL);
        }
        if size == 0 || size >= ASHMEM_MAX_SIZE {
            return Err(EINVAL);
        }
        inner.size = size;
        Ok(0)
    }

    // ── ashmem.rs:343 — get_size ──
    #[inline(never)]
    fn get_size(self: Pin<&Self>) -> Result<isize> {
        let inner = unsafe { self.lock() };
        Ok(inner.size as isize)
    }

    // ── ashmem.rs:347 — set_prot_mask (reads module statics) ──
    #[inline(never)]
    fn set_prot_mask(self: Pin<&Self>, mut prot: usize) -> Result<isize> {
        let inner = unsafe { self.lock() };
        if IGNORE_UNSET_PROT_READ.load(Ordering::Relaxed) {
            prot |= PROT_READ;
        }
        if IGNORE_UNSET_PROT_EXEC.load(Ordering::Relaxed) {
            prot |= PROT_EXEC;
        }
        if prot & !PROT_MASK != 0 {
            return Err(EINVAL);
        }
        if prot & !inner.prot_mask != 0 {
            return Err(EINVAL);
        }
        inner.prot_mask = prot;
        Ok(0)
    }

    // ── ashmem.rs:373 — get_prot_mask ──
    #[inline(never)]
    fn get_prot_mask(self: Pin<&Self>) -> Result<isize> {
        let inner = unsafe { self.lock() };
        Ok(inner.prot_mask as isize)
    }

    // ── ashmem.rs:389 — pin_unpin (accesses NUM_PIN_IOCTLS_WAITING) ──
    #[inline(never)]
    fn pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
        let inner = unsafe { self.lock() };
        if inner.size == 0 {
            return Err(EINVAL);
        }

        NUM_PIN_IOCTLS_WAITING.fetch_add(1, Ordering::Relaxed);

        let mut pin_buf = [0u8; 8];
        reader.read_all(&mut pin_buf)?;

        let _offset = u32::from_ne_bytes([pin_buf[0], pin_buf[1], pin_buf[2], pin_buf[3]]);
        let _len = u32::from_ne_bytes([pin_buf[4], pin_buf[5], pin_buf[6], pin_buf[7]]);

        NUM_PIN_IOCTLS_WAITING.fetch_sub(1, Ordering::Relaxed);

        match cmd {
            0x4008_7707 => Ok(0),
            0x4008_7708 => Ok(0),
            0x0000_7709 => Ok(0),
            _ => Err(EINVAL),
        }
    }

    // ── ashmem.rs:257 — main ioctl dispatch ──
    #[inline(never)]
    fn ioctl(me: Pin<&Self>, cmd: u32, arg: usize) -> Result<isize> {
        let size = ioc_size(cmd);
        match cmd {
            0x4100_7701 => me.set_name(user_slice_reader(arg, size)),
            0x8100_7702 => me.get_name(user_slice_writer(arg, size)),
            0x4008_7703 => me.set_size(arg),
            0x0000_7704 => me.get_size(),
            0x4004_7705 => me.set_prot_mask(arg),
            0x0000_7706 => me.get_prot_mask(),
            0x4008_7707 | 0x4008_7708 | 0x0000_7709 => {
                me.pin_unpin(cmd, user_slice_reader(arg, size))
            }
            0x0000_770A => Err(EPERM),
            _ => Err(ENOTTY),
        }
    }
}

// ─── Free functions matching ashmem.rs ───────────────────────

#[inline(never)]
fn shrinker_should_stop() -> bool {
    NUM_PIN_IOCTLS_WAITING.load(Ordering::Relaxed) > 0
}

/// FFI export — mirrors `ashmem.rs:506`.
#[no_mangle]
pub unsafe extern "C" fn is_ashmem_file(file: *mut u8) -> bool {
    let fops_ptr = ASHMEM_FOPS_PTR.load(Ordering::Relaxed);
    if file.is_null() || fops_ptr.is_null() {
        return false;
    }
    file == fops_ptr
}

#[inline(never)]
fn shrink_count() -> usize {
    LRU_COUNT.load(Ordering::Relaxed)
}

#[inline(never)]
fn shrink_scan(nr: usize) -> usize {
    if shrinker_should_stop() {
        return 0;
    }
    let count = LRU_COUNT.load(Ordering::Relaxed);
    let scanned = core::cmp::min(nr, count);
    LRU_COUNT.fetch_sub(scanned, Ordering::Relaxed);
    scanned
}

// ─── Entry point — forces all statics and functions live ─────

/// FFI export — ensures all globals are reachable so KASAN
/// synthesises `asan.module_ctor` to register them.
#[no_mangle]
pub extern "C" fn force_emit_all() -> isize {
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

    let cmds: [(u32, usize); 11] = [
        (0x4100_7701, 0x2000),
        (0x8100_7702, 0x3000),
        (0x4008_7703, 4096),
        (0x0000_7704, 0),
        (0x4004_7705, 0x3),
        (0x0000_7706, 0),
        (0x4008_7707, 0x4000),
        (0x4008_7708, 0x5000),
        (0x0000_7709, 0x6000),
        (0x0000_770A, 0),
        (0xDEAD, 0),
    ];

    let mut i = 0;
    while i < cmds.len() {
        let (cmd, arg) = cmds[i];
        match Ashmem::ioctl(pinned, cmd, arg) {
            Ok(v) => r = r.wrapping_add(v),
            Err(e) => r = r.wrapping_add(e as isize),
        }
        i += 1;
    }

    r = r.wrapping_add(shrinker_should_stop() as isize);
    r = r.wrapping_add(shrink_count() as isize);
    r = r.wrapping_add(shrink_scan(10) as isize);
    r = r.wrapping_add(unsafe { is_ashmem_file(null_mut()) } as isize);

    r
}
