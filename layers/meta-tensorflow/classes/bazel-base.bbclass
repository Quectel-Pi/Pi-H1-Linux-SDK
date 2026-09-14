export JAVA_HOME="${STAGING_LIBDIR_NATIVE}/jvm/openjdk-11-native"

# %25 of total number of local CPU cores
BAZEL_JOBS ??= "HOST_CPUS*.25"

# 25% of total amount of local host RAM
BAZEL_MEM ??= "HOST_RAM*.25"

TS_DL_DIR ??= "${DL_DIR}"

CCACHE_DISABLE = "1"

export HOSTTOOLS_DIR
