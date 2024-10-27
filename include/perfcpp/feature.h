#pragma once

#include <linux/version.h>

/// The features of the perf subsystem have evolved over time (more precisely over Linux Kernel generations).
/// In this file, we define some preprocessor variables to keep up with older Linux Kernel versions without yielding
/// errors at compile- and runtime.
/// The documentation for the perf_event_open system call (https://man7.org/linux/man-pages/man2/perf_event_open.2.html)
/// has a great overview of features added in various versions.
/// For the moment, we support Linux 4.0 and newer.

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#define PERFCPP_NO_SAMPLE_BRANCH_IND_JUMP
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
#define PERFCPP_NO_RECORD_SWITCH
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
#define PERFCPP_NO_SAMPLE_BRANCH_CALL
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
#define PERFCPP_NO_SAMPLE_MAX_STACK
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 13, 0)
#define PERFCPP_NO_SAMPLE_PHYS_ADDR
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
#define PERFCPP_NO_RECORD_MISC_SWITCH_OUT_PREEMPT
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
#define PERFCPP_NO_RECORD_CGROUP
#define PERFCPP_NO_SAMPLE_CGROUP
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
#define PERFCPP_NO_SAMPLE_DATA_PAGE_SIZE
#define PERFCPP_NO_SAMPLE_CODE_PAGE_SIZE
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
#define PERFCPP_NO_SAMPLE_WEIGHT_STRUCT
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
#define PERFCPP_NO_CGROUP_SWITCHES
#define PERFCPP_NO_INHERIT_THREAD
#endif