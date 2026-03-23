warning: type `UserSliceReader` is more private than the item `Ashmem::ashmem_set_name`
  --> test_ashmem.rs:83:5
   |
83 |     pub fn ashmem_set_name(self: Pin<&Self>, reader: UserSliceReader) -> Result<isize> {
   |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ method `Ashmem::ashmem_set_name` is reachable at visibility `pub`
   |
note: but type `UserSliceReader` is only usable at visibility `pub(crate)`
  --> test_ashmem.rs:36:1
   |
36 | struct UserSliceReader { addr: usize, remaining: usize }
   | ^^^^^^^^^^^^^^^^^^^^^^
   = note: `#[warn(private_interfaces)]` on by default

warning: type `UserSliceWriter` is more private than the item `Ashmem::ashmem_get_name`
  --> test_ashmem.rs:98:5
   |
98 |     pub fn ashmem_get_name(self: Pin<&Self>, writer: UserSliceWriter) -> Result<isize> {
   |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ method `Ashmem::ashmem_get_name` is reachable at visibility `pub`
   |
note: but type `UserSliceWriter` is only usable at visibility `pub(crate)`
  --> test_ashmem.rs:37:1
   |
37 | struct UserSliceWriter { addr: usize, remaining: usize }
   | ^^^^^^^^^^^^^^^^^^^^^^

warning: type `UserSliceReader` is more private than the item `Ashmem::ashmem_pin_unpin`
   --> test_ashmem.rs:155:5
    |
155 |     pub fn ashmem_pin_unpin(self: Pin<&Self>, cmd: u32, reader: UserSliceReader) -> Result<isize> {
    |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ method `Ashmem::ashmem_pin_unpin` is reachable at visibility `pub`
    |
note: but type `UserSliceReader` is only usable at visibility `pub(crate)`
   --> test_ashmem.rs:36:1
    |
36  | struct UserSliceReader { addr: usize, remaining: usize }
    | ^^^^^^^^^^^^^^^^^^^^^^

error: casting `&T` to `&mut T` is undefined behavior, even if the reference is unused, consider instead using an `UnsafeCell`
  --> test_ashmem.rs:84:27
   |
84 |         let me = unsafe { &mut *(self.get_ref() as *const Ashmem as *mut Ashmem) };
   |                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   |
   = note: for more information, visit <https://doc.rust-lang.org/book/ch15-05-interior-mutability.html>
   = note: `#[deny(invalid_reference_casting)]` on by default
error: casting `&T` to `&mut T` is undefined behavior, even if the reference is unused, consider instead using an `UnsafeCell`
   --> test_ashmem.rs:119:27
    |
119 |         let me = unsafe { &mut *(self.get_ref() as *const Ashmem as *mut Ashmem) };
    |                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    |
    = note: for more information, visit <https://doc.rust-lang.org/book/ch15-05-interior-mutability.html>

error: casting `&T` to `&mut T` is undefined behavior, even if the reference is unused, consider instead using an `UnsafeCell`
   --> test_ashmem.rs:135:27
    |
135 |         let me = unsafe { &mut *(self.get_ref() as *const Ashmem as *mut Ashmem) };
    |                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
    |
    = note: for more information, visit <https://doc.rust-lang.org/book/ch15-05-interior-mutability.html>

error: aborting due to 3 previous errors; 3 warnings emitted
