savedcmd_/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.o := aarch64-qcom-linux-gcc   -fuse-ld=bfd -fmacro-prefix-map=/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0=/usr/src/debug/r8168/1.0-r0                      -fdebug-prefix-map=/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0=/usr/src/debug/r8168/1.0-r0                      -fdebug-prefix-map=/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/recipe-sysroot=                      -fdebug-prefix-map=/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/recipe-sysroot-native=  -fdebug-prefix-map=/opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source=/usr/src/kernel -fdebug-prefix-map=/opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-build-artifacts=/usr/src/kernel -Wp,-MMD,/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/.r8168_n.o.d -nostdinc -I/opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include -I./arch/arm64/include/generated -I/opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include -I./include -I/opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi -I./arch/arm64/include/generated/uapi -I/opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi -I./include/generated/uapi -include /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compiler-version.h -include /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kconfig.h -include /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compiler_types.h -D__KERNEL__ -mlittle-endian -DKASAN_SHADOW_SCALE_SHIFT= -fmacro-prefix-map=/opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/= -std=gnu11 -fshort-wchar -funsigned-char -fno-common -fno-PIE -fno-strict-aliasing -mgeneral-regs-only -DCONFIG_CC_HAS_K_CONSTRAINT=1 -Wno-psabi -mabi=lp64 -fno-asynchronous-unwind-tables -fno-unwind-tables -mbranch-protection=pac-ret -Wa,-march=armv8.5-a -DARM64_ASM_ARCH='"armv8.5-a"' -DKASAN_SHADOW_SCALE_SHIFT= -fno-delete-null-pointer-checks -O2 -fno-allow-store-data-races -fstack-protector-strong -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-stack-clash-protection -falign-functions=4 -fno-strict-overflow -fno-stack-check -fconserve-stack -Wall -Wundef -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Werror=strict-prototypes -Wno-format-security -Wno-trigraphs -Wno-frame-address -Wno-address-of-packed-member -Wframe-larger-than=2048 -Wno-main -Wno-unused-but-set-variable -Wno-unused-const-variable -Wvla -Wno-pointer-sign -Wcast-function-type -Wno-array-bounds -Wno-alloc-size-larger-than -Wimplicit-fallthrough=5 -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -Wenum-conversion -Wno-unused-but-set-variable -Wno-unused-const-variable -Wno-restrict -Wno-packed-not-aligned -Wno-format-overflow -Wno-format-truncation -Wno-stringop-overflow -Wno-stringop-truncation -Wno-missing-field-initializers -Wno-type-limits -Wno-shift-negative-value -Wno-maybe-uninitialized -Wno-sign-compare -g -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=1464 -DCONFIG_R8168_NAPI -DCONFIG_R8168_VLAN -DENABLE_S5WOL -DCONFIG_DYNAMIC_ASPM  -DMODULE  -DKBUILD_BASENAME='"r8168_n"' -DKBUILD_MODNAME='"r8168"' -D__KBUILD_MODNAME=kmod_r8168 -c -o /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.o /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.c  

source_/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.o := /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.c

