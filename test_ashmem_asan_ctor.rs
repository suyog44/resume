// test_ashmem_asan_ctor.rs
//
// Standalone reproducer mirroring the exact global statics from
// drivers/staging/android/ashmem.rs (android16-6.12) that trigger
// LLVM's asan.module_ctor generation when built with KASAN.
//
// ashmem.rs lines 73-76 define these module-level statics:
//
//   static NUM_PIN_IOCTLS_WAITING: AtomicUsize = AtomicUsize::new(0);
//   static IGNORE_UNSET_PROT_READ: AtomicBool  = AtomicBool::new(false);
//   static IGNORE_UNSET_PROT_EXEC: AtomicBool  = AtomicBool::new(false);
//   static ASHMEM_FOPS_PTR: AtomicPtr<file_operations> = AtomicPtr::new(null_mut());
//
// When KASAN instruments this CGU, LLVM creates asan.module_ctor to
// call __asan_register_globals() for these statics. The synthesized
// asan.module_ctor does NOT inherit "frame-pointer"="all", emitting
// str x30 instead of stp x29, x30.
//
// This file cannot use `kernel::` crate imports, so it reproduces
// the same types and access patterns with core:: equivalents.
//
// ═══════════════════════════════════════════════════════════════
// BUILD — three variants to compare:
// ═══════════════════════════════════════════════════════════════
//
// 1. Baseline (no KASAN) — no asan.module_ctor exists:
//
//    rustc +nightly --edition=2021 --crate-type=lib \
//      --target=aarch64-unknown-none -Cpanic=abort \
//      -Copt-level=2 -Crelocation-model=static \
//      -Cembed-bitcode=n -Clto=n -Ccodegen-units=1 \
//      -Cforce-unwind-tables=yes \
//      -Cforce-frame-pointers=yes \
//      -Zbranch-protection=pac-ret -Zfixed-x18 \
//      -o baseline.o --emit=obj test_ashmem_asan_ctor.rs
//
// 2. KASAN + frame pointers (tests if asan.module_ctor inherits FP):
//
//    rustc +nightly --edition=2021 --crate-type=lib \
//      --target=aarch64-unknown-none -Cpanic=abort \
//      -Copt-level=2 -Crelocation-model=static \
//      -Cembed-bitcode=n -Clto=n -Ccodegen-units=1 \
//      -Cforce-unwind-tables=yes \
//      -Cforce-frame-pointers=yes \
//      -Zbranch-protection=pac-ret -Zfixed-x18 \
//      -Zsanitizer=kernel-address \
//      -o kasan_fp.o --emit=obj test_ashmem_asan_ctor.rs
//
// 3. KASAN without frame pointers (current broken KBUILD_RUSTFLAGS):
//
//    rustc +nightly --edition=2021 --crate-type=lib \
//      --target=aarch64-unknown-none -Cpanic=abort \
//      -Copt-level=2 -Crelocation-model=static \
//      -Cembed-bitcode=n -Clto=n -Ccodegen-units=1 \
//      -Cforce-unwind-tables=yes \
//      -Zbranch-protection=pac-ret -Zfixed-x18 \
//      -Zsanitizer=kernel-address \
//      -o kasan_nofp.o --emit=obj test_ashmem_asan_ctor.rs
//
// ═══════════════════════════════════════════════════════════════
// CHECK:
// ═══════════════════════════════════════════════════════════════
//
//   for f in baseline.o kasan_fp.o kasan_nofp.o; do
//     echo "=== $f ==="
//     llvm-objdump -d $f | \
//       awk '/^[0-9a-f]+ <.*>:/{fn=$0} /str.*x30, \[sp/{print fn; print "  "$0}'
//     echo
//   done
//
// EXPECTED:
//   baseline.o  : zero str x30 (no asan.module_ctor at all)
//   kasan_fp.o  : str x30 ONLY in asan.module_ctor (LLVM bug)
//   kasan_nofp.o: str x30 in asan.module_ctor AND user functions
//
// ═══════════════════════════════════════════════════════════════
// IR CHECK (confirms attribute mismatch):
// ═══════════════════════════════════════════════════════════════
//
//   rustc +nightly --edition=2021 --crate-type=lib \
//     --target=aarch64-unknown-none -Cpanic=abort \
//     -Copt-level=2 -Crelocation-model=static \
//     -Cembed-bitcode=n -Clto=n -Ccodegen-units=1 \
//     -Cforce-unwind-tables=yes \
//     -Cforce-frame-pointers=yes \
//     -Zbranch-protection=pac-ret -Zfixed-x18 \
//     -Zsanitizer=kernel-address \
//     --emit=llvm-ir -o kasan.ll test_ashmem_asan_ctor.rs
//
//   # User functions have frame-pointer=all:
//   grep '"frame-pointer"="all"' kasan.ll
//
//   # asan.module_ctor definition — check its attribute group:
//   grep -A2 'define.*asan.module_ctor' kasan.ll
//
//   # That attribute group does NOT contain frame-pointer:
//   # (get the #N from the asan.module_ctor line, then check)
//   grep '^attributes #' kasan.ll

