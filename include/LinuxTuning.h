#ifndef NANOMATCH_LINUX_TUNING_H
#define NANOMATCH_LINUX_TUNING_H

/**
 * Linux-Exclusive HFT Performance Tuning
 * 
 * These optimizations exploit Linux kernel features that have no Windows equivalent.
 * All functions are no-ops on non-Linux platforms and degrade gracefully if
 * kernel capabilities are missing.
 * 
 * Required capabilities (for full performance):
 *   - CAP_IPC_LOCK   → for mlockall (prevent page faults)
 *   - CAP_SYS_NICE   → for SCHED_FIFO (see ThreadUtils.h)
 *   - Huge Pages      → echo 1024 > /proc/sys/vm/nr_hugepages
 *   - CPU Isolation   → Boot with: isolcpus=0,1 nohz_full=0,1 rcu_nocbs=0,1
 */

#include <iostream>
#include <memory>
#include <cstddef>

#ifdef __linux__
#include <sys/mman.h>   // mmap, munmap, mlockall, MAP_HUGETLB
#endif

namespace NanoMatch {

// ─── 1. Memory Locking (Prevent Page Faults) ────────────────────────────────
// 
// During trading, if the OS decides to page our 512MB memory pool to swap,
// the resulting page fault takes ~10,000 nanoseconds — catastrophic for HFT.
// mlockall() pins ALL current and future memory pages into physical RAM.

inline void lockAllMemory() {
    #ifdef __linux__
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "[WARN] mlockall failed — pages may be swapped. "
                  << "(Run as root or grant CAP_IPC_LOCK)" << std::endl;
    } else {
        std::cout << "[LINUX] All memory pages locked into RAM (no page faults)." << std::endl;
    }
    #endif
    // No-op on Windows/macOS
}

// ─── 2. Huge Page Allocator (Reduce TLB Misses) ─────────────────────────────
//
// The 512MB MemoryPool causes ~131,000 TLB entries with standard 4KB pages.
// A TLB miss costs ~100 CPU cycles. With 2MB huge pages, we need only ~256
// TLB entries — virtually eliminating misses.
//
// BUG FIX: A capturing lambda cannot decay to a raw function pointer void(*)(void*).
// We use a stateless deleter struct that stores the byte count as a static-free
// approach — instead, we encode the allocation size in a wrapper and use
// a stateless free function via the munmap_deleter approach below.
//
// System setup required:
//   echo 1024 > /proc/sys/vm/nr_hugepages
//   # Or permanently in /etc/sysctl.conf:
//   vm.nr_hugepages = 1024

#ifdef __linux__

namespace detail {
    // Stores the mmap byte count alongside the pointer so the deleter is stateless.
    // We prepend the size as a size_t before the actual data region.
    struct HugePageBlock {
        void*  base;   // the raw mmap pointer (includes the header)
        size_t total;  // total bytes passed to mmap
    };

    // Pad the header to 64 bytes to guarantee that the returned pointer
    // remains 64-byte aligned (required by AVX-512 and cache-line boundaries).
    constexpr size_t HUGE_PAGE_HDR_SIZE = (sizeof(HugePageBlock) + 63) & ~63;

    inline void hugepage_deleter(void* p) {
        if (!p) return;
        // Recover the HugePageBlock header stored exactly HUGE_PAGE_HDR_SIZE bytes before the data
        auto* hdr = reinterpret_cast<HugePageBlock*>(
            static_cast<char*>(p) - HUGE_PAGE_HDR_SIZE);
        munmap(hdr->base, hdr->total);
    }
} // namespace detail

template<typename T>
inline std::unique_ptr<T[], void(*)(void*)> make_hugepage(size_t count) {
    constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024; // 2MB

    size_t data_bytes = count * sizeof(T);
    // Total = header + data, rounded up to nearest 2MB
    size_t total = data_bytes + detail::HUGE_PAGE_HDR_SIZE;
    total = (total + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);

    void* raw = mmap(nullptr, total,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                     -1, 0);

    if (raw == MAP_FAILED) {
        std::cerr << "[WARN] Huge page allocation failed for " << data_bytes << " bytes. "
                  << "Falling back to standard pages. "
                  << "(Configure: echo 1024 > /proc/sys/vm/nr_hugepages)" << std::endl;
        return { nullptr, [](void*){} };
    }

    // Write the header into the first bytes of the mapping
    auto* hdr = static_cast<detail::HugePageBlock*>(raw);
    hdr->base  = raw;
    hdr->total = total;

    // Return a pointer to the data region (past the padded header)
    T* data = reinterpret_cast<T*>(static_cast<char*>(raw) + detail::HUGE_PAGE_HDR_SIZE);
    return { data, detail::hugepage_deleter };
}

#endif // __linux__

// ─── 3. Cross-Platform CPU Spin Hint ────────────────────────────────────────
// Used in spin loops (network thread idle, matching engine idle) to signal
// the CPU to reduce power while staying alert — avoids thermal throttling.
// This is more portable than calling _mm_pause() directly in loop bodies.

inline void cpu_pause() noexcept {
    #if defined(_MSC_VER)
        _mm_pause();
    #elif defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
    #elif defined(__aarch64__) || defined(__arm__)
        asm volatile("yield" ::: "memory");
    #else
        // Fallback: compiler barrier to prevent loop optimization
        __asm__ volatile("" ::: "memory");
    #endif
}

// ─── 4. CPU Isolation Guide (Documentation) ─────────────────────────────────
//
// For maximum determinism, isolate the matching engine cores from the Linux
// scheduler entirely. Add to /etc/default/grub:
//
//   GRUB_CMDLINE_LINUX="isolcpus=0,1 nohz_full=0,1 rcu_nocbs=0,1"
//
// Then run: sudo update-grub && reboot
//
// - isolcpus=0,1    → Removes cores 0,1 from general scheduling
// - nohz_full=0,1   → Disables timer ticks on isolated cores (no jitter)
// - rcu_nocbs=0,1   → Offloads RCU callbacks away from isolated cores
//
// ─── 5. Additional sysctl Tuning for HFT ────────────────────────────────────
//
//   net.core.busy_read = 50       (socket busy-poll timeout μs)
//   net.core.busy_poll = 50       (global busy-poll timeout μs)
//   net.core.somaxconn = 4096     (listen backlog)
//   net.ipv4.tcp_fastopen = 3     (client + server TCP fast open)
//   kernel.numa_balancing = 0     (disable auto NUMA migration)

} // namespace NanoMatch

#endif // NANOMATCH_LINUX_TUNING_H
