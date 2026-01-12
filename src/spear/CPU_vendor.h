/*
* Copyright (c) 2026 Maximilian Krebs
 * All rights reserved.
*/

#ifndef SRC_SPEAR_CPU_VENDOR_H
#define SRC_SPEAR_CPU_VENDOR_H

#if !defined(CPU_VENDOR_INTEL)
#define CPU_VENDOR_INTEL   1
#define CPU_VENDOR_AMD     2
#define CPU_VENDOR_UNKNOWN 0
#endif

#if defined(__INTEL_COMPILER) || defined(__INTEL_LLVM_COMPILER)

    #define CPU_VENDOR CPU_VENDOR_INTEL

#elif defined(__znver1__) || defined(__znver2__) || \
defined(__znver3__) || defined(__znver4__)

    #define CPU_VENDOR CPU_VENDOR_AMD

#else
    #define CPU_VENDOR CPU_VENDOR_UNKNOWN
#endif

#if CPU_VENDOR == CPU_VENDOR_UNKNOWN

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <cpuid.h>
#endif

static inline int cpu_vendor_runtime()
{
    unsigned int eax, ebx, ecx, edx;
    char vendor[13];

#if defined(_MSC_VER)
    int regs[4];
    __cpuid(regs, 0);
    ebx = regs[1];
    edx = regs[3];
    ecx = regs[2];
#elif defined(__GNUC__) || defined(__clang__)
    __get_cpuid(0, &eax, &ebx, &ecx, &edx);
#else
    return CPU_VENDOR_UNKNOWN;
#endif

    ((unsigned int*)vendor)[0] = ebx;
    ((unsigned int*)vendor)[1] = edx;
    ((unsigned int*)vendor)[2] = ecx;
    vendor[12] = '\0';

    if (!__builtin_strcmp(vendor, "GenuineIntel"))
        return CPU_VENDOR_INTEL;

    if (!__builtin_strcmp(vendor, "AuthenticAMD"))
        return CPU_VENDOR_AMD;

    return CPU_VENDOR_UNKNOWN;
}

#else
/* Non-x86 */
static inline int cpu_vendor_runtime(void)
{
    return CPU_VENDOR_UNKNOWN;
}
#endif

#endif /* CPU_VENDOR_UNKNOWN */

#endif  // SRC_SPEAR_CPU_VENDOR_H