#![no_std]
#![allow(dead_code, unused_variables)]

use core::{
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
//
// These are what trigger asan.module_ctor generation.
// KASAN must register each global with the shadow memory.
// ═══════════════════════════════════════════════════════════════

/// ashmem.rs:73 — guards shrinker_should_stop()
static NUM_PIN_IOCTLS_WAITING: AtomicUsize = AtomicUsize::new(0);

/// ashmem.rs:74 — toggle for prot_read enforcement
static IGNORE_UNSET_PROT_READ: AtomicBool = AtomicBool::new(false);

/// ashmem.rs:75 — toggle for prot_exec enforcement
static IGNORE_UNSET_PROT_EXEC: AtomicBool = AtomicBool::new(false);

/// ashmem.rs:76 — stores fops pointer for is_ashmem_file()
/// Using *mut u8 instead of *mut bindings::file_operations
/// since we can't reference kernel types standalone.
static ASHMEM_FOPS_PTR: AtomicPtr<u8> = AtomicPtr::new(null_mut());

// ═══════════════════════════════════════════════════════════════
// Additional statics from sub-modules referenced by ashmem.rs
// (ashmem_range module has LRU_COUNT etc.)
// ═══════════════════════════════════════════════════════════════

/// From ashmem_range.rs — LRU count for shrinker
static LRU_COUNT: AtomicUsize = AtomicUsize::new(0);

// ═══════════════════════════════════════════════════════════════
// Constants matching ashmem.rs lines 37-48
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

// ═══════════════════════════════════════════════════════════════
// Simulated kernel types — minimal stand-ins for kernel crate
// ═══════════════════════════════════════════════════════════════

type Result<T> = core::result::Result<T, i32>;
const EINVAL: i32 = -22;
const ENOTTY: i32 = -25;
const EPERM: i32 = -1;

/// Stand-in for kernel::uaccess::UserSliceReader
struct UserSliceReader { addr: usize, remaining: usize }
/// Stand-in for kernel::uaccess::UserSliceWriter
struct UserSliceWriter { addr: usize, remaining: usize }

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
// Pin<&Self> receiver on all methods, Mutex<AshmemInner> inside
// ═══════════════════════════════════════════════════════════════

pub struct Ashmem {
    inner: AshmemInner, // Simplified: no Mutex in standalone
}

impl Ashmem {
    // ── ashmem.rs:300 ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
        let me = unsafe { &mut *(self.get_ref() as *const Ashmem as *mut Ashmem) };
        let inner = &mut me.inner;
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

    // ── ashmem.rs:315 ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
        let inner = &self.get_ref().inner;
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

    // ── ashmem.rs:334 ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_set_size(self: Pin<&Self>, size: usize) -> Result<isize> {
        let me = unsafe { &mut *(self.get_ref() as *const Ashmem as *mut Ashmem) };
        let inner = &mut me.inner;
        if inner.mapped { return Err(EINVAL); }
        if size == 0 || size >= ASHMEM_MAX_SIZE { return Err(EINVAL); }
        inner.size = size;
        Ok(0)
    }

    // ── ashmem.rs:343 ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_get_size(&self) -> Result<isize> {
        Ok(self.inner.size as isize)
    }

    // ── ashmem.rs:347 ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_set_prot_mask(self: Pin<&Self>, mut prot: usize) -> Result<isize> {
        let me = unsafe { &mut *(self.get_ref() as *const Ashmem as *mut Ashmem) };
        let inner = &mut me.inner;

        // Mirrors ashmem.rs:350-363 — toggle checks using the module statics
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

    // ── ashmem.rs:373 ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_get_prot_mask(&self) -> Result<isize> {
        Ok(self.inner.prot_mask as isize)
    }

    // ── ashmem.rs:389 — pin_unpin with UserSliceReader ──
    #[inline(never)]
    #[no_mangle]
    pub fn ashmem_pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
        let inner = &self.get_ref().inner;
        if inner.size == 0 { return Err(EINVAL); }

        // Increment/decrement the waiting counter (ashmem.rs uses this
        // to signal the shrinker to stop)
        NUM_PIN_IOCTLS_WAITING.fetch_add(1, Ordering::Relaxed);

        let mut pin_buf = [0u8; 8];
        reader.read_all(&mut pin_buf)?;

        let offset = u32::from_ne_bytes([pin_buf[0], pin_buf[1], pin_buf[2], pin_buf[3]]);
        let len = u32::from_ne_bytes([pin_buf[4], pin_buf[5], pin_buf[6], pin_buf[7]]);

        NUM_PIN_IOCTLS_WAITING.fetch_sub(1, Ordering::Relaxed);

        // Simplified — real code does range tracking
        match cmd {
            0x77_07 => Ok(0), // PIN
            0x77_08 => Ok(0), // UNPIN
            0x77_09 => Ok(0), // GET_PIN_STATUS
            _ => Err(EINVAL),
        }
    }

    // ── ashmem.rs:257 — the main ioctl dispatch ──
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
            0x0000770A => {
                // PURGE_ALL_CACHES — just return EPERM like ashmem.rs
                Err(EPERM)
            }
            _ => Err(ENOTTY),
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// ashmem.rs:78 — shrinker_should_stop() reads NUM_PIN_IOCTLS_WAITING
// ═══════════════════════════════════════════════════════════════

