Build with your kernel's rustc nightly using -Zsanitizer=kernel-address:
RUSTC=/path/to/prebuilts/rust/linux-x86/bin/rustc

# KASAN + frame pointers — reproduces the asan.module_ctor bug:
$RUSTC +nightly --edition=2021 --crate-type=lib \
  --target=aarch64-unknown-none -Cpanic=abort \
  -Copt-level=2 -Crelocation-model=static \
  -Cembed-bitcode=n -Clto=n -Ccodegen-units=1 \
  -Cforce-unwind-tables=yes -Cforce-frame-pointers=yes \
  -Zbranch-protection=pac-ret -Zfixed-x18 \
  -Zsanitizer=kernel-address \
  -o kasan.o --emit=obj test_ashmem_asan_ctor.rs

# Check — asan.module_ctor should be the only str x30:
llvm-objdump -d kasan.o | \
  awk '/^[0-9a-f]+ <.*>:/{fn=$0} /str.*x30, \[sp/{print fn; print "  "$0}'
