rustc  : rustc 1.82.0-dev (f6e511eec 2024-10-15) (Android Rust Toolchain version linux-12909517)
objdump: llvm-objdump
source : ./test_ashmem_asan_ctor.rs

═══════════════════════════════════════════════════════════
  Build 1: BASELINE — no KASAN, with -Cforce-frame-pointers=yes
═══════════════════════════════════════════════════════════
warning: `extern` fn uses type `UserSliceReader`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:208:54
    |
208 |     extern "C" fn set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:125:1
    |
125 | pub struct UserSliceReader {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^
    = note: `#[warn(improper_ctypes_definitions)]` on by default

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:208:74
    |
208 |     extern "C" fn set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
    |                                                                          ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `UserSliceWriter`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:229:54
    |
229 |     extern "C" fn get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:130:1
    |
130 | pub struct UserSliceWriter {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:229:74
    |
229 |     extern "C" fn get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
    |                                                                          ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:251:62
    |
251 |     extern "C" fn set_size(self: Pin<&Self>, size: usize) -> Result<isize> {
    |                                                              ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:266:49
    |
266 |     extern "C" fn get_size(self: Pin<&Self>) -> Result<isize> {
    |                                                 ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:274:71
    |
274 |     extern "C" fn set_prot_mask(self: Pin<&Self>, mut prot: usize) -> Result<isize> {
    |                                                                       ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:296:54
    |
296 |     extern "C" fn get_prot_mask(self: Pin<&Self>) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `UserSliceReader`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:304:65
    |
304 |     extern "C" fn pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
    |                                                                 ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:125:1
    |
125 | pub struct UserSliceReader {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:304:85
    |
304 |     extern "C" fn pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
    |                                                                                     ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:331:66
    |
331 |     extern "C" fn ioctl(me: Pin<&Self>, cmd: u32, arg: usize) -> Result<isize> {
    |                                                                  ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: 11 warnings emitted

  OK → /tmp/tmp.P6Y454sP82/baseline.o

═══════════════════════════════════════════════════════════
  Build 2: KASAN + -Cforce-frame-pointers=yes (proposed fix)
═══════════════════════════════════════════════════════════
warning: `extern` fn uses type `UserSliceReader`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:208:54
    |
208 |     extern "C" fn set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:125:1
    |
125 | pub struct UserSliceReader {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^
    = note: `#[warn(improper_ctypes_definitions)]` on by default

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:208:74
    |
208 |     extern "C" fn set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
    |                                                                          ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `UserSliceWriter`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:229:54
    |
229 |     extern "C" fn get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:130:1
    |
130 | pub struct UserSliceWriter {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:229:74
    |
229 |     extern "C" fn get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
    |                                                                          ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:251:62
    |
251 |     extern "C" fn set_size(self: Pin<&Self>, size: usize) -> Result<isize> {
    |                                                              ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:266:49
    |
266 |     extern "C" fn get_size(self: Pin<&Self>) -> Result<isize> {
    |                                                 ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:274:71
    |
274 |     extern "C" fn set_prot_mask(self: Pin<&Self>, mut prot: usize) -> Result<isize> {
    |                                                                       ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:296:54
    |
296 |     extern "C" fn get_prot_mask(self: Pin<&Self>) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `UserSliceReader`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:304:65
    |
304 |     extern "C" fn pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
    |                                                                 ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:125:1
    |
125 | pub struct UserSliceReader {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:304:85
    |
304 |     extern "C" fn pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
    |                                                                                     ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:331:66
    |
331 |     extern "C" fn ioctl(me: Pin<&Self>, cmd: u32, arg: usize) -> Result<isize> {
    |                                                                  ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: 11 warnings emitted

  OK → /tmp/tmp.P6Y454sP82/kasan_fp.o

═══════════════════════════════════════════════════════════
  Build 3: KASAN WITHOUT -Cforce-frame-pointers (current broken)
═══════════════════════════════════════════════════════════
warning: `extern` fn uses type `UserSliceReader`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:208:54
    |
208 |     extern "C" fn set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:125:1
    |
125 | pub struct UserSliceReader {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^
    = note: `#[warn(improper_ctypes_definitions)]` on by default

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:208:74
    |
208 |     extern "C" fn set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
    |                                                                          ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `UserSliceWriter`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:229:54
    |
229 |     extern "C" fn get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:130:1
    |
130 | pub struct UserSliceWriter {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:229:74
    |
229 |     extern "C" fn get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
    |                                                                          ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:251:62
    |
251 |     extern "C" fn set_size(self: Pin<&Self>, size: usize) -> Result<isize> {
    |                                                              ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:266:49
    |
266 |     extern "C" fn get_size(self: Pin<&Self>) -> Result<isize> {
    |                                                 ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:274:71
    |
274 |     extern "C" fn set_prot_mask(self: Pin<&Self>, mut prot: usize) -> Result<isize> {
    |                                                                       ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:296:54
    |
296 |     extern "C" fn get_prot_mask(self: Pin<&Self>) -> Result<isize> {
    |                                                      ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `UserSliceReader`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:304:65
    |
304 |     extern "C" fn pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
    |                                                                 ^^^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]` or `#[repr(transparent)]` attribute to this struct
    = note: this struct has unspecified layout
note: the type is defined here
   --> ./test_ashmem_asan_ctor.rs:125:1
    |
125 | pub struct UserSliceReader {
    | ^^^^^^^^^^^^^^^^^^^^^^^^^^

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:304:85
    |
304 |     extern "C" fn pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
    |                                                                                     ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: `extern` fn uses type `core::result::Result<isize, i32>`, which is not FFI-safe
   --> ./test_ashmem_asan_ctor.rs:331:66
    |
331 |     extern "C" fn ioctl(me: Pin<&Self>, cmd: u32, arg: usize) -> Result<isize> {
    |                                                                  ^^^^^^^^^^^^^ not FFI-safe
    |
    = help: consider adding a `#[repr(C)]`, `#[repr(transparent)]`, or integer `#[repr(...)]` attribute to this enum
    = note: enum has no representation hint

warning: 11 warnings emitted

  OK → /tmp/tmp.P6Y454sP82/kasan_nofp.o


═══════════════════════════════════════════════════════════
  BASELINE — no KASAN, with frame pointers
═══════════════════════════════════════════════════════════

  Total functions          : 56
  paciasp (PAC-RET)        : 0
  stp x29, x30 (GOOD)     : 56
  str x30       (BAD)     : 0
  asan.module_ctor present : NO

═══════════════════════════════════════════════════════════
  KASAN WITHOUT -Cforce-frame-pointers (current build)
═══════════════════════════════════════════════════════════

  Total functions          : 58
  paciasp (PAC-RET)        : 0
  stp x29, x30 (GOOD)     : 12
  str x30       (BAD)     : 39
  asan.module_ctor present : YES

  *** str x30 sites: ***
    0000000000000000 <_RINvNtCsgc5Frn9pZ9V_4core3cmp3minjECsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
             4: fe 0f 1f f8  	str	x30, [sp, #-16]!
    00000000000001bc <_RINvNtCsgc5Frn9pZ9V_4core3ptr13read_volatilehECsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
           1c4: fe 13 00 f9  	str	x30, [sp, #32]
    0000000000000254 <_RINvNtCsgc5Frn9pZ9V_4core3ptr14write_volatilehECsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
           25c: fe 13 00 f9  	str	x30, [sp, #32]
    00000000000011b4 <_RNvMs1e_NtNtCsgc5Frn9pZ9V_4core4sync6atomicNtB6_11AtomicUsize4loadCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          11b8: fe 0f 1f f8  	str	x30, [sp, #-16]!
    00000000000011cc <_RNvMs1e_NtNtCsgc5Frn9pZ9V_4core4sync6atomicNtB6_11AtomicUsize9fetch_addCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          11d4: fe 53 00 f9  	str	x30, [sp, #160]
    00000000000014e8 <_RNvMs1e_NtNtCsgc5Frn9pZ9V_4core4sync6atomicNtB6_11AtomicUsize9fetch_subCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          14f0: fe 53 00 f9  	str	x30, [sp, #160]
    0000000000001804 <_RNvMs3_NtNtCsgc5Frn9pZ9V_4core4sync6atomicNtB5_10AtomicBool4loadCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1808: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000001824 <_RNvMs4_NtNtCsgc5Frn9pZ9V_4core4sync6atomicINtB5_9AtomicPtrhE4loadCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1828: fe 0f 1f f8  	str	x30, [sp, #-16]!
    000000000000183c <_RNvMs4_NtNtCsgc5Frn9pZ9V_4core4sync6atomicINtB5_9AtomicPtrhE5storeCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1840: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000001854 <_RNvMs6_NtCsgc5Frn9pZ9V_4core3numm13from_ne_bytesCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          185c: fe 0b 00 f9  	str	x30, [sp, #16]
    00000000000018a8 <_RNvNvMs9_NtCsgc5Frn9pZ9V_4core3numj13unchecked_add18precondition_checkCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          18ac: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000001d64 <_RNvXNtNtCsgc5Frn9pZ9V_4core3ops5derefRNtCsd8Bjm28kwoH_21test_ashmem_asan_ctor6AshmemNtB2_5Deref5derefBC_>:
          1d6c: fe 0b 00 f9  	str	x30, [sp, #16]
    0000000000001db4 <_RNvXs2_NtNtCsgc5Frn9pZ9V_4core5slice5indexINtNtNtB9_3ops5range5RangejEINtB5_10SliceIndexShE9index_mutCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1dbc: fe 23 00 f9  	str	x30, [sp, #64]
    0000000000001e58 <_RNvXs3_NtNtCsgc5Frn9pZ9V_4core4iter5rangeINtNtNtB9_3ops5range5RangejENtB5_17RangeIteratorImpl9spec_nextCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1e60: fe 33 00 f9  	str	x30, [sp, #96]
    0000000000001fa4 <_RNvXs4_NtNtCsgc5Frn9pZ9V_4core4iter5rangeINtNtNtB9_3ops5range5RangejENtNtNtB7_6traits8iterator8Iterator4nextCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1fa8: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000001fbc <_RNvXs4_NtNtCsgc5Frn9pZ9V_4core5slice5indexINtNtNtB9_3ops5range7RangeTojEINtB5_10SliceIndexShE9index_mutCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1fc0: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000001fe8 <_RNvXsF_NtNtCsgc5Frn9pZ9V_4core4iter5rangejNtB5_4Step17forward_uncheckedCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          1ff0: fe 0b 00 f9  	str	x30, [sp, #16]
    000000000000202c <_RNvXsV_NtNtCsgc5Frn9pZ9V_4core3cmp5implsjNtB7_3Ord3cmpCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          2034: fe 1b 00 f9  	str	x30, [sp, #48]
    00000000000020d0 <_RNvXsb_NtCsgc5Frn9pZ9V_4core3pinINtB5_3PinRNtCsd8Bjm28kwoH_21test_ashmem_asan_ctor6AshmemENtNtNtB7_3ops5deref5Deref5derefBH_>:
          20d4: fe 0f 1f f8  	str	x30, [sp, #-16]!
    00000000000020e8 <_RNvXsd_NtCsgc5Frn9pZ9V_4core5arrayAhj100_INtNtNtB7_3ops5index5IndexINtNtBI_5range7RangeTojEE5indexCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          20f0: fe 1b 00 f9  	str	x30, [sp, #48]
    0000000000002150 <_RNvXsd_NtCsgc5Frn9pZ9V_4core5arrayAhj10b_INtNtNtB7_3ops5index5IndexINtNtBI_5range7RangeTojEE5indexCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          2158: fe 1b 00 f9  	str	x30, [sp, #48]
    00000000000021b8 <_RNvXse_NtCsgc5Frn9pZ9V_4core5arrayAhj100_INtNtNtB7_3ops5index8IndexMutINtNtBI_5range7RangeTojEE9index_mutCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          21c0: fe 0b 00 f9  	str	x30, [sp, #16]
    00000000000021f0 <_RNvXse_NtCsgc5Frn9pZ9V_4core5arrayAhj10b_INtNtNtB7_3ops5index8IndexMutINtNtBI_5range5RangejEE9index_mutCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          21f8: fe 0b 00 f9  	str	x30, [sp, #16]
    0000000000002234 <_RNvXse_NtCsgc5Frn9pZ9V_4core5arrayAhj10b_INtNtNtB7_3ops5index8IndexMutINtNtBI_5range7RangeTojEE9index_mutCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          223c: fe 0b 00 f9  	str	x30, [sp, #16]
    00000000000022bc <_RNvXsp_NtCsgc5Frn9pZ9V_4core6resultINtB5_6ResultilEINtNtNtB7_3ops9try_trait12FromResidualIBy_NtNtB7_7convert10InfalliblelEE13from_residualCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          22c4: fe 23 00 f9  	str	x30, [sp, #64]
    00000000000023b0 <_RNvYNvYjNtNtCsgc5Frn9pZ9V_4core3cmp3Ord3cmpINtNtNtBa_3ops8function6FnOnceTRjB1a_EE9call_onceCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          23b8: fe 0b 00 f9  	str	x30, [sp, #16]
    00000000000023e0 <_RNvYjNtNtCsgc5Frn9pZ9V_4core3cmp3Ord3minCsd8Bjm28kwoH_21test_ashmem_asan_ctor>:
          23e4: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000002400 <_RNvMCsd8Bjm28kwoH_21test_ashmem_asan_ctorNtB2_15UserSliceReader8read_all>:
          2408: fe 43 00 f9  	str	x30, [sp, #128]
    0000000000002570 <_RNvMs_Csd8Bjm28kwoH_21test_ashmem_asan_ctorNtB4_15UserSliceWriter9write_all>:
          2578: fe 4b 00 f9  	str	x30, [sp, #144]
    0000000000003490 <_RNvMs2_Csd8Bjm28kwoH_21test_ashmem_asan_ctorNtB5_6Ashmem8set_size>:
          3498: fe 2b 00 f9  	str	x30, [sp, #80]
    00000000000035d4 <_RNvMs2_Csd8Bjm28kwoH_21test_ashmem_asan_ctorNtB5_6Ashmem8get_size>:
          35dc: fe 1b 00 f9  	str	x30, [sp, #48]
    0000000000003648 <_RNvMs2_Csd8Bjm28kwoH_21test_ashmem_asan_ctorNtB5_6Ashmem13set_prot_mask>:
          3650: fe 33 00 f9  	str	x30, [sp, #96]
    00000000000037dc <_RNvMs2_Csd8Bjm28kwoH_21test_ashmem_asan_ctorNtB5_6Ashmem13get_prot_mask>:
          37e4: fe 1b 00 f9  	str	x30, [sp, #48]
    0000000000003e98 <_RNvCsd8Bjm28kwoH_21test_ashmem_asan_ctor20shrinker_should_stop>:
          3ea0: fe 0b 00 f9  	str	x30, [sp, #16]
    0000000000003ed4 <is_ashmem_file>:
          3edc: fe 13 00 f9  	str	x30, [sp, #32]
    0000000000003f68 <_RNvCsd8Bjm28kwoH_21test_ashmem_asan_ctor12shrink_count>:
          3f70: fe 0b 00 f9  	str	x30, [sp, #16]
    0000000000003f9c <_RNvCsd8Bjm28kwoH_21test_ashmem_asan_ctor11shrink_scan>:
          3fa4: fe 1b 00 f9  	str	x30, [sp, #48]
    0000000000000000 <asan.module_ctor>:
             4: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000000000 <asan.module_dtor>:
             4: fe 0f 1f f8  	str	x30, [sp, #-16]!

  asan.module_ctor prologue:
    0000000000000000 <asan.module_ctor>:
           0: 3f 23 03 d5  	hint	#25
           4: fe 0f 1f f8  	str	x30, [sp, #-16]!
           8: 00 00 00 90  	adrp	x0, 0x0 <asan.module_ctor+0x8>
           c: 00 00 00 91  	add	x0, x0, #0
          10: 08 06 80 52  	mov	w8, #48
          14: e1 03 08 2a  	mov	w1, w8
          18: 00 00 00 94  	bl	0x18 <asan.module_ctor+0x18>
          1c: fe 07 41 f8  	ldr	x30, [sp], #16

═══════════════════════════════════════════════════════════
  KASAN WITH -Cforce-frame-pointers=yes (proposed fix)
═══════════════════════════════════════════════════════════

  Total functions          : 58
  paciasp (PAC-RET)        : 0
  stp x29, x30 (GOOD)     : 56
  str x30       (BAD)     : 2
  asan.module_ctor present : YES

  *** str x30 sites: ***
    0000000000000000 <asan.module_ctor>:
             4: fe 0f 1f f8  	str	x30, [sp, #-16]!
    0000000000000000 <asan.module_dtor>:
             4: fe 0f 1f f8  	str	x30, [sp, #-16]!

  asan.module_ctor prologue:
    0000000000000000 <asan.module_ctor>:
           0: 3f 23 03 d5  	hint	#25
           4: fe 0f 1f f8  	str	x30, [sp, #-16]!
           8: 00 00 00 90  	adrp	x0, 0x0 <asan.module_ctor+0x8>
           c: 00 00 00 91  	add	x0, x0, #0
          10: 08 06 80 52  	mov	w8, #48
          14: e1 03 08 2a  	mov	w1, w8
          18: 00 00 00 94  	bl	0x18 <asan.module_ctor+0x18>
          1c: fe 07 41 f8  	ldr	x30, [sp], #16

═══════════════════════════════════════════════════════════
  VERDICT
═══════════════════════════════════════════════════════════

  baseline (no KASAN)     : 0 str x30
  KASAN (no FP flag)      : 39 str x30
  KASAN (with FP flag)    : 2 str x30

  RESULT: asan.module_ctor IGNORES -Cforce-frame-pointers=yes

  Two bugs:
  1. Kernel: KBUILD_RUSTFLAGS missing -Cforce-frame-pointers=yes
     → fixes user functions but not asan.module_ctor
  2. LLVM: asan.module_ctor skips frame-pointer=all attribute
     → file at https://github.com/llvm/llvm-project/issues

═══════════════════════════════════════════════════════════