#[inline(never)]
#[no_mangle]
pub fn shrinker_should_stop() -> bool {
    NUM_PIN_IOCTLS_WAITING.load(Ordering::Relaxed) > 0
}

// ═══════════════════════════════════════════════════════════════
// ashmem.rs:506 — is_ashmem_file() reads ASHMEM_FOPS_PTR
// ═══════════════════════════════════════════════════════════════

#[no_mangle]
pub unsafe extern "C" fn is_ashmem_file(file: *mut u8) -> bool {
    let fops_ptr = ASHMEM_FOPS_PTR.load(Ordering::Relaxed);
    if file.is_null() || fops_ptr.is_null() {
        return false;
    }
    // Simulated: compare fops pointers
    file == fops_ptr
}

// ═══════════════════════════════════════════════════════════════
// LRU_COUNT access — mirrors ashmem_range shrinker callbacks
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
        inner: AshmemInner {
            size: 0,
            prot_mask: PROT_MASK,
            name: None,
            name_len: 0,
            mapped: false,
        },
    };

    let pinned = unsafe { Pin::new_unchecked(&ashmem) };
    let mut r: isize = 0;

    // Store a fake fops ptr (mirrors ashmem.rs:121)
    ASHMEM_FOPS_PTR.store(0x1000 as *mut u8, Ordering::Relaxed);

    // Exercise every ioctl path
    for &(cmd, arg) in &[
        (0x41007701u32, 0x2000usize), // SET_NAME
        (0x81007702, 0x3000),          // GET_NAME
        (0x40087703, 4096),            // SET_SIZE
        (0x00007704, 0),               // GET_SIZE
        (0x40047705, 0x3),             // SET_PROT_MASK
        (0x00007706, 0),               // GET_PROT_MASK
        (0x40087707, 0x4000),          // PIN
        (0x40087708, 0x5000),          // UNPIN
        (0x00007709, 0x6000),          // GET_PIN_STATUS
        (0x0000770A, 0),               // PURGE_ALL_CACHES
        (0xDEAD, 0),                   // unknown
    ] {
        match Ashmem::ashmem_ioctl(pinned, cmd, arg) {
            Ok(v) => r = r.wrapping_add(v),
            Err(e) => r = r.wrapping_add(e as isize),
        }
    }

    // Access all statics to ensure KASAN must register them
    r = r.wrapping_add(shrinker_should_stop() as isize);
    r = r.wrapping_add(ashmem_shrink_count() as isize);
    r = r.wrapping_add(ashmem_shrink_scan(10) as isize);
    r = r.wrapping_add(unsafe { is_ashmem_file(null_mut()) } as isize);

    r
}