deps_/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.o := \
    $(wildcard include/config/ASPM) \
    $(wildcard include/config/DYNAMIC_ASPM) \
    $(wildcard include/config/SOC_LAN) \
    $(wildcard include/config/R8168_NAPI) \
    $(wildcard include/config/DOWN_SPEED_100) \
    $(wildcard include/config/R8168_VLAN) \
    $(wildcard include/config/NET_POLL_CONTROLLER) \
    $(wildcard include/config/PM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compiler-version.h \
    $(wildcard include/config/CC_VERSION_TEXT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kconfig.h \
    $(wildcard include/config/CPU_BIG_ENDIAN) \
    $(wildcard include/config/BOOGER) \
    $(wildcard include/config/FOO) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compiler_types.h \
    $(wildcard include/config/DEBUG_INFO_BTF) \
    $(wildcard include/config/PAHOLE_HAS_BTF_TAG) \
    $(wildcard include/config/FUNCTION_ALIGNMENT) \
    $(wildcard include/config/CC_IS_GCC) \
    $(wildcard include/config/X86_64) \
    $(wildcard include/config/ARM64) \
    $(wildcard include/config/HAVE_ARCH_COMPILER_H) \
    $(wildcard include/config/CC_HAS_ASM_INLINE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compiler_attributes.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compiler-gcc.h \
    $(wildcard include/config/RETPOLINE) \
    $(wildcard include/config/GCC_ASM_GOTO_OUTPUT_WORKAROUND) \
    $(wildcard include/config/ARCH_USE_BUILTIN_BSWAP) \
    $(wildcard include/config/SHADOW_CALL_STACK) \
    $(wildcard include/config/KCOV) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/compiler.h \
    $(wildcard include/config/ARM64_PTR_AUTH_KERNEL) \
    $(wildcard include/config/ARM64_PTR_AUTH) \
    $(wildcard include/config/BUILTIN_RETURN_ADDRESS_STRIPS_PAC) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/module.h \
    $(wildcard include/config/MODULES) \
    $(wildcard include/config/SYSFS) \
    $(wildcard include/config/MODULES_TREE_LOOKUP) \
    $(wildcard include/config/LIVEPATCH) \
    $(wildcard include/config/STACKTRACE_BUILD_ID) \
    $(wildcard include/config/ARCH_USES_CFI_TRAPS) \
    $(wildcard include/config/MODULE_SIG) \
    $(wildcard include/config/GENERIC_BUG) \
    $(wildcard include/config/KALLSYMS) \
    $(wildcard include/config/SMP) \
    $(wildcard include/config/TRACEPOINTS) \
    $(wildcard include/config/TREE_SRCU) \
    $(wildcard include/config/BPF_EVENTS) \
    $(wildcard include/config/DEBUG_INFO_BTF_MODULES) \
    $(wildcard include/config/JUMP_LABEL) \
    $(wildcard include/config/TRACING) \
    $(wildcard include/config/EVENT_TRACING) \
    $(wildcard include/config/FTRACE_MCOUNT_RECORD) \
    $(wildcard include/config/KPROBES) \
    $(wildcard include/config/HAVE_STATIC_CALL_INLINE) \
    $(wildcard include/config/KUNIT) \
    $(wildcard include/config/PRINTK_INDEX) \
    $(wildcard include/config/MODULE_UNLOAD) \
    $(wildcard include/config/CONSTRUCTORS) \
    $(wildcard include/config/FUNCTION_ERROR_INJECTION) \
    $(wildcard include/config/DYNAMIC_DEBUG_CORE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/list.h \
    $(wildcard include/config/LIST_HARDENED) \
    $(wildcard include/config/DEBUG_LIST) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/container_of.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/build_bug.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compiler.h \
    $(wildcard include/config/TRACE_BRANCH_PROFILING) \
    $(wildcard include/config/PROFILE_ALL_BRANCHES) \
    $(wildcard include/config/OBJTOOL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/rwonce.h \
    $(wildcard include/config/LTO) \
    $(wildcard include/config/AS_HAS_LDAPR) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/rwonce.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kasan-checks.h \
    $(wildcard include/config/KASAN_GENERIC) \
    $(wildcard include/config/KASAN_SW_TAGS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/types.h \
    $(wildcard include/config/HAVE_UID16) \
    $(wildcard include/config/UID16) \
    $(wildcard include/config/ARCH_DMA_ADDR_T_64BIT) \
    $(wildcard include/config/PHYS_ADDR_T_64BIT) \
    $(wildcard include/config/64BIT) \
    $(wildcard include/config/ARCH_32BIT_USTAT_F_TINODE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/types.h \
  arch/arm64/include/generated/uapi/asm/types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/int-ll64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/int-ll64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/bitsperlong.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitsperlong.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/bitsperlong.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/posix_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/stddef.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/stddef.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/posix_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/posix_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kcsan-checks.h \
    $(wildcard include/config/KCSAN) \
    $(wildcard include/config/KCSAN_WEAK_MEMORY) \
    $(wildcard include/config/KCSAN_IGNORE_ATOMICS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/poison.h \
    $(wildcard include/config/ILLEGAL_POINTER_VALUE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/const.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/const.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/const.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/barrier.h \
    $(wildcard include/config/ARM64_PSEUDO_NMI) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/alternative-macros.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/bits.h \
  arch/arm64/include/generated/asm/cpucaps.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/insn-def.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/brk-imm.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/stringify.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/barrier.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/stat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/stat.h \
    $(wildcard include/config/COMPAT) \
  arch/arm64/include/generated/uapi/asm/stat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/stat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/time.h \
    $(wildcard include/config/POSIX_TIMERS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cache.h \
    $(wildcard include/config/ARCH_HAS_CACHE_LINE_SIZE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/kernel.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/sysinfo.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/cache.h \
    $(wildcard include/config/KASAN_HW_TAGS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bitops.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bits.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/typecheck.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/generic-non-atomic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/bitops.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/builtin-__ffs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/builtin-ffs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/builtin-__fls.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/builtin-fls.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/ffz.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/fls64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/sched.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/hweight.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/arch_hweight.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/const_hweight.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/atomic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/atomic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/atomic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/cmpxchg.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/lse.h \
    $(wildcard include/config/ARM64_LSE_ATOMICS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/atomic_ll_sc.h \
    $(wildcard include/config/CC_HAS_K_CONSTRAINT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/export.h \
    $(wildcard include/config/MODVERSIONS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/linkage.h \
    $(wildcard include/config/ARCH_USE_SYM_ANNOTATIONS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/linkage.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/alternative.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/init.h \
    $(wildcard include/config/HAVE_ARCH_PREL32_RELOCATIONS) \
    $(wildcard include/config/STRICT_KERNEL_RWX) \
    $(wildcard include/config/STRICT_MODULE_RWX) \
    $(wildcard include/config/LTO_CLANG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/atomic_lse.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/atomic/atomic-arch-fallback.h \
    $(wildcard include/config/GENERIC_ATOMIC64) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/atomic/atomic-long.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/atomic/atomic-instrumented.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/instrumented.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kmsan-checks.h \
    $(wildcard include/config/KMSAN) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/instrumented-atomic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/lock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/instrumented-lock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/non-atomic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/non-instrumented-non-atomic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/le.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/byteorder.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/byteorder/little_endian.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/byteorder/little_endian.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/swab.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/swab.h \
  arch/arm64/include/generated/uapi/asm/swab.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/swab.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/byteorder/generic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bitops/ext2-atomic-setbit.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kasan-enabled.h \
    $(wildcard include/config/KASAN) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/static_key.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/jump_label.h \
    $(wildcard include/config/HAVE_ARCH_JUMP_LABEL_RELATIVE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/jump_label.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/insn.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/cputype.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/sysreg.h \
    $(wildcard include/config/BROKEN_GAS_INST) \
    $(wildcard include/config/ARM64_PA_BITS_52) \
    $(wildcard include/config/ARM64_4K_PAGES) \
    $(wildcard include/config/ARM64_16K_PAGES) \
    $(wildcard include/config/ARM64_64K_PAGES) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kasan-tags.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/gpr-num.h \
  arch/arm64/include/generated/asm/sysreg-defs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bitfield.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/mte-def.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/math64.h \
    $(wildcard include/config/ARCH_SUPPORTS_INT128) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/math.h \
  arch/arm64/include/generated/asm/div64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/div64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/math64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/time64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/time64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/time.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/time_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/time32.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/timex.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/timex.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/param.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/param.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/param.h \
    $(wildcard include/config/HZ) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/param.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/timex.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/arch_timer.h \
    $(wildcard include/config/ARM_ARCH_TIMER_OOL_WORKAROUND) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/hwcap.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/hwcap.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/cpufeature.h \
    $(wildcard include/config/ARM64_PAN) \
    $(wildcard include/config/ARM64_SW_TTBR0_PAN) \
    $(wildcard include/config/ARM64_SVE) \
    $(wildcard include/config/ARM64_SME) \
    $(wildcard include/config/ARM64_CNP) \
    $(wildcard include/config/ARM64_MTE) \
    $(wildcard include/config/ARM64_DEBUG_PRIORITY_MASKING) \
    $(wildcard include/config/ARM64_BTI) \
    $(wildcard include/config/ARM64_TLB_RANGE) \
    $(wildcard include/config/ARM64_PA_BITS) \
    $(wildcard include/config/ARM64_HW_AFDBM) \
    $(wildcard include/config/ARM64_AMU_EXTN) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bug.h \
    $(wildcard include/config/BUG_ON_DATA_CORRUPTION) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/bug.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/asm-bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/bug.h \
    $(wildcard include/config/BUG) \
    $(wildcard include/config/GENERIC_BUG_RELATIVE_POINTERS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/instrumentation.h \
    $(wildcard include/config/NOINSTR_VALIDATION) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/once_lite.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/panic.h \
    $(wildcard include/config/PANIC_TIMEOUT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/printk.h \
    $(wildcard include/config/MESSAGE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_QUIET) \
    $(wildcard include/config/EARLY_PRINTK) \
    $(wildcard include/config/PRINTK) \
    $(wildcard include/config/DYNAMIC_DEBUG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/stdarg.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kern_levels.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ratelimit_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/spinlock_types_raw.h \
    $(wildcard include/config/DEBUG_SPINLOCK) \
    $(wildcard include/config/DEBUG_LOCK_ALLOC) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/spinlock_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/qspinlock_types.h \
    $(wildcard include/config/NR_CPUS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/qrwlock_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/lockdep_types.h \
    $(wildcard include/config/PROVE_RAW_LOCK_NESTING) \
    $(wildcard include/config/LOCKDEP) \
    $(wildcard include/config/LOCK_STAT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kernel.h \
    $(wildcard include/config/PREEMPT_VOLUNTARY_BUILD) \
    $(wildcard include/config/PREEMPT_DYNAMIC) \
    $(wildcard include/config/HAVE_PREEMPT_DYNAMIC_CALL) \
    $(wildcard include/config/HAVE_PREEMPT_DYNAMIC_KEY) \
    $(wildcard include/config/PREEMPT_) \
    $(wildcard include/config/DEBUG_ATOMIC_SLEEP) \
    $(wildcard include/config/MMU) \
    $(wildcard include/config/PROVE_LOCKING) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/align.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/limits.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/limits.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/limits.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/hex.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kstrtox.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/log2.h \
    $(wildcard include/config/ARCH_HAS_ILOG2_U32) \
    $(wildcard include/config/ARCH_HAS_ILOG2_U64) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/minmax.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sprintf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/static_call_types.h \
    $(wildcard include/config/HAVE_STATIC_CALL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/instruction_pointer.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/smp.h \
    $(wildcard include/config/UP_LATE_INIT) \
    $(wildcard include/config/DEBUG_PREEMPT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/errno.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/errno.h \
  arch/arm64/include/generated/uapi/asm/errno.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/errno.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/errno-base.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cpumask.h \
    $(wildcard include/config/FORCE_NR_CPUS) \
    $(wildcard include/config/HOTPLUG_CPU) \
    $(wildcard include/config/DEBUG_PER_CPU_MAPS) \
    $(wildcard include/config/CPUMASK_OFFSTACK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/threads.h \
    $(wildcard include/config/BASE_SMALL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bitmap.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/find.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/string.h \
    $(wildcard include/config/BINARY_PRINTF) \
    $(wildcard include/config/FORTIFY_SOURCE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/err.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/overflow.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/string.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/string.h \
    $(wildcard include/config/ARCH_HAS_UACCESS_FLUSHCACHE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/fortify-string.h \
    $(wildcard include/config/CC_HAS_KASAN_MEMINTRINSIC_PREFIX) \
    $(wildcard include/config/GENERIC_ENTRY) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/gfp_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/numa.h \
    $(wildcard include/config/NODES_SHIFT) \
    $(wildcard include/config/NUMA_KEEP_MEMINFO) \
    $(wildcard include/config/NUMA) \
    $(wildcard include/config/HAVE_ARCH_NODE_DEV_GROUP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/smp_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/llist.h \
    $(wildcard include/config/ARCH_HAVE_NMI_SAFE_CMPXCHG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/preempt.h \
    $(wildcard include/config/PREEMPT_RT) \
    $(wildcard include/config/PREEMPT_COUNT) \
    $(wildcard include/config/TRACE_PREEMPT_TOGGLE) \
    $(wildcard include/config/PREEMPTION) \
    $(wildcard include/config/PREEMPT_NOTIFIERS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cleanup.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/preempt.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/thread_info.h \
    $(wildcard include/config/THREAD_INFO_IN_TASK) \
    $(wildcard include/config/HAVE_ARCH_WITHIN_STACK_FRAMES) \
    $(wildcard include/config/HARDENED_USERCOPY) \
    $(wildcard include/config/SH) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/restart_block.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/current.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/thread_info.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/memory.h \
    $(wildcard include/config/ARM64_VA_BITS) \
    $(wildcard include/config/KASAN_SHADOW_OFFSET) \
    $(wildcard include/config/VMAP_STACK) \
    $(wildcard include/config/RANDOMIZE_BASE) \
    $(wildcard include/config/DEBUG_VIRTUAL) \
    $(wildcard include/config/EFI) \
    $(wildcard include/config/ARM_GIC_V3_ITS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sizes.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/page-def.h \
    $(wildcard include/config/ARM64_PAGE_SHIFT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mmdebug.h \
    $(wildcard include/config/DEBUG_VM) \
    $(wildcard include/config/DEBUG_VM_IRQSOFF) \
    $(wildcard include/config/DEBUG_VM_PGFLAGS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/boot.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/memory_model.h \
    $(wildcard include/config/FLATMEM) \
    $(wildcard include/config/SPARSEMEM_VMEMMAP) \
    $(wildcard include/config/SPARSEMEM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pfn.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/stack_pointer.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/smp.h \
    $(wildcard include/config/ARM64_ACPI_PARKING_PROTOCOL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/percpu.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/percpu.h \
    $(wildcard include/config/HAVE_SETUP_PER_CPU_AREA) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/percpu-defs.h \
    $(wildcard include/config/DEBUG_FORCE_WEAK_PER_CPU) \
    $(wildcard include/config/AMD_MEM_ENCRYPT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/clocksource/arm_arch_timer.h \
    $(wildcard include/config/ARM_ARCH_TIMER) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/timecounter.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/timex.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/time32.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/time.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/compat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/compat.h \
    $(wildcard include/config/COMPAT_FOR_U64_ALIGNMENT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched.h \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_NATIVE) \
    $(wildcard include/config/SCHED_INFO) \
    $(wildcard include/config/SCHEDSTATS) \
    $(wildcard include/config/SCHED_CORE) \
    $(wildcard include/config/FAIR_GROUP_SCHED) \
    $(wildcard include/config/RT_GROUP_SCHED) \
    $(wildcard include/config/RT_MUTEXES) \
    $(wildcard include/config/UCLAMP_TASK) \
    $(wildcard include/config/UCLAMP_BUCKETS_COUNT) \
    $(wildcard include/config/KMAP_LOCAL) \
    $(wildcard include/config/CGROUP_SCHED) \
    $(wildcard include/config/BLK_DEV_IO_TRACE) \
    $(wildcard include/config/PREEMPT_RCU) \
    $(wildcard include/config/TASKS_RCU) \
    $(wildcard include/config/TASKS_TRACE_RCU) \
    $(wildcard include/config/MEMCG) \
    $(wildcard include/config/LRU_GEN) \
    $(wildcard include/config/COMPAT_BRK) \
    $(wildcard include/config/CGROUPS) \
    $(wildcard include/config/BLK_CGROUP) \
    $(wildcard include/config/PSI) \
    $(wildcard include/config/PAGE_OWNER) \
    $(wildcard include/config/EVENTFD) \
    $(wildcard include/config/IOMMU_SVA) \
    $(wildcard include/config/CPU_SUP_INTEL) \
    $(wildcard include/config/TASK_DELAY_ACCT) \
    $(wildcard include/config/STACKPROTECTOR) \
    $(wildcard include/config/ARCH_HAS_SCALED_CPUTIME) \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_GEN) \
    $(wildcard include/config/NO_HZ_FULL) \
    $(wildcard include/config/POSIX_CPUTIMERS) \
    $(wildcard include/config/POSIX_CPU_TIMERS_TASK_WORK) \
    $(wildcard include/config/KEYS) \
    $(wildcard include/config/SYSVIPC) \
    $(wildcard include/config/DETECT_HUNG_TASK) \
    $(wildcard include/config/IO_URING) \
    $(wildcard include/config/AUDIT) \
    $(wildcard include/config/AUDITSYSCALL) \
    $(wildcard include/config/DEBUG_MUTEXES) \
    $(wildcard include/config/TRACE_IRQFLAGS) \
    $(wildcard include/config/UBSAN) \
    $(wildcard include/config/UBSAN_TRAP) \
    $(wildcard include/config/COMPACTION) \
    $(wildcard include/config/TASK_XACCT) \
    $(wildcard include/config/CPUSETS) \
    $(wildcard include/config/X86_CPU_RESCTRL) \
    $(wildcard include/config/FUTEX) \
    $(wildcard include/config/PERF_EVENTS) \
    $(wildcard include/config/NUMA_BALANCING) \
    $(wildcard include/config/RSEQ) \
    $(wildcard include/config/SCHED_MM_CID) \
    $(wildcard include/config/FAULT_INJECTION) \
    $(wildcard include/config/LATENCYTOP) \
    $(wildcard include/config/FUNCTION_GRAPH_TRACER) \
    $(wildcard include/config/UPROBES) \
    $(wildcard include/config/BCACHE) \
    $(wildcard include/config/SECURITY) \
    $(wildcard include/config/BPF_SYSCALL) \
    $(wildcard include/config/GCC_PLUGIN_STACKLEAK) \
    $(wildcard include/config/X86_MCE) \
    $(wildcard include/config/KRETPROBES) \
    $(wildcard include/config/RETHOOK) \
    $(wildcard include/config/ARCH_HAS_PARANOID_L1D_FLUSH) \
    $(wildcard include/config/RV) \
    $(wildcard include/config/USER_EVENTS) \
    $(wildcard include/config/ARCH_TASK_STRUCT_ON_STACK) \
    $(wildcard include/config/PREEMPT_NONE) \
    $(wildcard include/config/PREEMPT_VOLUNTARY) \
    $(wildcard include/config/PREEMPT) \
    $(wildcard include/config/DEBUG_RSEQ) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/sched.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pid.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rculist.h \
    $(wildcard include/config/PROVE_RCU_LIST) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rcupdate.h \
    $(wildcard include/config/TINY_RCU) \
    $(wildcard include/config/RCU_STRICT_GRACE_PERIOD) \
    $(wildcard include/config/RCU_LAZY) \
    $(wildcard include/config/TASKS_RCU_GENERIC) \
    $(wildcard include/config/RCU_STALL_COMMON) \
    $(wildcard include/config/KVM_XFER_TO_GUEST_WORK) \
    $(wildcard include/config/RCU_NOCB_CPU) \
    $(wildcard include/config/TASKS_RUDE_RCU) \
    $(wildcard include/config/TREE_RCU) \
    $(wildcard include/config/DEBUG_OBJECTS_RCU_HEAD) \
    $(wildcard include/config/PROVE_RCU) \
    $(wildcard include/config/ARCH_WEAK_RELEASE_ACQUIRE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/irqflags.h \
    $(wildcard include/config/IRQSOFF_TRACER) \
    $(wildcard include/config/PREEMPT_TRACER) \
    $(wildcard include/config/DEBUG_IRQFLAGS) \
    $(wildcard include/config/TRACE_IRQFLAGS_SUPPORT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/irqflags.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/ptrace.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/ptrace.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/sve_context.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bottom_half.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/lockdep.h \
    $(wildcard include/config/DEBUG_LOCKING_API_SELFTESTS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/processor.h \
    $(wildcard include/config/KUSER_HELPERS) \
    $(wildcard include/config/ARM64_FORCE_52BIT) \
    $(wildcard include/config/HAVE_HW_BREAKPOINT) \
    $(wildcard include/config/ARM64_TAGGED_ADDR_ABI) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/processor.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/vdso/processor.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/hw_breakpoint.h \
    $(wildcard include/config/CPU_PM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/virt.h \
    $(wildcard include/config/KVM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/sections.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/sections.h \
    $(wildcard include/config/HAVE_FUNCTION_DESCRIPTORS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/kasan.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/mte-kasan.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/pgtable-types.h \
    $(wildcard include/config/PGTABLE_LEVELS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/pgtable-nopud.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/pgtable-nop4d.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/pgtable-hwdef.h \
    $(wildcard include/config/ARM64_CONT_PTE_SHIFT) \
    $(wildcard include/config/ARM64_CONT_PMD_SHIFT) \
    $(wildcard include/config/ARM64_VA_BITS_52) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/pointer_auth.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/prctl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/random.h \
    $(wildcard include/config/VMGENID) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/random.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/ioctl.h \
  arch/arm64/include/generated/uapi/asm/ioctl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/ioctl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/ioctl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/irqnr.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/irqnr.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/prandom.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/once.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/percpu.h \
    $(wildcard include/config/RANDOM_KMALLOC_CACHES) \
    $(wildcard include/config/NEED_PER_CPU_PAGE_FIRST_CHUNK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/spectre.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/fpsimd.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/sigcontext.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/context_tracking_irq.h \
    $(wildcard include/config/CONTEXT_TRACKING_IDLE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rcutree.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/wait.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/spinlock.h \
  arch/arm64/include/generated/asm/mmiowb.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/mmiowb.h \
    $(wildcard include/config/MMIOWB) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/spinlock_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rwlock_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/spinlock.h \
  arch/arm64/include/generated/asm/qspinlock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/qspinlock.h \
  arch/arm64/include/generated/asm/qrwlock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/qrwlock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rwlock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/spinlock_api_smp.h \
    $(wildcard include/config/INLINE_SPIN_LOCK) \
    $(wildcard include/config/INLINE_SPIN_LOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK_BH) \
    $(wildcard include/config/UNINLINE_SPIN_UNLOCK) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/GENERIC_LOCKBREAK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rwlock_api_smp.h \
    $(wildcard include/config/INLINE_READ_LOCK) \
    $(wildcard include/config/INLINE_WRITE_LOCK) \
    $(wildcard include/config/INLINE_READ_LOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_LOCK_BH) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_READ_TRYLOCK) \
    $(wildcard include/config/INLINE_WRITE_TRYLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_BH) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQRESTORE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/wait.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/refcount.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sem.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/sem.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ipc.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/uidgid.h \
    $(wildcard include/config/MULTIUSER) \
    $(wildcard include/config/USER_NS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/highuid.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rhashtable-types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mutex.h \
    $(wildcard include/config/MUTEX_SPIN_ON_OWNER) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/osq_lock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/debug_locks.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/workqueue.h \
    $(wildcard include/config/DEBUG_OBJECTS_WORK) \
    $(wildcard include/config/FREEZER) \
    $(wildcard include/config/WQ_WATCHDOG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/timer.h \
    $(wildcard include/config/DEBUG_OBJECTS_TIMERS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ktime.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/jiffies.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/jiffies.h \
  include/generated/timeconst.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/vdso/ktime.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/timekeeping.h \
    $(wildcard include/config/GENERIC_CMOS_UPDATE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/clocksource_ids.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/debugobjects.h \
    $(wildcard include/config/DEBUG_OBJECTS) \
    $(wildcard include/config/DEBUG_OBJECTS_FREE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/ipc.h \
  arch/arm64/include/generated/uapi/asm/ipcbuf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/ipcbuf.h \
  arch/arm64/include/generated/uapi/asm/sembuf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/sembuf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/shm.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/page.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/personality.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/personality.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/getorder.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/shm.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/hugetlb_encode.h \
  arch/arm64/include/generated/uapi/asm/shmbuf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/shmbuf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/shmparam.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/shmparam.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kmsan_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/plist.h \
    $(wildcard include/config/DEBUG_PLIST) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/hrtimer.h \
    $(wildcard include/config/HIGH_RES_TIMERS) \
    $(wildcard include/config/TIME_LOW_RES) \
    $(wildcard include/config/TIMERFD) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/hrtimer_defs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rbtree.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rbtree_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/seqlock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/timerqueue.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/seccomp.h \
    $(wildcard include/config/SECCOMP) \
    $(wildcard include/config/HAVE_ARCH_SECCOMP_FILTER) \
    $(wildcard include/config/SECCOMP_FILTER) \
    $(wildcard include/config/CHECKPOINT_RESTORE) \
    $(wildcard include/config/SECCOMP_CACHE_DEBUG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/seccomp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/seccomp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/unistd.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/unistd.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/unistd.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/seccomp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/unistd.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/nodemask.h \
    $(wildcard include/config/HIGHMEM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/resource.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/resource.h \
  arch/arm64/include/generated/uapi/asm/resource.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/resource.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/resource.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/latencytop.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/prio.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/signal_types.h \
    $(wildcard include/config/OLD_SIGACTION) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/signal.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/signal.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/signal.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/signal.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/signal.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/signal-defs.h \
  arch/arm64/include/generated/uapi/asm/siginfo.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/siginfo.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/syscall_user_dispatch.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mm_types_task.h \
    $(wildcard include/config/ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH) \
    $(wildcard include/config/SPLIT_PTLOCK_CPUS) \
    $(wildcard include/config/ARCH_ENABLE_SPLIT_PMD_PTLOCK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/tlbbatch.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/task_io_accounting.h \
    $(wildcard include/config/TASK_IO_ACCOUNTING) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/posix-timers.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/alarmtimer.h \
    $(wildcard include/config/RTC_CLASS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/rseq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kcsan.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rv.h \
    $(wildcard include/config/RV_REACTORS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/livepatch_sched.h \
  arch/arm64/include/generated/asm/kmap_size.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/kmap_size.h \
    $(wildcard include/config/DEBUG_KMAP_LOCAL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/task_stack.h \
    $(wildcard include/config/STACK_GROWSUP) \
    $(wildcard include/config/DEBUG_STACK_USAGE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/magic.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/stat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/buildid.h \
    $(wildcard include/config/CRASH_CORE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mm_types.h \
    $(wildcard include/config/HAVE_ALIGNED_STRUCT_PAGE) \
    $(wildcard include/config/USERFAULTFD) \
    $(wildcard include/config/PER_VMA_LOCK) \
    $(wildcard include/config/ANON_VMA_NAME) \
    $(wildcard include/config/SWAP) \
    $(wildcard include/config/HAVE_ARCH_COMPAT_MMAP_BASES) \
    $(wildcard include/config/MEMBARRIER) \
    $(wildcard include/config/AIO) \
    $(wildcard include/config/MMU_NOTIFIER) \
    $(wildcard include/config/TRANSPARENT_HUGEPAGE) \
    $(wildcard include/config/HUGETLB_PAGE) \
    $(wildcard include/config/KSM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/auxvec.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/auxvec.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/auxvec.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kref.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/maple_tree.h \
    $(wildcard include/config/MAPLE_RCU_DISABLED) \
    $(wildcard include/config/DEBUG_MAPLE_TREE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rwsem.h \
    $(wildcard include/config/RWSEM_SPIN_ON_OWNER) \
    $(wildcard include/config/DEBUG_RWSEMS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/completion.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/swait.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/uprobes.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/uprobes.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/debug-monitors.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/esr.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/probes.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/page-flags-layout.h \
  include/generated/bounds.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/sparsemem.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/percpu_counter.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/mmu.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kmod.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/umh.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/gfp.h \
    $(wildcard include/config/ZONE_DMA) \
    $(wildcard include/config/ZONE_DMA32) \
    $(wildcard include/config/ZONE_DEVICE) \
    $(wildcard include/config/CONTIG_ALLOC) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mmzone.h \
    $(wildcard include/config/ARCH_FORCE_MAX_ORDER) \
    $(wildcard include/config/CMA) \
    $(wildcard include/config/MEMORY_ISOLATION) \
    $(wildcard include/config/ZSMALLOC) \
    $(wildcard include/config/UNACCEPTED_MEMORY) \
    $(wildcard include/config/LRU_GEN_STATS) \
    $(wildcard include/config/MEMORY_HOTPLUG) \
    $(wildcard include/config/MEMORY_FAILURE) \
    $(wildcard include/config/PAGE_EXTENSION) \
    $(wildcard include/config/DEFERRED_STRUCT_PAGE_INIT) \
    $(wildcard include/config/HAVE_MEMORYLESS_NODES) \
    $(wildcard include/config/SPARSEMEM_EXTREME) \
    $(wildcard include/config/HAVE_ARCH_PFN_VALID) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/list_nulls.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pageblock-flags.h \
    $(wildcard include/config/HUGETLB_PAGE_SIZE_VARIABLE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/page-flags.h \
    $(wildcard include/config/ARCH_USES_PG_UNCACHED) \
    $(wildcard include/config/PAGE_IDLE_FLAG) \
    $(wildcard include/config/ARCH_USES_PG_ARCH_X) \
    $(wildcard include/config/HUGETLB_PAGE_OPTIMIZE_VMEMMAP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/local_lock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/local_lock_internal.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/memory_hotplug.h \
    $(wildcard include/config/HAVE_ARCH_NODEDATA_EXTENSION) \
    $(wildcard include/config/ARCH_HAS_ADD_PAGES) \
    $(wildcard include/config/MEMORY_HOTREMOVE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/notifier.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/srcu.h \
    $(wildcard include/config/TINY_SRCU) \
    $(wildcard include/config/NEED_SRCU_NMI_SAFE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rcu_segcblist.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/srcutree.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rcu_node_tree.h \
    $(wildcard include/config/RCU_FANOUT) \
    $(wildcard include/config/RCU_FANOUT_LEAF) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/topology.h \
    $(wildcard include/config/USE_PERCPU_NUMA_NODE_ID) \
    $(wildcard include/config/SCHED_SMT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/arch_topology.h \
    $(wildcard include/config/ACPI_CPPC_LIB) \
    $(wildcard include/config/GENERIC_ARCH_TOPOLOGY) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/topology.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/topology.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sysctl.h \
    $(wildcard include/config/SYSCTL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/sysctl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/elf.h \
    $(wildcard include/config/ARCH_USE_GNU_PROPERTY) \
    $(wildcard include/config/ARCH_HAVE_ELF_PROT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/elf.h \
    $(wildcard include/config/COMPAT_VDSO) \
  arch/arm64/include/generated/asm/user.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/user.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/elf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/elf-em.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/fs.h \
    $(wildcard include/config/READ_ONLY_THP_FOR_FS) \
    $(wildcard include/config/FS_POSIX_ACL) \
    $(wildcard include/config/CGROUP_WRITEBACK) \
    $(wildcard include/config/IMA) \
    $(wildcard include/config/FILE_LOCKING) \
    $(wildcard include/config/FSNOTIFY) \
    $(wildcard include/config/FS_ENCRYPTION) \
    $(wildcard include/config/FS_VERITY) \
    $(wildcard include/config/EPOLL) \
    $(wildcard include/config/UNICODE) \
    $(wildcard include/config/QUOTA) \
    $(wildcard include/config/FS_DAX) \
    $(wildcard include/config/BLOCK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/wait_bit.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kdev_t.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/kdev_t.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dcache.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rculist_bl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/list_bl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bit_spinlock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/lockref.h \
    $(wildcard include/config/ARCH_USE_CMPXCHG_LOCKREF) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/stringhash.h \
    $(wildcard include/config/DCACHE_WORD_ACCESS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/hash.h \
    $(wildcard include/config/HAVE_ARCH_HASH) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/path.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/list_lru.h \
    $(wildcard include/config/MEMCG_KMEM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/shrinker.h \
    $(wildcard include/config/SHRINKER_DEBUG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/xarray.h \
    $(wildcard include/config/XARRAY_MULTI) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/mm.h \
    $(wildcard include/config/MMU_LAZY_TLB_REFCOUNT) \
    $(wildcard include/config/ARCH_HAS_MEMBARRIER_CALLBACKS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sync_core.h \
    $(wildcard include/config/ARCH_HAS_SYNC_CORE_BEFORE_USERMODE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/radix-tree.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/capability.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/capability.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/semaphore.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/fcntl.h \
    $(wildcard include/config/ARCH_32BIT_OFF_T) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/fcntl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/uapi/asm/fcntl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/fcntl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/openat2.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/migrate_mode.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/percpu-rwsem.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rcuwait.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/signal.h \
    $(wildcard include/config/SCHED_AUTOGROUP) \
    $(wildcard include/config/BSD_PROCESS_ACCT) \
    $(wildcard include/config/TASKSTATS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/signal.h \
    $(wildcard include/config/DYNAMIC_SIGFRAME) \
    $(wildcard include/config/PROC_FS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/jobctl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/task.h \
    $(wildcard include/config/HAVE_EXIT_THREAD) \
    $(wildcard include/config/ARCH_WANTS_DYNAMIC_TASK_STRUCT) \
    $(wildcard include/config/HAVE_ARCH_THREAD_STRUCT_WHITELIST) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/uaccess.h \
    $(wildcard include/config/ARCH_HAS_SUBPAGE_FAULTS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/fault-inject-usercopy.h \
    $(wildcard include/config/FAULT_INJECTION_USERCOPY) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/uaccess.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/kernel-pgtable.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/asm-extable.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/mte.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/extable.h \
    $(wildcard include/config/BPF_JIT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/access_ok.h \
    $(wildcard include/config/ALTERNATE_USER_ADDRESS_SPACE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cred.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/key.h \
    $(wildcard include/config/KEY_NOTIFICATIONS) \
    $(wildcard include/config/NET) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/assoc_array.h \
    $(wildcard include/config/ASSOCIATIVE_ARRAY) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/user.h \
    $(wildcard include/config/VFIO_PCI_ZDEV_KVM) \
    $(wildcard include/config/IOMMUFD) \
    $(wildcard include/config/WATCH_QUEUE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ratelimit.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rcu_sync.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/delayed_call.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/uuid.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/errseq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ioprio.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/rt.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/iocontext.h \
    $(wildcard include/config/BLK_ICQ) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/ioprio.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/fs_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mount.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mnt_idmapping.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/slab.h \
    $(wildcard include/config/DEBUG_SLAB) \
    $(wildcard include/config/SLUB_TINY) \
    $(wildcard include/config/FAILSLAB) \
    $(wildcard include/config/KFENCE) \
    $(wildcard include/config/SLAB) \
    $(wildcard include/config/SLUB) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/percpu-refcount.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kasan.h \
    $(wildcard include/config/KASAN_STACK) \
    $(wildcard include/config/KASAN_VMALLOC) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/fs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/quota.h \
    $(wildcard include/config/QUOTA_NETLINK_INTERFACE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/dqblk_xfs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dqblk_v1.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dqblk_v2.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dqblk_qtree.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/projid.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/quota.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kobject.h \
    $(wildcard include/config/UEVENT_HELPER) \
    $(wildcard include/config/DEBUG_KOBJECT_RELEASE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sysfs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kernfs.h \
    $(wildcard include/config/KERNFS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/idr.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kobject_ns.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/moduleparam.h \
    $(wildcard include/config/ALPHA) \
    $(wildcard include/config/IA64) \
    $(wildcard include/config/PPC64) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rbtree_latch.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/error-injection.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/error-injection.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/tracepoint-defs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dynamic_debug.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/module.h \
    $(wildcard include/config/DYNAMIC_FTRACE) \
    $(wildcard include/config/ARM64_ERRATUM_843419) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/module.h \
    $(wildcard include/config/HAVE_MOD_ARCH_SPECIFIC) \
    $(wildcard include/config/MODULES_USE_ELF_REL) \
    $(wildcard include/config/MODULES_USE_ELF_RELA) \
  include/generated/uapi/linux/version.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pci.h \
    $(wildcard include/config/PCI_IOV) \
    $(wildcard include/config/PCIEAER) \
    $(wildcard include/config/PCIEPORTBUS) \
    $(wildcard include/config/PCIEASPM) \
    $(wildcard include/config/HOTPLUG_PCI_PCIE) \
    $(wildcard include/config/PCIE_PTM) \
    $(wildcard include/config/PCI_MSI) \
    $(wildcard include/config/PCIE_DPC) \
    $(wildcard include/config/PCI_ATS) \
    $(wildcard include/config/PCI_PRI) \
    $(wildcard include/config/PCI_PASID) \
    $(wildcard include/config/PCI_P2PDMA) \
    $(wildcard include/config/PCI_DOE) \
    $(wildcard include/config/PCI_DOMAINS_GENERIC) \
    $(wildcard include/config/PCI) \
    $(wildcard include/config/PCI_DOMAINS) \
    $(wildcard include/config/ACPI) \
    $(wildcard include/config/PCI_QUIRKS) \
    $(wildcard include/config/PCI_MMCONFIG) \
    $(wildcard include/config/ACPI_MCFG) \
    $(wildcard include/config/HOTPLUG_PCI) \
    $(wildcard include/config/OF) \
    $(wildcard include/config/EEH) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/args.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mod_devicetable.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/mei.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/mei_uuid.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ioport.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/device.h \
    $(wildcard include/config/HAS_IOMEM) \
    $(wildcard include/config/GENERIC_MSI_IRQ) \
    $(wildcard include/config/ENERGY_MODEL) \
    $(wildcard include/config/PINCTRL) \
    $(wildcard include/config/DMA_OPS) \
    $(wildcard include/config/DMA_DECLARE_COHERENT) \
    $(wildcard include/config/DMA_CMA) \
    $(wildcard include/config/SWIOTLB) \
    $(wildcard include/config/SWIOTLB_DYNAMIC) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_DEVICE) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_CPU) \
    $(wildcard include/config/ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) \
    $(wildcard include/config/DMA_OPS_BYPASS) \
    $(wildcard include/config/PM_SLEEP) \
    $(wildcard include/config/DEVTMPFS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dev_printk.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/energy_model.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/cpufreq.h \
    $(wildcard include/config/CPU_FREQ) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/topology.h \
    $(wildcard include/config/SCHED_DEBUG) \
    $(wildcard include/config/SCHED_CLUSTER) \
    $(wildcard include/config/SCHED_MC) \
    $(wildcard include/config/CPU_FREQ_GOV_SCHEDUTIL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/idle.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/sd_flags.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/klist.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pm.h \
    $(wildcard include/config/VT_CONSOLE_SLEEP) \
    $(wildcard include/config/CXL_SUSPEND) \
    $(wildcard include/config/PM_CLK) \
    $(wildcard include/config/PM_GENERIC_DOMAINS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/device/bus.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/device/class.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/device/driver.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/device.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pm_wakeup.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/interrupt.h \
    $(wildcard include/config/IRQ_FORCED_THREADING) \
    $(wildcard include/config/GENERIC_IRQ_PROBE) \
    $(wildcard include/config/IRQ_TIMINGS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/irqreturn.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/hardirq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/context_tracking_state.h \
    $(wildcard include/config/CONTEXT_TRACKING_USER) \
    $(wildcard include/config/CONTEXT_TRACKING) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ftrace_irq.h \
    $(wildcard include/config/HWLAT_TRACER) \
    $(wildcard include/config/OSNOISE_TRACER) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/vtime.h \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING) \
    $(wildcard include/config/IRQ_TIME_ACCOUNTING) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/hardirq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/irq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/irq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/kvm_arm.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/hardirq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/irq.h \
    $(wildcard include/config/GENERIC_IRQ_EFFECTIVE_AFF_MASK) \
    $(wildcard include/config/GENERIC_IRQ_IPI) \
    $(wildcard include/config/IRQ_DOMAIN_HIERARCHY) \
    $(wildcard include/config/DEPRECATED_IRQ_CPU_ONOFFLINE) \
    $(wildcard include/config/GENERIC_IRQ_MIGRATION) \
    $(wildcard include/config/GENERIC_PENDING_IRQ) \
    $(wildcard include/config/HARDIRQS_SW_RESEND) \
    $(wildcard include/config/GENERIC_IRQ_LEGACY) \
    $(wildcard include/config/GENERIC_IRQ_MULTI_HANDLER) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/irqhandler.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/io.h \
    $(wildcard include/config/HAS_IOPORT_MAP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/io.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pgtable.h \
    $(wildcard include/config/HIGHPTE) \
    $(wildcard include/config/ARCH_HAS_NONLEAF_PMD_YOUNG) \
    $(wildcard include/config/GUP_GET_PXX_LOW_HIGH) \
    $(wildcard include/config/ARCH_WANT_PMD_MKWRITE) \
    $(wildcard include/config/HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD) \
    $(wildcard include/config/HAVE_ARCH_SOFT_DIRTY) \
    $(wildcard include/config/ARCH_ENABLE_THP_MIGRATION) \
    $(wildcard include/config/ARCH_HAS_PTE_DEVMAP) \
    $(wildcard include/config/HAVE_ARCH_HUGE_VMAP) \
    $(wildcard include/config/X86_ESPFIX64) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/pgtable.h \
    $(wildcard include/config/PAGE_TABLE_CHECK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/proc-fns.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/pgtable-prot.h \
    $(wildcard include/config/ARM64_BTI_KERNEL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/tlbflush.h \
    $(wildcard include/config/ARM64_WORKAROUND_REPEAT_TLBI) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mmu_notifier.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mmap_lock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/interval_tree.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/fixmap.h \
    $(wildcard include/config/ACPI_APEI_GHES) \
    $(wildcard include/config/ARM_SDE_INTERFACE) \
    $(wildcard include/config/UNMAP_KERNEL_AT_EL0) \
    $(wildcard include/config/RELOCATABLE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/fixmap.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/page_table_check.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/pgtable_uffd.h \
    $(wildcard include/config/HAVE_ARCH_USERFAULTFD_WP) \
  arch/arm64/include/generated/asm/early_ioremap.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/early_ioremap.h \
    $(wildcard include/config/GENERIC_EARLY_IOREMAP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/io.h \
    $(wildcard include/config/GENERIC_IOMAP) \
    $(wildcard include/config/TRACE_MMIO_ACCESS) \
    $(wildcard include/config/GENERIC_IOREMAP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/pci_iomap.h \
    $(wildcard include/config/NO_GENERIC_PCI_IOPORT_MAP) \
    $(wildcard include/config/GENERIC_PCI_IOMAP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/logic_pio.h \
    $(wildcard include/config/INDIRECT_PIO) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/fwnode.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/vmalloc.h \
    $(wildcard include/config/HAVE_ARCH_HUGE_VMALLOC) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/vmalloc.h \
    $(wildcard include/config/PTDUMP_DEBUGFS) \
  arch/arm64/include/generated/asm/irq_regs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/irq_regs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/irqdesc.h \
    $(wildcard include/config/GENERIC_IRQ_DEBUGFS) \
    $(wildcard include/config/SPARSE_IRQ) \
    $(wildcard include/config/IRQ_DOMAIN) \
  arch/arm64/include/generated/asm/hw_irq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/hw_irq.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/resource_ext.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/msi_api.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/pci.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/pci_regs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pci_ids.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dmapool.h \
    $(wildcard include/config/HAS_DMA) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/scatterlist.h \
    $(wildcard include/config/NEED_SG_DMA_LENGTH) \
    $(wildcard include/config/NEED_SG_DMA_FLAGS) \
    $(wildcard include/config/DEBUG_SG) \
    $(wildcard include/config/SGL_ALLOC) \
    $(wildcard include/config/ARCH_NO_SG_CHAIN) \
    $(wildcard include/config/SG_POOL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mm.h \
    $(wildcard include/config/HAVE_ARCH_MMAP_RND_BITS) \
    $(wildcard include/config/HAVE_ARCH_MMAP_RND_COMPAT_BITS) \
    $(wildcard include/config/MEM_SOFT_DIRTY) \
    $(wildcard include/config/ARCH_USES_HIGH_VMA_FLAGS) \
    $(wildcard include/config/ARCH_HAS_PKEYS) \
    $(wildcard include/config/PPC) \
    $(wildcard include/config/X86_USER_SHADOW_STACK) \
    $(wildcard include/config/X86) \
    $(wildcard include/config/PARISC) \
    $(wildcard include/config/SPARC64) \
    $(wildcard include/config/HAVE_ARCH_USERFAULTFD_MINOR) \
    $(wildcard include/config/SHMEM) \
    $(wildcard include/config/MIGRATION) \
    $(wildcard include/config/ARCH_HAS_PTE_SPECIAL) \
    $(wildcard include/config/DEBUG_VM_RB) \
    $(wildcard include/config/PAGE_POISONING) \
    $(wildcard include/config/INIT_ON_ALLOC_DEFAULT_ON) \
    $(wildcard include/config/INIT_ON_FREE_DEFAULT_ON) \
    $(wildcard include/config/DEBUG_PAGEALLOC) \
    $(wildcard include/config/ARCH_WANT_OPTIMIZE_DAX_VMEMMAP) \
    $(wildcard include/config/HUGETLBFS) \
    $(wildcard include/config/MAPPING_DIRTY_HELPERS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/range.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/page_ext.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/stacktrace.h \
    $(wildcard include/config/ARCH_STACKWALK) \
    $(wildcard include/config/STACKTRACE) \
    $(wildcard include/config/HAVE_RELIABLE_STACKTRACE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/stackdepot.h \
    $(wildcard include/config/STACKDEPOT_ALWAYS_INIT) \
    $(wildcard include/config/STACKDEPOT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/page_ref.h \
    $(wildcard include/config/DEBUG_PAGE_REF) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/memremap.h \
    $(wildcard include/config/DEVICE_PRIVATE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/huge_mm.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sched/coredump.h \
    $(wildcard include/config/CORE_DUMP_DEFAULT_ELF_HEADERS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/vmstat.h \
    $(wildcard include/config/VM_EVENT_COUNTERS) \
    $(wildcard include/config/DEBUG_TLBFLUSH) \
    $(wildcard include/config/PER_VMA_LOCK_STATS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/vm_event_item.h \
    $(wildcard include/config/MEMORY_BALLOON) \
    $(wildcard include/config/BALLOON_COMPACTION) \
    $(wildcard include/config/ZSWAP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/pci.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dma-mapping.h \
    $(wildcard include/config/DMA_API_DEBUG) \
    $(wildcard include/config/NEED_DMA_MAP_STATE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dma-direction.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mem_encrypt.h \
    $(wildcard include/config/ARCH_HAS_MEM_ENCRYPT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/pci.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netdevice.h \
    $(wildcard include/config/DCB) \
    $(wildcard include/config/HYPERV_NET) \
    $(wildcard include/config/WLAN) \
    $(wildcard include/config/AX25) \
    $(wildcard include/config/MAC80211_MESH) \
    $(wildcard include/config/NET_IPIP) \
    $(wildcard include/config/NET_IPGRE) \
    $(wildcard include/config/IPV6_SIT) \
    $(wildcard include/config/IPV6_TUNNEL) \
    $(wildcard include/config/RPS) \
    $(wildcard include/config/NETPOLL) \
    $(wildcard include/config/XPS) \
    $(wildcard include/config/XDP_SOCKETS) \
    $(wildcard include/config/BQL) \
    $(wildcard include/config/RFS_ACCEL) \
    $(wildcard include/config/FCOE) \
    $(wildcard include/config/XFRM_OFFLOAD) \
    $(wildcard include/config/LIBFCOE) \
    $(wildcard include/config/WIRELESS_EXT) \
    $(wildcard include/config/NET_L3_MASTER_DEV) \
    $(wildcard include/config/IPV6) \
    $(wildcard include/config/TLS_DEVICE) \
    $(wildcard include/config/VLAN_8021Q) \
    $(wildcard include/config/NET_DSA) \
    $(wildcard include/config/TIPC) \
    $(wildcard include/config/ATALK) \
    $(wildcard include/config/CFG80211) \
    $(wildcard include/config/IEEE802154) \
    $(wildcard include/config/6LOWPAN) \
    $(wildcard include/config/MPLS_ROUTING) \
    $(wildcard include/config/MCTP) \
    $(wildcard include/config/NET_XGRESS) \
    $(wildcard include/config/NETFILTER_INGRESS) \
    $(wildcard include/config/NETFILTER_EGRESS) \
    $(wildcard include/config/NET_SCHED) \
    $(wildcard include/config/PCPU_DEV_REFCNT) \
    $(wildcard include/config/GARP) \
    $(wildcard include/config/MRP) \
    $(wildcard include/config/NET_DROP_MONITOR) \
    $(wildcard include/config/CGROUP_NET_PRIO) \
    $(wildcard include/config/MACSEC) \
    $(wildcard include/config/NET_FLOW_LIMIT) \
    $(wildcard include/config/NET_EGRESS) \
    $(wildcard include/config/NET_DEV_REFCNT_TRACKER) \
    $(wildcard include/config/ETHTOOL_NETLINK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/delay.h \
  arch/arm64/include/generated/asm/delay.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/delay.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/prefetch.h \
  arch/arm64/include/generated/asm/local.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/local.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/dynamic_queue_limits.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/net_namespace.h \
    $(wildcard include/config/NF_CONNTRACK) \
    $(wildcard include/config/NF_FLOW_TABLE) \
    $(wildcard include/config/UNIX) \
    $(wildcard include/config/IEEE802154_6LOWPAN) \
    $(wildcard include/config/IP_SCTP) \
    $(wildcard include/config/NETFILTER) \
    $(wildcard include/config/NF_TABLES) \
    $(wildcard include/config/WEXT_CORE) \
    $(wildcard include/config/XFRM) \
    $(wildcard include/config/IP_VS) \
    $(wildcard include/config/MPLS) \
    $(wildcard include/config/CAN) \
    $(wildcard include/config/CRYPTO_USER) \
    $(wildcard include/config/SMC) \
    $(wildcard include/config/NET_NS) \
    $(wildcard include/config/NET_NS_REFCNT_TRACKER) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/flow.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/in6.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/in6.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/libc-compat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/core.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/mib.h \
    $(wildcard include/config/XFRM_STATISTICS) \
    $(wildcard include/config/TLS) \
    $(wildcard include/config/MPTCP) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/snmp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/snmp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/u64_stats_sync.h \
  arch/arm64/include/generated/asm/local64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/local64.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/unix.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/packet.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/ipv4.h \
    $(wildcard include/config/IP_MULTIPLE_TABLES) \
    $(wildcard include/config/IP_ROUTE_CLASSID) \
    $(wildcard include/config/IP_MROUTE) \
    $(wildcard include/config/IP_MROUTE_MULTIPLE_TABLES) \
    $(wildcard include/config/IP_ROUTE_MULTIPATH) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/inet_frag.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/dropreason-core.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/siphash.h \
    $(wildcard include/config/HAVE_EFFICIENT_UNALIGNED_ACCESS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/ipv6.h \
    $(wildcard include/config/IPV6_MULTIPLE_TABLES) \
    $(wildcard include/config/IPV6_SUBTREES) \
    $(wildcard include/config/IPV6_MROUTE) \
    $(wildcard include/config/IPV6_MROUTE_MULTIPLE_TABLES) \
    $(wildcard include/config/NF_DEFRAG_IPV6) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/dst_ops.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/icmpv6.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/nexthop.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/ieee802154_6lowpan.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/sctp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/netfilter.h \
    $(wildcard include/config/LWTUNNEL) \
    $(wildcard include/config/NETFILTER_FAMILY_ARP) \
    $(wildcard include/config/NETFILTER_FAMILY_BRIDGE) \
    $(wildcard include/config/NF_DEFRAG_IPV4) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netfilter_defs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netfilter.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/in.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/in.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/socket.h \
  arch/arm64/include/generated/uapi/asm/socket.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/socket.h \
  arch/arm64/include/generated/uapi/asm/sockios.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/sockios.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/sockios.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/uio.h \
    $(wildcard include/config/ARCH_HAS_COPY_MC) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/uio.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/socket.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/conntrack.h \
    $(wildcard include/config/NF_CT_PROTO_DCCP) \
    $(wildcard include/config/NF_CT_PROTO_SCTP) \
    $(wildcard include/config/NF_CT_PROTO_GRE) \
    $(wildcard include/config/NF_CONNTRACK_EVENTS) \
    $(wildcard include/config/NF_CONNTRACK_LABELS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netfilter/nf_conntrack_tcp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netfilter/nf_conntrack_tcp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netfilter/nf_conntrack_dccp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netfilter/nf_conntrack_tuple_common.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netfilter/nf_conntrack_common.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netfilter/nf_conntrack_common.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netfilter/nf_conntrack_sctp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netfilter/nf_conntrack_sctp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/nftables.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/xfrm.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/xfrm.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/mpls.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/can.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/xdp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/smc.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/bpf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/mctp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/net_trackers.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ref_tracker.h \
    $(wildcard include/config/REF_TRACKER) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ns_common.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/skbuff.h \
    $(wildcard include/config/BRIDGE_NETFILTER) \
    $(wildcard include/config/NET_TC_SKB_EXT) \
    $(wildcard include/config/MAX_SKB_FRAGS) \
    $(wildcard include/config/DEBUG_KMAP_LOCAL_FORCE_MAP) \
    $(wildcard include/config/NET_SOCK_MSG) \
    $(wildcard include/config/SKB_EXTENSIONS) \
    $(wildcard include/config/WIRELESS) \
    $(wildcard include/config/IPV6_NDISC_NODETYPE) \
    $(wildcard include/config/NETFILTER_XT_TARGET_TRACE) \
    $(wildcard include/config/NET_SWITCHDEV) \
    $(wildcard include/config/NET_REDIRECT) \
    $(wildcard include/config/NETFILTER_SKIP_EGRESS) \
    $(wildcard include/config/NET_RX_BUSY_POLL) \
    $(wildcard include/config/NETWORK_SECMARK) \
    $(wildcard include/config/DEBUG_NET) \
    $(wildcard include/config/PAGE_POOL) \
    $(wildcard include/config/NETWORK_PHY_TIMESTAMPING) \
    $(wildcard include/config/MCTP_FLOWS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bvec.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/highmem.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cacheflush.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/cacheflush.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kgdb.h \
    $(wildcard include/config/HAVE_ARCH_KGDB) \
    $(wildcard include/config/KGDB) \
    $(wildcard include/config/SERIAL_KGDB_NMI) \
    $(wildcard include/config/KGDB_HONOUR_BLOCKLIST) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kprobes.h \
    $(wildcard include/config/KRETPROBE_ON_RETHOOK) \
    $(wildcard include/config/KPROBES_SANITY_TEST) \
    $(wildcard include/config/OPTPROBES) \
    $(wildcard include/config/KPROBES_ON_FTRACE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ftrace.h \
    $(wildcard include/config/HAVE_FUNCTION_GRAPH_RETVAL) \
    $(wildcard include/config/FUNCTION_TRACER) \
    $(wildcard include/config/HAVE_DYNAMIC_FTRACE_WITH_ARGS) \
    $(wildcard include/config/DYNAMIC_FTRACE_WITH_REGS) \
    $(wildcard include/config/DYNAMIC_FTRACE_WITH_ARGS) \
    $(wildcard include/config/DYNAMIC_FTRACE_WITH_DIRECT_CALLS) \
    $(wildcard include/config/STACK_TRACER) \
    $(wildcard include/config/DYNAMIC_FTRACE_WITH_CALL_OPS) \
    $(wildcard include/config/FRAME_POINTER) \
    $(wildcard include/config/FUNCTION_GRAPH_RETVAL) \
    $(wildcard include/config/FUNCTION_PROFILER) \
    $(wildcard include/config/FTRACE_SYSCALLS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/trace_recursion.h \
    $(wildcard include/config/FTRACE_RECORD_RECURSION) \
    $(wildcard include/config/ARCH_WANTS_NO_INSTR) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/trace_clock.h \
  arch/arm64/include/generated/asm/trace_clock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/trace_clock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kallsyms.h \
    $(wildcard include/config/KALLSYMS_ALL) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ptrace.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/pid_namespace.h \
    $(wildcard include/config/MEMFD_CREATE) \
    $(wildcard include/config/PID_NS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/nsproxy.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/ptrace.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/ftrace.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/compat.h \
    $(wildcard include/config/ARCH_HAS_SYSCALL_WRAPPER) \
    $(wildcard include/config/X86_X32_ABI) \
    $(wildcard include/config/COMPAT_OLD_SIGACTION) \
    $(wildcard include/config/ODD_RT_SIGACTION) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/hdlc/ioctl.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/aio_abi.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/syscall_wrapper.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/freelist.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rethook.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/kprobes.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/kprobes.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/kgdb.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/cacheflush.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kmsan.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/highmem-internal.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/checksum.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/checksum.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/checksum.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netdev_features.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/flow_dissector.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if_ether.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if_packet.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/net_debug.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/seq_file_net.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/seq_file.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/string_helpers.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ctype.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/string_choices.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netprio_cgroup.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cgroup.h \
    $(wildcard include/config/DEBUG_CGROUP_REF) \
    $(wildcard include/config/CGROUP_CPUACCT) \
    $(wildcard include/config/SOCK_CGROUP_DATA) \
    $(wildcard include/config/CGROUP_DATA) \
    $(wildcard include/config/CGROUP_BPF) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/cgroupstats.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/taskstats.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/user_namespace.h \
    $(wildcard include/config/INOTIFY_USER) \
    $(wildcard include/config/FANOTIFY) \
    $(wildcard include/config/PERSISTENT_KEYRINGS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kernel_stat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cgroup-defs.h \
    $(wildcard include/config/CGROUP_NET_CLASSID) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bpf-cgroup-defs.h \
    $(wildcard include/config/BPF_LSM) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/psi_types.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kthread.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cgroup_subsys.h \
    $(wildcard include/config/CGROUP_DEVICE) \
    $(wildcard include/config/CGROUP_FREEZER) \
    $(wildcard include/config/CGROUP_PERF) \
    $(wildcard include/config/CGROUP_HUGETLB) \
    $(wildcard include/config/CGROUP_PIDS) \
    $(wildcard include/config/CGROUP_RDMA) \
    $(wildcard include/config/CGROUP_MISC) \
    $(wildcard include/config/CGROUP_DEBUG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/cgroup_refcnt.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/neighbour.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/netlink.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/scm.h \
    $(wildcard include/config/SECURITY_NETWORK) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/net.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/sockptr.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/net.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/security.h \
    $(wildcard include/config/SECURITY_INFINIBAND) \
    $(wildcard include/config/SECURITY_NETWORK_XFRM) \
    $(wildcard include/config/SECURITY_PATH) \
    $(wildcard include/config/SECURITYFS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/kernel_read_file.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/file.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/compat.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netlink.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netdevice.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/if_ether.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/if_link.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if_link.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if_bonding.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/pkt_cls.h \
    $(wildcard include/config/NET_CLS_ACT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/pkt_sched.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/netdev.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/hashtable.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/etherdevice.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/crc32.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bitrev.h \
    $(wildcard include/config/HAVE_ARCH_BITREVERSE) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/arch/arm64/include/asm/bitrev.h \
  arch/arm64/include/generated/asm/unaligned.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/asm-generic/unaligned.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/unaligned/packed_struct.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mii.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/linkmode.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ethtool.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/ethtool.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/mii.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/if_vlan.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rtnetlink.h \
    $(wildcard include/config/NET_INGRESS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/rtnetlink.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if_addr.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if_vlan.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ip.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/ip.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/ipv6.h \
    $(wildcard include/config/IPV6_ROUTER_PREF) \
    $(wildcard include/config/IPV6_ROUTE_INFO) \
    $(wildcard include/config/IPV6_OPTIMISTIC_DAD) \
    $(wildcard include/config/IPV6_SEG6_HMAC) \
    $(wildcard include/config/IPV6_MIP6) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/ipv6.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/tcp.h \
    $(wildcard include/config/BPF) \
    $(wildcard include/config/TCP_MD5SIG) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/win_minmax.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/sock.h \
    $(wildcard include/config/SOCK_RX_QUEUE_MAPPING) \
    $(wildcard include/config/SOCK_VALIDATE_XMIT) \
    $(wildcard include/config/INET) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/page_counter.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/memcontrol.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/vmpressure.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/eventfd.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/eventfd.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/writeback.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/flex_proportions.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/backing-dev-defs.h \
    $(wildcard include/config/DEBUG_FS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/blk_types.h \
    $(wildcard include/config/FAIL_MAKE_REQUEST) \
    $(wildcard include/config/BLK_CGROUP_IOCOST) \
    $(wildcard include/config/BLK_INLINE_ENCRYPTION) \
    $(wildcard include/config/BLK_DEV_INTEGRITY) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/bio.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mempool.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rculist_nulls.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/poll.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/poll.h \
  arch/arm64/include/generated/uapi/asm/poll.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/asm-generic/poll.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/eventpoll.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/indirect_call_wrapper.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/dst.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/rcuref.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/neighbour.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/rtnetlink.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netlink.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/tcp_states.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/net_tstamp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/net_tstamp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/l3mdev.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/fib_rules.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/fib_rules.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/fib_notifier.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/inet_connection_sock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/inet_sock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/jhash.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/request_sock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/netns/hash.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/inet_timewait_sock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/timewait_sock.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/tcp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/udp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/udp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/ip6_checksum.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/ip.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/route.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/inetpeer.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/ipv6.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/jump_label_ratelimit.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/if_inet6.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/inet_dscp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/ip_fib.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/arp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/if_arp.h \
    $(wildcard include/config/FIREWIRE_NET) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/if_arp.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/ndisc.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/ipv6_stubs.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/icmpv6.h \
    $(wildcard include/config/NF_NAT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/in_route.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/route.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/lwtunnel.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/lwtunnel.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/mdio.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/uapi/linux/mdio.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168.h \
    $(wildcard include/config/E1000_DISABLE_PACKET_SPLIT) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_dash.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_realwow.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_fiber.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_asf.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/rtl_eeprom.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/rtltool.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_firmware.h \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/firmware.h \
    $(wildcard include/config/FW_LOADER) \
    $(wildcard include/config/FW_UPLOAD) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/linux/proc_fs.h \
    $(wildcard include/config/PROC_PID_ARCH_STATUS) \
  /opt/1.3/build-qcom-wayland/tmp-glibc/work-shared/qcm6490-idp/kernel-source/include/net/gso.h \

/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.o: $(deps_/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.o)

$(deps_/opt/1.3/build-qcom-wayland/tmp-glibc/work/qcm6490_idp-qcom-linux/r8168/1.0-r0/r8168/r8168_n.o):
